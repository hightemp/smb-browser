#include "ui/RemoteFileModel.h"

#include <QDateTime>
#include <QStringList>
#include <utility>

namespace smb::ui {

RemoteFileModel::RemoteFileModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int RemoteFileModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_entries.size();
}

int RemoteFileModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return ColumnCount;
}

QVariant RemoteFileModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size() ||
      index.column() < 0 || index.column() >= ColumnCount) {
    return {};
  }

  const auto &entry = m_entries.at(index.row());
  switch (role) {
  case Qt::DisplayRole:
    return displayData(entry, index.column());
  case Qt::TextAlignmentRole:
    if (index.column() == SizeColumn) {
      return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
  case NameRole:
    return entry.name;
  case RemotePathRole:
    return entry.remotePath;
  case TypeRole:
    return smb::core::toString(entry.type);
  case SizeRole:
    return entry.size;
  case ModifiedAtRole:
    return entry.modifiedAt;
  case PermissionsRole:
    return entry.permissions;
  case AttributesRole:
    return entry.attributes;
  case HiddenRole:
    return entry.isHidden;
  default:
    return {};
  }
}

QVariant RemoteFileModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }

  switch (section) {
  case NameColumn:
    return tr("Name");
  case TypeColumn:
    return tr("Type");
  case SizeColumn:
    return tr("Size");
  case ModifiedColumn:
    return tr("Modified");
  case PermissionsColumn:
    return tr("Permissions");
  case AttributesColumn:
    return tr("Attributes");
  default:
    return {};
  }
}

QHash<int, QByteArray> RemoteFileModel::roleNames() const {
  auto roles = QAbstractTableModel::roleNames();
  roles.insert(NameRole, "name");
  roles.insert(RemotePathRole, "remotePath");
  roles.insert(TypeRole, "type");
  roles.insert(SizeRole, "size");
  roles.insert(ModifiedAtRole, "modifiedAt");
  roles.insert(PermissionsRole, "permissions");
  roles.insert(AttributesRole, "attributes");
  roles.insert(HiddenRole, "hidden");
  return roles;
}

void RemoteFileModel::setEntries(QVector<smb::core::RemoteFileEntry> entries,
                                 QString currentRemotePath) {
  beginResetModel();
  m_entries = std::move(entries);
  m_currentRemotePath = std::move(currentRemotePath);
  endResetModel();
}

void RemoteFileModel::clear() { setEntries({}); }

smb::core::RemoteFileEntry RemoteFileModel::entryAt(int row) const {
  if (row < 0 || row >= m_entries.size()) {
    return {};
  }
  return m_entries.at(row);
}

QVector<smb::core::RemoteFileEntry> RemoteFileModel::entries() const {
  return m_entries;
}

QString RemoteFileModel::currentRemotePath() const {
  return m_currentRemotePath;
}

QString
RemoteFileModel::displayType(const smb::core::RemoteFileEntry &entry) const {
  switch (entry.type) {
  case smb::core::RemoteFileType::Directory:
    return tr("Folder");
  case smb::core::RemoteFileType::File:
    return tr("File");
  case smb::core::RemoteFileType::Symlink:
    return tr("Symlink");
  case smb::core::RemoteFileType::Unknown:
    return tr("Unknown");
  }

  return tr("Unknown");
}

QVariant RemoteFileModel::displayData(const smb::core::RemoteFileEntry &entry,
                                      int column) const {
  switch (column) {
  case NameColumn:
    return entry.name;
  case TypeColumn:
    return displayType(entry);
  case SizeColumn:
    return entry.isDirectory() ? QVariant() : QVariant(entry.size);
  case ModifiedColumn:
    return entry.modifiedAt.isValid()
               ? entry.modifiedAt.toLocalTime().toString(Qt::ISODateWithMs)
               : QString();
  case PermissionsColumn:
    return entry.permissions;
  case AttributesColumn:
    return entry.attributes;
  default:
    return {};
  }
}

RemoteFileFilterProxyModel::RemoteFileFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
}

void RemoteFileFilterProxyModel::setFilterText(const QString &filterText) {
  const auto normalized = filterText.trimmed().toLower();
  if (m_filterText == normalized) {
    return;
  }

  m_filterText = normalized;
  invalidateFilter();
}

bool RemoteFileFilterProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex &sourceParent) const {
  if (m_filterText.isEmpty()) {
    return true;
  }

  const auto index = sourceModel()->index(sourceRow, 0, sourceParent);
  if (!index.isValid()) {
    return false;
  }

  const QStringList fields = {
      sourceModel()->data(index, RemoteFileModel::NameRole).toString(),
      sourceModel()->data(index, RemoteFileModel::RemotePathRole).toString(),
      sourceModel()->data(index, RemoteFileModel::TypeRole).toString(),
      sourceModel()->data(index, RemoteFileModel::PermissionsRole).toString(),
      sourceModel()->data(index, RemoteFileModel::AttributesRole).toString(),
  };

  for (const auto &field : fields) {
    if (field.toLower().contains(m_filterText)) {
      return true;
    }
  }

  return false;
}

} // namespace smb::ui
