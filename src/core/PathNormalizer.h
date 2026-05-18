#pragma once

#include "core/Connection.h"
#include "core/Error.h"

#include <QString>

namespace smb::core {

struct NormalizedSmbPath {
  QString inputPath;
  QString normalizedUri;
  QString server;
  QString share;
  QString initialRemotePath;
};

struct ParsedIdentity {
  AuthType authType = AuthType::Password;
  QString domain;
  QString username;
};

class PathNormalizer {
public:
  static Result<NormalizedSmbPath> normalizeSmbPath(const QString &inputPath);
  static Result<ParsedIdentity>
  normalizeIdentity(AuthType authType, const QString &usernameInput,
                    const QString &domainInput = QString());
};

} // namespace smb::core
