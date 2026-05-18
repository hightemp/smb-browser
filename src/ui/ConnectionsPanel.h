#pragma once

#include "core/Connection.h"
#include "ui/ConnectionListModel.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QEvent;

namespace smb::ui {

class ConnectionsPanel final : public QWidget {
  Q_OBJECT

public:
  explicit ConnectionsPanel(QWidget *parent = nullptr);

  void setConnections(QVector<smb::core::Connection> connections);
  QString selectedConnectionId() const;
  QString selectedNormalizedUri() const;
  void retranslateUi();

signals:
  void addRequested();
  void editRequested(const QString &connectionId);
  void deleteRequested(const QString &connectionId);
  void checkRequested(const QString &connectionId);
  void connectRequested(const QString &connectionId);
  void copyPathRequested(const QString &normalizedUri);

private:
  void changeEvent(QEvent *event) override;
  QModelIndex selectedSourceIndex() const;
  void updateActionState();

  ConnectionListModel *m_model = nullptr;
  ConnectionFilterProxyModel *m_filterModel = nullptr;
  QLabel *m_titleLabel = nullptr;
  QLineEdit *m_filterEdit = nullptr;
  QCheckBox *m_favoritesOnly = nullptr;
  QListView *m_listView = nullptr;
  QPushButton *m_addButton = nullptr;
  QPushButton *m_editButton = nullptr;
  QPushButton *m_deleteButton = nullptr;
  QPushButton *m_checkButton = nullptr;
  QPushButton *m_connectButton = nullptr;
  QPushButton *m_copyPathButton = nullptr;
};

} // namespace smb::ui
