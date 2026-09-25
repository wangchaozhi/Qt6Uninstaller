#pragma once

#include <QString>
#include <QStringList>

struct ScopedPathEntry
{
    QString scope;
    QString value;
};

struct ScopedVariable
{
    QString scope;
    QString name;
};

struct UninstallConfig
{
    QString appName;
    QString appId;
    QString displayVersion;
    QString publisher;
    QString installRootRelative = QStringLiteral(".");
    QStringList processNames;
    QStringList userDataPaths;
    QStringList shortcutPaths;
    QStringList registryKeys;
    QList<ScopedPathEntry> pathEntries;
    QList<ScopedVariable> environmentVariables;
    bool removeInstallDirectory = true;
    bool removeUserDataByDefault = false;
    bool requiresAdmin = false;

    static UninstallConfig load(const QString &filePath, QString *errorMessage);
};

QString expandEnvironmentVariables(const QString &value);
