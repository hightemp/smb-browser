#include "core/Connection.h"

namespace smb::core {

QString toString(AuthType authType) {
  switch (authType) {
  case AuthType::Password:
    return QStringLiteral("password");
  case AuthType::Guest:
    return QStringLiteral("guest");
  case AuthType::Anonymous:
    return QStringLiteral("anonymous");
  case AuthType::CurrentUser:
    return QStringLiteral("current_user");
  }

  return QStringLiteral("password");
}

Connection Connection::createEmpty() {
  Connection connection;
  connection.authType = AuthType::Password;
  connection.lastErrorCode = ErrorCode::None;
  connection.isFavorite = false;
  return connection;
}

bool Connection::usesStoredCredential() const {
  return authType == AuthType::Password && !credentialRef.isEmpty();
}

} // namespace smb::core
