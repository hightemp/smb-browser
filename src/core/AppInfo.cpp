#include "core/AppInfo.h"

namespace smb::core {

QString applicationName() { return QStringLiteral("SMB Browser"); }

QString applicationVersion() { return QStringLiteral(SMB_BROWSER_VERSION); }

} // namespace smb::core
