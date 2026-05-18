#include "ui/ConnectionListModel.h"

#include <QStringList>
#include <utility>

namespace smb::ui {

ConnectionListModel::ConnectionListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ConnectionListModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_connections.size();
}

QVariant ConnectionListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= m_connections.size()) {
    return {};
  }

  const auto &connection = m_connections.at(index.row());
  switch (role) {
  case Qt::DisplayRole:
    if (connection.name.isEmpty()) {
      return connection.normalizedUri;
    }
    return QStringLiteral("%1  %2/%3")
        .arg(connection.name, connection.server, connection.share);
  case IdRole:
    return connection.id;
  case NameRole:
    return connection.name;
  case ServerRole:
    return connection.server;
  case ShareRole:
    return connection.share;
  case GroupRole:
    return connection.groupId;
  case FavoriteRole:
    return connection.isFavorite;
  case NormalizedUriRole:
    return connection.normalizedUri;
  default:
    return {};
  }
}

QHash<int, QByteArray> ConnectionListModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  roles.insert(IdRole, "id");
  roles.insert(NameRole, "name");
  roles.insert(ServerRole, "server");
  roles.insert(ShareRole, "share");
  roles.insert(GroupRole, "group");
  roles.insert(FavoriteRole, "favorite");
  roles.insert(NormalizedUriRole, "normalizedUri");
  return roles;
}

void ConnectionListModel::setConnections(
    QVector<smb::core::Connection> connections) {
  beginResetModel();
  m_connections = std::move(connections);
  endResetModel();
}

smb::core::Connection ConnectionListModel::connectionAt(int row) const {
  if (row < 0 || row >= m_connections.size()) {
    return {};
  }
  return m_connections.at(row);
}

ConnectionFilterProxyModel::ConnectionFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
}

void ConnectionFilterProxyModel::setFilterText(const QString &filterText) {
  const auto normalized = filterText.trimmed().toLower();
  if (m_filterText == normalized) {
    return;
  }
  m_filterText = normalized;
  invalidateFilter();
}

void ConnectionFilterProxyModel::setFavoritesOnly(bool favoritesOnly) {
  if (m_favoritesOnly == favoritesOnly) {
    return;
  }
  m_favoritesOnly = favoritesOnly;
  invalidateFilter();
}

bool ConnectionFilterProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex &sourceParent) const {
  const auto index = sourceModel()->index(sourceRow, 0, sourceParent);
  if (!index.isValid()) {
    return false;
  }

  if (m_favoritesOnly &&
      !sourceModel()->data(index, ConnectionListModel::FavoriteRole).toBool()) {
    return false;
  }

  if (m_filterText.isEmpty()) {
    return true;
  }

  const QStringList fields = {
      sourceModel()->data(index, ConnectionListModel::NameRole).toString(),
      sourceModel()->data(index, ConnectionListModel::ServerRole).toString(),
      sourceModel()->data(index, ConnectionListModel::ShareRole).toString(),
      sourceModel()->data(index, ConnectionListModel::GroupRole).toString(),
      sourceModel()
          ->data(index, ConnectionListModel::NormalizedUriRole)
          .toString(),
  };

  for (const auto &field : fields) {
    if (field.toLower().contains(m_filterText)) {
      return true;
    }
  }

  return false;
}

} // namespace smb::ui
