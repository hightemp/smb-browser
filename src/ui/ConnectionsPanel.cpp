#include "ui/ConnectionsPanel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QEvent>
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

  m_titleLabel = new QLabel(this);
  m_titleLabel->setObjectName(QStringLiteral("connectionsTitle"));
  layout->addWidget(m_titleLabel);

  m_filterEdit = new QLineEdit(this);
  m_filterEdit->setObjectName(QStringLiteral("connectionFilterEdit"));
  layout->addWidget(m_filterEdit);

  m_favoritesOnly = new QCheckBox(this);
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
  m_addButton = new QPushButton(this);
  m_addButton->setObjectName(QStringLiteral("panelAddConnectionButton"));
  m_editButton = new QPushButton(this);
  m_editButton->setObjectName(QStringLiteral("panelEditConnectionButton"));
  m_deleteButton = new QPushButton(this);
  m_deleteButton->setObjectName(QStringLiteral("panelDeleteConnectionButton"));
  primaryActions->addWidget(m_addButton);
  primaryActions->addWidget(m_editButton);
  primaryActions->addWidget(m_deleteButton);
  layout->addLayout(primaryActions);

  auto *secondaryActions = new QHBoxLayout();
  secondaryActions->setSpacing(6);
  m_checkButton = new QPushButton(this);
  m_checkButton->setObjectName(QStringLiteral("panelCheckConnectionButton"));
  m_connectButton = new QPushButton(this);
  m_connectButton->setObjectName(QStringLiteral("panelConnectButton"));
  m_copyPathButton = new QPushButton(this);
  m_copyPathButton->setObjectName(QStringLiteral("panelCopyPathButton"));
  secondaryActions->addWidget(m_checkButton);
  secondaryActions->addWidget(m_connectButton);
  secondaryActions->addWidget(m_copyPathButton);
  layout->addLayout(secondaryActions);

  connect(m_addButton, &QPushButton::clicked, this,
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

  retranslateUi();
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

void ConnectionsPanel::retranslateUi() {
  m_titleLabel->setText(tr("Connections"));
  m_filterEdit->setPlaceholderText(tr("Filter connections"));
  m_favoritesOnly->setText(tr("Favorites only"));
  m_addButton->setText(tr("Add"));
  m_editButton->setText(tr("Edit"));
  m_deleteButton->setText(tr("Delete"));
  m_checkButton->setText(tr("Check"));
  m_connectButton->setText(tr("Connect"));
  m_copyPathButton->setText(tr("Copy Path"));
}

void ConnectionsPanel::changeEvent(QEvent *event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }

  QWidget::changeEvent(event);
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
  emit selectionAvailabilityChanged(hasSelection);
}

} // namespace smb::ui
