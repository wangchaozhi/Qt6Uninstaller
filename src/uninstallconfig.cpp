#include "uninstallconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {
QStringList readStringArray(const QJsonObject &object, const QString &name)
{
    QStringList result;
    const QJsonArray array = object.value(name).toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isString() && !entry.toString().trimmed().isEmpty())
            result.append(entry.toString().trimmed());
    }
    return result;
}

QList<ScopedPathEntry> readPathEntries(const QJsonObject &object)
{
    QList<ScopedPathEntry> result;
    for (const QJsonValue &entry : object.value(QStringLiteral("pathEntries")).toArray()) {
        const QJsonObject item = entry.toObject();
        const QString scope = item.value(QStringLiteral("scope")).toString().trimmed().toLower();
        const QString value = item.value(QStringLiteral("value")).toString().trimmed();
        if ((scope == QStringLiteral("user") || scope == QStringLiteral("machine")) && !value.isEmpty())
            result.append({scope, value});
    }
    return result;
}

QList<ScopedVariable> readVariables(const QJsonObject &object)
{
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,127}$"));
    QList<ScopedVariable> result;
    for (const QJsonValue &entry : object.value(QStringLiteral("environmentVariables")).toArray()) {
        const QJsonObject item = entry.toObject();
        const QString scope = item.value(QStringLiteral("scope")).toString().trimmed().toLower();
        const QString name = item.value(QStringLiteral("name")).toString().trimmed();
        if ((scope == QStringLiteral("user") || scope == QStringLiteral("machine"))
            && safeName.match(name).hasMatch()) {
            result.append({scope, name});
        }
    }
    return result;
}
}

UninstallConfig UninstallConfig::load(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("无法读取配置文件：%1").arg(filePath);
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        *errorMessage = QStringLiteral("配置文件 JSON 无效：%1").arg(parseError.errorString());
        return {};
    }

    const QJsonObject object = document.object();
    UninstallConfig config;
    config.appName = object.value(QStringLiteral("appName")).toString().trimmed();
    config.appId = object.value(QStringLiteral("appId")).toString().trimmed();
    config.displayVersion = object.value(QStringLiteral("displayVersion")).toString().trimmed();
    config.publisher = object.value(QStringLiteral("publisher")).toString().trimmed();
    config.installRootRelative = object.value(QStringLiteral("installRootRelative")).toString(QStringLiteral(".")).trimmed();
    config.processNames = readStringArray(object, QStringLiteral("processNames"));
    config.userDataPaths = readStringArray(object, QStringLiteral("userDataPaths"));
    config.shortcutPaths = readStringArray(object, QStringLiteral("shortcutPaths"));
    config.registryKeys = readStringArray(object, QStringLiteral("registryKeys"));
    config.pathEntries = readPathEntries(object);
    config.environmentVariables = readVariables(object);
    config.removeInstallDirectory = object.value(QStringLiteral("removeInstallDirectory")).toBool(true);
    config.removeUserDataByDefault = object.value(QStringLiteral("removeUserDataByDefault")).toBool(false);
    config.requiresAdmin = object.value(QStringLiteral("requiresAdmin")).toBool(false);

    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9._-]{3,100}$"));
    if (config.appName.isEmpty() || !safeId.match(config.appId).hasMatch()) {
        *errorMessage = QStringLiteral("appName 不能为空，appId 只能包含字母、数字、点、下划线和连字符。");
        return {};
    }
    if (config.installRootRelative != QStringLiteral(".")
        && config.installRootRelative != QStringLiteral("..")) {
        *errorMessage = QStringLiteral("installRootRelative 仅允许设置为 . 或 ..。");
        return {};
    }

    *errorMessage = QString();
    return config;
}

QString expandEnvironmentVariables(const QString &value)
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    static const QRegularExpression variablePattern(QStringLiteral("%([^%]+)%"));

    QString expanded = value;
    QRegularExpressionMatch match;
    qsizetype offset = 0;
    while ((match = variablePattern.match(expanded, offset)).hasMatch()) {
        const QString name = match.captured(1);
        const QString replacement = environment.value(name, match.captured(0));
        expanded.replace(match.capturedStart(), match.capturedLength(), replacement);
        offset = match.capturedStart() + replacement.size();
    }
    return expanded;
}
