#include "DeviceModel.hpp"

#include "AssignableProxy.hpp"
#include "DynamicReadableProxy.hpp"
#include <fplus/fplus.hpp>
#include <QApplication>
#include <QDBusReply>
#include <QDebug>
#include <QStyle>
#include <QVariantAnimation>

// 'match' is a method in QAbstractItemModel :(
namespace p = mpark::patterns;
using namespace fplus;
using namespace mpark::patterns;
using namespace TuxClocker::Device;

Q_DECLARE_METATYPE(AssignableItemData)
Q_DECLARE_METATYPE(AssignableProxy*)
Q_DECLARE_METATYPE(DynamicReadableProxy*)
Q_DECLARE_METATYPE(TCDBus::Enumeration)
Q_DECLARE_METATYPE(TCDBus::Range)
Q_DECLARE_METATYPE(EnumerationVec)
Q_DECLARE_METATYPE(TCDBus::Result<QString>)

DeviceModel::DeviceModel(TC::TreeNode<TCDBus::DeviceNode> root, QObject *parent) :
		QStandardItemModel(parent) {
	qDBusRegisterMetaType<TCDBus::Enumeration>();
	qDBusRegisterMetaType<QVector<TCDBus::Enumeration>>();
	qDBusRegisterMetaType<TCDBus::Range>();
	qDBusRegisterMetaType<TCDBus::Result<QString>>();

	/* Data storage:
		- Interface column should store assignable info for editors
		- Name colums should store the interface type for filtering 
		- Parametrization/connection data, where? */ 
			
	setColumnCount(2);
	
	std::function<void(TC::TreeNode<TCDBus::DeviceNode> node,
		QStandardItem*)> traverse;
	traverse = [&traverse, this](auto node, auto item) {
		auto conn = QDBusConnection::systemBus();
		QDBusInterface nodeIface("org.tuxclocker",
			node.value().path,"org.tuxclocker.Node", conn);
		auto nodeName = nodeIface.property("name").toString();
		
		QList<QStandardItem*> rowItems;
		auto nameItem = new QStandardItem;
		nameItem->setText(nodeName);
		rowItems.append(nameItem);
		
		p::match(node.value().interface) (
			pattern("org.tuxclocker.Assignable") = [=, &rowItems]{
				if_let(pattern(some(arg)) = setupAssignable(node, conn))
						= [&](auto item) {
					nameItem->setData(Assignable, InterfaceTypeRole);
					auto icon = assignableIcon();
					nameItem->setData(icon, Qt::DecorationRole);
					rowItems.append(item);
				};
			},
			pattern("org.tuxclocker.DynamicReadable") = [=, &rowItems] {
				if_let(pattern(some(arg)) = setupDynReadable(node, conn))
						= [&](auto item) {
					auto icon = dynamicReadableIcon();
					nameItem->setData(icon, Qt::DecorationRole);	
					
					nameItem->setData(DeviceModel::DynamicReadable,
						InterfaceTypeRole);
					rowItems.append(item);
					//qDebug() << item->data(DynamicReadableProxyRole);
				};
			},
			pattern("org.tuxclocker.StaticReadable") = [=, &rowItems] {
				if_let(pattern(some(arg)) = setupStaticReadable(node, conn))
						= [&](auto item) {
					auto icon = staticReadableIcon();
					nameItem->setData(icon, Qt::DecorationRole);
					nameItem->setData(DeviceModel::StaticReadable,
						InterfaceTypeRole);
					rowItems.append(item);
				};
			},
			pattern(_) = []{}
		);
		item->appendRow(rowItems);
		
		for (auto c_node : node.children())
			traverse(c_node, nameItem);
	};
	auto rootItem = invisibleRootItem();
	
	for (auto &node : root.children())
		traverse(node, rootItem);
}

EnumerationVec toEnumVec(QVector<TCDBus::Enumeration> enums) {
	return transform([](auto e) {
		return Enumeration{e.name.toStdString(), e.key};
	}, enums.toStdVector());
}

std::optional<const AssignableProxy*>
		DeviceModel::assignableProxyFromItem(QStandardItem *item) {
	return (m_assignableProxyHash.contains(item)) ?
		std::optional(m_assignableProxyHash.value(item)) :
		std::nullopt;
}

QString fromAssignmentArgument(AssignmentArgument a_arg) {
	return p::match(a_arg) (
		pattern(as<int>(arg)) = [](auto i) {return QString::number(i);},
		pattern(as<uint>(arg)) = [](auto u) {return QString::number(u);},
		pattern(as<double>(arg)) = [](auto d) {return QString::number(d);},
		pattern(_) = [] {return QString("");}
	);
}

QStandardItem *DeviceModel::createAssignable(TC::TreeNode<TCDBus::DeviceNode> node,
		QDBusConnection conn, AssignableItemData itemData) {
	auto ifaceItem = new AssignableItem(this);
	auto proxy = new AssignableProxy(node.value().path, conn, this);
	
	connect(proxy, &AssignableProxy::connectionValueChanged, [=] (auto result, auto text) {
		p::match(result) (
			pattern(as<QVariant>(arg)) = [=](auto v) {
				QVariant data;
				data.setValue(connectionColor());
				ifaceItem->setData(data, Qt::BackgroundRole);
				ifaceItem->setText(text);
				//qDebug() << text;
			},
			pattern(_) = []{}
		);
	});
	
	
	QVariant pv;
	pv.setValue(proxy);
	ifaceItem->setData(pv, AssignableProxyRole);
	QVariant v;
	v.setValue(itemData);
	ifaceItem->setData(v, AssignableRole);

	// Set initial text to current value (one-time at startup)
	QString text("No value set");
	auto unit = itemData.unit();
	auto currentValue = proxy->currentValue();

	if (currentValue.has_value()) {
		p::match(itemData.assignableInfo()) (
			pattern(as<EnumerationVec>(arg)) = [&](auto e_vec) {
				/* Find index from EnumVec (O(n) doesn't matter since we only search once here)
				   This should never be anything other than an uint but in theory it could be
				   whatever at this point */
				try {
					auto index = std::get<uint>(currentValue.value());
					for (auto &e : e_vec) {
						if (index == e.key) {
							text = QString::fromStdString(e.name);
							break;
						}
					}
				} catch (std::bad_variant_access &e) {}

				//text = QString::fromStdString(e_vec[std::get<uint>(currentValue.value())].name);
			},
			pattern(as<RangeInfo>(_)) = [&]() {
				auto base = fromAssignmentArgument(currentValue.value());
				if (unit.has_value())
					text = QString("%1 %2").arg(base, unit.value());
				else
					text = base;
			}
		);
	}
	ifaceItem->setText(text);

	connect(ifaceItem, &AssignableItem::assignableDataChanged,
			[=](QVariant v) {
		// Only show checkbox when value has been changed
		ifaceItem->setCheckable(true);
		ifaceItem->setCheckState(Qt::Checked);
		proxy->setValue(v);
		ifaceItem->setData(unappliedColor(), Qt::BackgroundRole);
	});
	
	connect(ifaceItem, &AssignableItem::committalChanged, [=](bool on) {
		QVariant colorData = (on) ? unappliedColor() : QVariant();
		ifaceItem->setData(colorData, Qt::BackgroundRole);
	});
	
	connect(proxy, &AssignableProxy::applied, [=](auto err) {
		// Fade out result color
		auto startColor = (err.has_value()) ? errorColor()
			: successColor();
		auto anim = new QVariantAnimation;
		anim->setDuration(fadeOutTime());
		anim->setStartValue(startColor);
		anim->setEndValue(QPalette().color(QPalette::Base));
		
		connect(anim, &QVariantAnimation::valueChanged, [=](QVariant v) {
			QVariant iv;
			iv.setValue(v.value<QColor>());
			ifaceItem->setData(iv, Qt::BackgroundRole);
		});
		
		connect(anim, &QVariantAnimation::finished, [=] {
			// Set invalid color to 'reset' the color
			ifaceItem->setData(QVariant(), Qt::BackgroundRole);
		});
		
		anim->start(QAbstractAnimation::DeleteWhenStopped);
	});
	
	connect(this, &DeviceModel::changesApplied, [=] {
		// Don't apply if unchecked
		if (ifaceItem->checkState() == Qt::Checked) {
			ifaceItem->setCheckState(Qt::Unchecked);
			ifaceItem->setCheckable(false);
			// What the fuck do I need to this for?
			ifaceItem->setData(QVariant(), Qt::CheckStateRole);
			proxy->apply();
		}
	});
	return ifaceItem;
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const {
	if (index.row() != DeviceModel::NameColumn && role == DeviceModel::NodeNameRole) {
		// Get name from adjacent column
		auto nameIndex =
			this->index(index.row(), DeviceModel::NameColumn, index.parent());
		return nameIndex.data(Qt::DisplayRole);
	}
	if (index.column() != InterfaceColumn && role == DynamicReadableProxyRole) {
		auto idx =
			this->index(index.row(), DeviceModel::InterfaceColumn, index.parent());
		return idx.data(DynamicReadableProxyRole);
	}
	return QStandardItemModel::data(index, role);
}

std::optional<QString> fromDBusResult(TCDBus::Result<QString> res) {
	if (res.error)
		return std::nullopt;
	return res.value;
}

std::optional<QStandardItem*> DeviceModel::setupAssignable(
		TC::TreeNode<TCDBus::DeviceNode> node, QDBusConnection conn) {
	QDBusInterface ifaceNode("org.tuxclocker", node.value().path,
		"org.tuxclocker.Assignable", conn);
	// Should never fail
	auto a_info =
		qvariant_cast<QDBusVariant>(ifaceNode.property("assignableInfo"))
		.variant();
	// Get unit of Assignable
	auto dbusUnit = qvariant_cast<TCDBus::Result<QString>>(ifaceNode.property("unit"));
	auto unit = fromDBusResult(dbusUnit);

	// Get initial value
	

	/* TODO: bad hack: this code can only differentiate between
		arrays and structs: make it based on signature instead */
	auto d_arg = qvariant_cast<QDBusArgument>(a_info);
	switch (d_arg.currentType()) {
		case QDBusArgument::StructureType: {
			TCDBus::Range r;
			d_arg >> r;
			AssignableItemData data(r.toAssignableInfo(), unit);
			return createAssignable(node, conn, data);
		}
		case QDBusArgument::ArrayType: {
			QVector<TCDBus::Enumeration> e;
			d_arg >> e;
			AssignableItemData data(toEnumVec(e), unit);
			return createAssignable(node, conn, data);
		}
		default:
			return std::nullopt;
	}
}

template <typename T>
void updateReadItemText(QStandardItem *item, T value,
		std::optional<QString> unit) {
	// TODO: this can be made a lot (around 3x) faster by using direct copying
	// Form a string of the form "1000 MHz" if has unit
	auto text = (unit.has_value()) ?
		QString("%1 %2").arg(value).arg(unit.value()) :
		QString("%1").arg(value);
	item->setText(text);
	//qDebug() << item->data(DeviceModel::DynamicReadableProxyRole);
}

std::optional<QStandardItem*> DeviceModel::setupDynReadable(
		TC::TreeNode<TCDBus::DeviceNode> node, QDBusConnection conn) {
	auto item = new QStandardItem;
	auto proxy = new DynamicReadableProxy(node.value().path, conn, this);
	QVariant v;
	v.setValue(proxy);
	item->setData(v, DynamicReadableProxyRole);
	auto unit = proxy->unit();	
	
	connect(proxy, &DynamicReadableProxy::valueChanged, [=](ReadResult res) {
		p::match(res)(
			pattern(as<ReadableValue>(arg)) = [=](auto rv) {
				p::match(rv)(
					pattern(as<double>(arg)) = [=](auto d) {
						updateReadItemText(item, d, unit);
					},
					pattern(as<int>(arg)) = [=](auto i) {
						updateReadItemText(item, i, unit);
					},
					pattern(as<uint>(arg)) = [=](auto u) {
						updateReadItemText(item, u, unit);
					},
					pattern(_) = []{}
				);
			},
			pattern(_) = []{}
		);
	});
	
	return item;
}

std::optional<QStandardItem*> DeviceModel::setupStaticReadable(
		TC::TreeNode<TCDBus::DeviceNode> node, QDBusConnection conn) {
	QDBusInterface staticIface("org.tuxclocker", node.value().path,
		"org.tuxclocker.StaticReadable", conn);
	auto value = staticIface.property("value")
		.value<QDBusVariant>().variant().toString();
	// Workaround from DynamicReadableProxy for getting property with custom type
	QDBusInterface propIface("org.tuxclocker", node.value().path,
		"org.freedesktop.DBus.Properties", conn);
	QDBusReply<QDBusVariant> reply =
		propIface.call("Get", "org.tuxclocker.StaticReadable", "unit");
	if (!reply.isValid())
		return std::nullopt;
	auto arg = reply.value().variant().value<QDBusArgument>();
	TCDBus::Result<QString> unit;
	arg >> unit;
	
	if (!unit.error)
		value += " " + unit.value;
	
	auto item = new QStandardItem;
	item->setData(value, Qt::DisplayRole);
	return item;
}
