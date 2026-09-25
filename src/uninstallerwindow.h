#pragma once

#include "uninstallconfig.h"

#include <QWidget>
#include <optional>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class UninstallerWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit UninstallerWindow(UninstallConfig config, bool silent = false,
        std::optional<bool> removeUserDataOverride = std::nullopt, QWidget *parent = nullptr);

public slots:
    void startUninstall();

private slots:
    void beginUninstall();

private:
    void appendLog(const QString &message);
    bool stopConfiguredProcesses();
    bool removeConfiguredPaths(const QStringList &paths, const QString &kind);
    bool removeRegistryKeys();
    bool removeEnvironmentSettings();
    bool launchCleanupHelper();
    bool isSafeUserPath(const QString &path) const;
    bool isSafeRegistryKey(const QString &key) const;
    void setBusy(bool busy);

    UninstallConfig m_config;
    QString m_installDirectory;
    QString m_logFilePath;
    QLabel *m_title = nullptr;
    QCheckBox *m_removeUserData = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_uninstallButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    bool m_silent = false;
};
