#pragma once

#include "core/Connection.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

namespace smb::ui {

class ConnectionListModel final : public QAbstractListModel {
  Q_OBJECT

public:
  enum Role {
    IdRole = Qt::UserRole + 1,
    NameRole,
    ServerRole,
    ShareRole,
    GroupRole,
    FavoriteRole,
    NormalizedUriRole,
  };

  explicit ConnectionListModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setConnections(QVector<smb::core::Connection> connections);
  smb::core::Connection connectionAt(int row) const;

private:
  QVector<smb::core::Connection> m_connections;
};

class ConnectionFilterProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit ConnectionFilterProxyModel(QObject *parent = nullptr);

  void setFilterText(const QString &filterText);
  void setFavoritesOnly(bool favoritesOnly);

protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;

private:
  QString m_filterText;
  bool m_favoritesOnly = false;
};

} // namespace smb::ui
