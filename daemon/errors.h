#pragma once

#include <QString>

namespace Errors {

inline const QString UnknownKey = QStringLiteral("net.seilent.MicroSet1.Error.UnknownKey");
inline const QString InvalidValue = QStringLiteral("net.seilent.MicroSet1.Error.InvalidValue");
inline const QString ReadOnly = QStringLiteral("net.seilent.MicroSet1.Error.ReadOnly");
inline const QString HoldPending = QStringLiteral("net.seilent.MicroSet1.Error.HoldPending");
inline const QString AccessDenied = QStringLiteral("org.freedesktop.DBus.Error.AccessDenied");
inline const QString ServiceUnknown = QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown");
inline const QString InvalidArgs = QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs");
inline const QString Failed = QStringLiteral("org.freedesktop.DBus.Error.Failed");

}
