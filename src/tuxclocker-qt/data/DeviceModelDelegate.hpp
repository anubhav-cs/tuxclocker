#pragma once

#include <QMenu>
#include <QStyledItemDelegate>

class AssignableItemData;
class AssignableProxy;
class FunctionEditor;
class QStandardItemModel;

struct AssignableDefaultData {
	QModelIndex index;
	QVariant defaultValue;
};

// TODO: align checkbox to the right
class DeviceModelDelegate : public QStyledItemDelegate {
public:
	DeviceModelDelegate(QObject *parent = nullptr);
	QWidget *createEditor(
	    QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;
	void updateEditorGeometry(
	    QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;
	void setEditorData(QWidget *editor, const QModelIndex &index) const;
	void setModelData(
	    QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
	bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &,
	    const QModelIndex &) override;

	static void setAssignableDefaults(QAbstractItemModel *, QVector<AssignableDefaultData>);
protected:
	void paint(QPainter *painter, const QStyleOptionViewItem &option,
	    const QModelIndex &index) const override;
private:
	void commitAndClose();
	template <typename T>
	static void setAssignableData(QAbstractItemModel *, const QModelIndex &, T data);
	static void setAssignableVariantData(
	    QAbstractItemModel *, const QModelIndex &, QVariant data);
	// Whether to show reset action for a node
	bool subtreeHasAssignableDefaults(QAbstractItemModel *, const QModelIndex &);
	QVector<AssignableDefaultData> subtreeAssignableDefaults(
	    QAbstractItemModel *, const QModelIndex &);

	FunctionEditor *m_functionEditor;
	QAction *m_parametrize;
	QAction *m_resetAssignable;
	QMenu m_menu;
};