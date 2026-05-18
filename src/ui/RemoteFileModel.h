#pragma once

#include "core/RemoteFileEntry.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

namespace smb::ui {

class RemoteFileModel final : public QAbstractTableModel {
  Q_OBJECT

public:
  enum Column {
    NameColumn = 0,
    TypeColumn,
    SizeColumn,
    ModifiedColumn,
    PermissionsColumn,
    AttributesColumn,
    ColumnCount,
  };

  enum Role {
    NameRole = Qt::UserRole + 1,
    RemotePathRole,
    TypeRole,
    SizeRole,
    ModifiedAtRole,
    PermissionsRole,
    AttributesRole,
    HiddenRole,
  };

  explicit RemoteFileModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setEntries(QVector<smb::core::RemoteFileEntry> entries,
                  QString currentRemotePath = {});
  void clear();

  smb::core::RemoteFileEntry entryAt(int row) const;
  QVector<smb::core::RemoteFileEntry> entries() const;
  QString currentRemotePath() const;

private:
  QString displayType(const smb::core::RemoteFileEntry &entry) const;
  QVariant displayData(const smb::core::RemoteFileEntry &entry,
                       int column) const;

  QVector<smb::core::RemoteFileEntry> m_entries;
  QString m_currentRemotePath;
};

class RemoteFileFilterProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit RemoteFileFilterProxyModel(QObject *parent = nullptr);

  void setFilterText(const QString &filterText);

protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;

private:
  QString m_filterText;
};

} // namespace smb::ui
