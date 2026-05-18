#pragma once

#include "core/Error.h"

#include <QDateTime>
#include <QString>

namespace smb::core {

enum class AuthType {
  Password,
  Guest,
  Anonymous,
  CurrentUser,
};

struct Connection {
  QString id;
  QString name;
  QString inputPath;
  QString normalizedUri;
  QString server;
  QString share;
  QString initialRemotePath;
  QString domain;
  QString username;
  AuthType authType = AuthType::Password;
  QString credentialRef;
  QString comment;
  QString groupId;
  bool isFavorite = false;
  QDateTime lastOpenedAt;
  QDateTime createdAt;
  QDateTime updatedAt;
  ErrorCode lastErrorCode = ErrorCode::None;
  QString lastErrorMessage;
  QDateTime lastSuccessfulCheckAt;

  static Connection createEmpty();

  bool usesStoredCredential() const;
};

QString toString(AuthType authType);

} // namespace smb::core
