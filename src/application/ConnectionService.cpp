#include "application/ConnectionService.h"

#include <QUuid>

namespace smb::application {

namespace {

smb::core::AppError missingCredentialError() {
  return smb::core::AppError::fromCode(
      smb::core::ErrorCode::CredentialNotFound,
      smb::core::ErrorCategory::Credentials,
      QStringLiteral("Password authentication requires a credential secret."),
      false);
}

} // namespace

ConnectionService::ConnectionService(
    smb::infrastructure::ConnectionRepository &repository,
    smb::core::CredentialStore &credentialStore)
    : m_repository(repository), m_credentialStore(credentialStore) {}

smb::core::Result<smb::core::Connection>
ConnectionService::create(smb::core::Connection connection,
                          std::optional<smb::core::CredentialSecret> secret) {
  if (connection.id.isEmpty()) {
    connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }

  if (connection.authType == smb::core::AuthType::Password) {
    if (!secret.has_value()) {
      return smb::core::Result<smb::core::Connection>::failure(
          missingCredentialError());
    }

    auto saved = m_credentialStore.save(connection.id, secret.value());
    if (!saved.ok()) {
      return smb::core::Result<smb::core::Connection>::failure(saved.error());
    }
    connection.credentialRef = saved.value();
  } else {
    connection.credentialRef.clear();
  }

  const auto credentialRef = connection.credentialRef;
  const auto shouldCleanupCredential =
      connection.authType == smb::core::AuthType::Password &&
      !credentialRef.isEmpty();

  auto added = m_repository.add(std::move(connection));
  if (!added.ok() && shouldCleanupCredential) {
    m_credentialStore.remove(credentialRef);
  }

  return added;
}

smb::core::Result<smb::core::Connection>
ConnectionService::update(smb::core::Connection connection,
                          std::optional<smb::core::CredentialSecret> secret) {
  const auto existing = m_repository.getById(connection.id);
  if (!existing.ok()) {
    return smb::core::Result<smb::core::Connection>::failure(existing.error());
  }

  const auto previousCredentialRef = existing.value().credentialRef;

  if (connection.authType == smb::core::AuthType::Password) {
    if (secret.has_value()) {
      if (connection.credentialRef.isEmpty()) {
        auto saved = m_credentialStore.save(connection.id, secret.value());
        if (!saved.ok()) {
          return smb::core::Result<smb::core::Connection>::failure(
              saved.error());
        }
        connection.credentialRef = saved.value();
      } else {
        auto updated =
            m_credentialStore.update(connection.credentialRef, secret.value());
        if (!updated.ok()) {
          return smb::core::Result<smb::core::Connection>::failure(
              updated.error());
        }
      }
    } else if (connection.credentialRef.isEmpty()) {
      return smb::core::Result<smb::core::Connection>::failure(
          missingCredentialError());
    }
  } else {
    connection.credentialRef.clear();
    if (!previousCredentialRef.isEmpty() &&
        !credentialIsShared(previousCredentialRef, connection.id)) {
      auto removed = m_credentialStore.remove(previousCredentialRef);
      if (!removed.ok()) {
        return smb::core::Result<smb::core::Connection>::failure(
            removed.error());
      }
    }
  }

  return m_repository.update(std::move(connection));
}

smb::core::Result<bool> ConnectionService::remove(const QString &connectionId) {
  const auto existing = m_repository.getById(connectionId);
  if (!existing.ok()) {
    return smb::core::Result<bool>::failure(existing.error());
  }

  const auto credentialRef = existing.value().credentialRef;
  const auto shouldRemoveCredential =
      !credentialRef.isEmpty() &&
      !credentialIsShared(credentialRef, connectionId);

  auto removedConnection = m_repository.remove(connectionId);
  if (!removedConnection.ok()) {
    return removedConnection;
  }
  if (!removedConnection.value()) {
    return smb::core::Result<bool>::success(false);
  }

  if (shouldRemoveCredential) {
    auto removedCredential = m_credentialStore.remove(credentialRef);
    if (!removedCredential.ok()) {
      return smb::core::Result<bool>::failure(removedCredential.error());
    }
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<QVector<smb::core::Connection>>
ConnectionService::list() const {
  return m_repository.list();
}

smb::core::Result<smb::core::Connection>
ConnectionService::getById(const QString &connectionId) const {
  return m_repository.getById(connectionId);
}

bool ConnectionService::credentialIsShared(
    const QString &credentialRef, const QString &excludingConnectionId) const {
  const auto connections = m_repository.list();
  if (!connections.ok()) {
    return true;
  }

  for (const auto &connection : connections.value()) {
    if (connection.id != excludingConnectionId &&
        connection.credentialRef == credentialRef) {
      return true;
    }
  }

  return false;
}

} // namespace smb::application
