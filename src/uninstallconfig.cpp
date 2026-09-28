#include "uninstallconfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {
QStringList readStringArray(const QJsonObject &object, const QString &name, bool *valid)
{
    QStringList result;
    const QJsonValue value = object.value(name);
    if (!value.isUndefined() && !value.isArray()) {
        *valid = false;
        return result;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isString() && !entry.toString().trimmed().isEmpty()) {
            result.append(entry.toString().trimmed());
        } else {
            *valid = false;
        }
    }
    return result;
}

QList<ScopedPathEntry> readPathEntries(const QJsonObject &object, bool *valid)
{
    QList<ScopedPathEntry> result;
    const QJsonValue arrayValue = object.value(QStringLiteral("pathEntries"));
    if (!arrayValue.isUndefined() && !arrayValue.isArray()) {
        *valid = false;
        return result;
    }
    for (const QJsonValue &entry : arrayValue.toArray()) {
        const QJsonObject item = entry.toObject();
        const QString scope = item.value(QStringLiteral("scope")).toString().trimmed().toLower();
        const QString value = item.value(QStringLiteral("value")).toString().trimmed();
        if ((scope == QStringLiteral("user") || scope == QStringLiteral("machine")) && !value.isEmpty())
            result.append({scope, value});
        else
            *valid = false;
    }
    return result;
}

QList<ScopedVariable> readVariables(const QJsonObject &object, bool *valid)
{
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,127}$"));
    QList<ScopedVariable> result;
    const QJsonValue arrayValue = object.value(QStringLiteral("environmentVariables"));
    if (!arrayValue.isUndefined() && !arrayValue.isArray()) {
        *valid = false;
        return result;
    }
    for (const QJsonValue &entry : arrayValue.toArray()) {
        const QJsonObject item = entry.toObject();
        const QString scope = item.value(QStringLiteral("scope")).toString().trimmed().toLower();
        const QString name = item.value(QStringLiteral("name")).toString().trimmed();
        if ((scope == QStringLiteral("user") || scope == QStringLiteral("machine"))
            && safeName.match(name).hasMatch()) {
            result.append({scope, name});
        } else
            *valid = false;
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
    bool arraysValid = true;
    config.processNames = readStringArray(object, QStringLiteral("processNames"), &arraysValid);
    config.userDataPaths = readStringArray(object, QStringLiteral("userDataPaths"), &arraysValid);
    config.shortcutPaths = readStringArray(object, QStringLiteral("shortcutPaths"), &arraysValid);
    config.registryKeys = readStringArray(object, QStringLiteral("registryKeys"), &arraysValid);
    config.pathEntries = readPathEntries(object, &arraysValid);
    config.environmentVariables = readVariables(object, &arraysValid);
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
    if (!arraysValid) {
        *errorMessage = QStringLiteral("配置数组包含无效项目；请检查字符串、scope、value 和环境变量名称。");
        return {};
    }
    for (const QString &processName : config.processNames) {
        if (QFileInfo(processName).fileName() != processName
            || !processName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            *errorMessage = QStringLiteral("processNames 只能包含以 .exe 结尾的文件名，不能包含路径。");
            return {};
        }
    }
    for (const QString &key : config.registryKeys) {
        const bool allowed = key.startsWith(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"),
            Qt::CaseInsensitive) || key.startsWith(
            QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"),
            Qt::CaseInsensitive);
        if (!allowed) {
            *errorMessage = QStringLiteral("registryKeys 仅允许 Windows 卸载注册表分支。");
            return {};
        }
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
