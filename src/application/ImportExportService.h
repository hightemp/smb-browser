#pragma once

#include "core/Connection.h"
#include "core/ConnectionGroup.h"
#include "core/CredentialStore.h"
#include "core/Error.h"
#include "core/LogSanitizer.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QVector>

namespace smb::application {

struct ExportPayload {
  QVector<smb::core::Connection> connections;
  QVector<smb::core::ConnectionGroup> groups;
};

struct ExportOptions {
  bool includePlainTextPasswords = false;
  bool confirmPlainTextPasswordExport = false;
  const smb::core::CredentialStore *credentialStore = nullptr;
  bool encryptExport = false;
  QByteArray encryptionPassphrase;
};

enum class DuplicatePolicy {
  Skip,
  Replace,
  CreateCopy,
};

struct ImportOptions {
  DuplicatePolicy duplicatePolicy = DuplicatePolicy::Skip;
  QSet<QString> existingConnectionIds;
  QByteArray encryptionPassphrase;
};

struct ImportRecordError {
  int index = -1;
  QString recordName;
  smb::core::AppError error = smb::core::AppError::none();
};

struct ImportResult {
  QVector<smb::core::Connection> connections;
  QVector<smb::core::ConnectionGroup> groups;
  QVector<ImportRecordError> errors;
  int skippedDuplicates = 0;
};

class ImportExportService final {
public:
  smb::core::Result<QByteArray>
  exportConnections(const ExportPayload &payload,
                    const ExportOptions &options = {}) const;
  smb::core::Result<ImportResult>
  importConnections(const QByteArray &bytes,
                    const ImportOptions &options = {}) const;

private:
  smb::core::Result<QJsonObject>
  connectionToJson(const smb::core::Connection &connection,
                   const ExportOptions &options) const;
  QJsonObject groupToJson(const smb::core::ConnectionGroup &group) const;
  smb::core::Result<smb::core::Connection>
  connectionFromJson(const QJsonObject &object, int index) const;
  smb::core::ConnectionGroup groupFromJson(const QJsonObject &object) const;
  static QString dateTimeToString(const QDateTime &dateTime);

  smb::core::LogSanitizer m_sanitizer;
};

} // namespace smb::application
