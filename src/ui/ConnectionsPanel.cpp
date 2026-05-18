#include "ui/ConnectionsPanel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

namespace smb::ui {

ConnectionsPanel::ConnectionsPanel(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("connectionsPanel"));
  setMinimumWidth(260);

  m_model = new ConnectionListModel(this);
  m_filterModel = new ConnectionFilterProxyModel(this);
  m_filterModel->setSourceModel(m_model);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  auto *title = new QLabel(tr("Connections"), this);
  title->setObjectName(QStringLiteral("connectionsTitle"));
  layout->addWidget(title);

  m_filterEdit = new QLineEdit(this);
  m_filterEdit->setObjectName(QStringLiteral("connectionFilterEdit"));
  m_filterEdit->setPlaceholderText(tr("Filter connections"));
  layout->addWidget(m_filterEdit);

  m_favoritesOnly = new QCheckBox(tr("Favorites only"), this);
  m_favoritesOnly->setObjectName(QStringLiteral("favoriteConnectionsOnly"));
  layout->addWidget(m_favoritesOnly);

  m_listView = new QListView(this);
  m_listView->setObjectName(QStringLiteral("connectionsList"));
  m_listView->setModel(m_filterModel);
  m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
  m_listView->setUniformItemSizes(true);
  layout->addWidget(m_listView, 1);

  auto *primaryActions = new QHBoxLayout();
  primaryActions->setSpacing(6);
  auto *addButton = new QPushButton(tr("Add"), this);
  addButton->setObjectName(QStringLiteral("panelAddConnectionButton"));
  m_editButton = new QPushButton(tr("Edit"), this);
  m_editButton->setObjectName(QStringLiteral("panelEditConnectionButton"));
  m_deleteButton = new QPushButton(tr("Delete"), this);
  m_deleteButton->setObjectName(QStringLiteral("panelDeleteConnectionButton"));
  primaryActions->addWidget(addButton);
  primaryActions->addWidget(m_editButton);
  primaryActions->addWidget(m_deleteButton);
  layout->addLayout(primaryActions);

  auto *secondaryActions = new QHBoxLayout();
  secondaryActions->setSpacing(6);
  m_checkButton = new QPushButton(tr("Check"), this);
  m_checkButton->setObjectName(QStringLiteral("panelCheckConnectionButton"));
  m_connectButton = new QPushButton(tr("Connect"), this);
  m_connectButton->setObjectName(QStringLiteral("panelConnectButton"));
  m_copyPathButton = new QPushButton(tr("Copy Path"), this);
  m_copyPathButton->setObjectName(QStringLiteral("panelCopyPathButton"));
  secondaryActions->addWidget(m_checkButton);
  secondaryActions->addWidget(m_connectButton);
  secondaryActions->addWidget(m_copyPathButton);
  layout->addLayout(secondaryActions);

  connect(addButton, &QPushButton::clicked, this,
          &ConnectionsPanel::addRequested);
  connect(m_filterEdit, &QLineEdit::textChanged, m_filterModel,
          &ConnectionFilterProxyModel::setFilterText);
  connect(m_favoritesOnly, &QCheckBox::toggled, m_filterModel,
          &ConnectionFilterProxyModel::setFavoritesOnly);
  connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this]() { updateActionState(); });
  connect(m_listView, &QListView::doubleClicked, this, [this]() {
    const auto id = selectedConnectionId();
    if (!id.isEmpty()) {
      emit connectRequested(id);
    }
  });

  connect(m_editButton, &QPushButton::clicked, this, [this]() {
    const auto id = selectedConnectionId();
    if (!id.isEmpty()) {
      emit editRequested(id);
    }
  });
  connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
    const auto id = selectedConnectionId();
    if (!id.isEmpty()) {
      emit deleteRequested(id);
    }
  });
  connect(m_checkButton, &QPushButton::clicked, this, [this]() {
    const auto id = selectedConnectionId();
    if (!id.isEmpty()) {
      emit checkRequested(id);
    }
  });
  connect(m_connectButton, &QPushButton::clicked, this, [this]() {
    const auto id = selectedConnectionId();
    if (!id.isEmpty()) {
      emit connectRequested(id);
    }
  });
  connect(m_copyPathButton, &QPushButton::clicked, this, [this]() {
    const auto path = selectedNormalizedUri();
    if (!path.isEmpty()) {
      emit copyPathRequested(path);
    }
  });

  updateActionState();
}

void ConnectionsPanel::setConnections(
    QVector<smb::core::Connection> connections) {
  m_model->setConnections(std::move(connections));
  updateActionState();
}

QString ConnectionsPanel::selectedConnectionId() const {
  const auto index = selectedSourceIndex();
  if (!index.isValid()) {
    return {};
  }
  return m_model->data(index, ConnectionListModel::IdRole).toString();
}

QString ConnectionsPanel::selectedNormalizedUri() const {
  const auto index = selectedSourceIndex();
  if (!index.isValid()) {
    return {};
  }
  return m_model->data(index, ConnectionListModel::NormalizedUriRole)
      .toString();
}

QModelIndex ConnectionsPanel::selectedSourceIndex() const {
  const auto selected = m_listView->selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    return {};
  }
  return m_filterModel->mapToSource(selected.first());
}

void ConnectionsPanel::updateActionState() {
  const auto hasSelection = selectedSourceIndex().isValid();
  m_editButton->setEnabled(hasSelection);
  m_deleteButton->setEnabled(hasSelection);
  m_checkButton->setEnabled(hasSelection);
  m_connectButton->setEnabled(hasSelection);
  m_copyPathButton->setEnabled(hasSelection);
}

} // namespace smb::ui
