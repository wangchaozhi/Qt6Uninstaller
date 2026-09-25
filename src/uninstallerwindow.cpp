#include "uninstallerwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTextStream>
#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {
QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isInside(const QString &path, const QString &root)
{
    const QString cleanPath = normalizedPath(path);
    QString cleanRoot = normalizedPath(root);
    if (!cleanRoot.endsWith(QDir::separator()))
        cleanRoot += QDir::separator();
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
}

bool removeOnePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (info.isFile() || info.isSymLink())
        return QFile::remove(path);
    return QDir(path).removeRecursively();
}

#ifdef Q_OS_WIN
bool deleteRegistryTree(const QString &key)
{
    const QString currentUserPrefix =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\");
    const QString localMachinePrefix =
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\");

    HKEY root = nullptr;
    QString subKey;
    if (key.startsWith(currentUserPrefix, Qt::CaseInsensitive)) {
        root = HKEY_CURRENT_USER;
        subKey = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\")
            + key.sliced(currentUserPrefix.size());
    } else if (key.startsWith(localMachinePrefix, Qt::CaseInsensitive)) {
        root = HKEY_LOCAL_MACHINE;
        subKey = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\")
            + key.sliced(localMachinePrefix.size());
    } else {
        return false;
    }

    const LSTATUS status = RegDeleteTreeW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()));
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

QString environmentRegistryPath(const QString &scope)
{
    return scope == QStringLiteral("machine")
        ? QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment")
        : QStringLiteral("HKEY_CURRENT_USER\\Environment");
}

QString comparablePath(const QString &path)
{
    return QDir::cleanPath(expandEnvironmentVariables(path).trimmed()).toLower();
}
#endif
}

UninstallerWindow::UninstallerWindow(UninstallConfig config, bool silent,
    std::optional<bool> removeUserDataOverride, QWidget *parent)
    : QWidget(parent), m_config(std::move(config)), m_silent(silent)
{
    m_installDirectory = normalizedPath(QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(m_config.installRootRelative));
    m_logFilePath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("%1-uninstall.log").arg(m_config.appId));
    QFile::remove(m_logFilePath);
    setWindowTitle(QStringLiteral("卸载 %1").arg(m_config.appName));
    setFixedWidth(600);
    setStyleSheet(QStringLiteral(R"(
        QWidget { font-family: "Microsoft YaHei UI"; font-size: 14px; color: #202124; }
        QPlainTextEdit { background: #f7f8fa; border: 1px solid #dfe1e5; border-radius: 7px; padding: 7px; }
        QProgressBar { border: 0; border-radius: 4px; background: #e8eaed; height: 8px; text-align: center; }
        QProgressBar::chunk { border-radius: 4px; background: #2563eb; }
        QPushButton { min-width: 88px; padding: 7px 16px; border: 1px solid #c8ccd1; border-radius: 7px; background: white; }
        QPushButton:hover { background: #f3f4f6; }
        QPushButton:default { color: white; border-color: #dc2626; background: #dc2626; }
        QPushButton:default:hover { background: #b91c1c; }
        QPushButton:disabled { color: #9aa0a6; background: #f1f3f4; }
        QCheckBox { spacing: 8px; }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

    m_title = new QLabel(QStringLiteral("卸载 %1").arg(m_config.appName), this);
    QFont titleFont = m_title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    QString details;
    if (!m_config.displayVersion.isEmpty())
        details += QStringLiteral("版本 %1").arg(m_config.displayVersion);
    if (!m_config.publisher.isEmpty())
        details += details.isEmpty() ? m_config.publisher : QStringLiteral(" · %1").arg(m_config.publisher);
    auto *description = new QLabel(QStringLiteral("将从此电脑删除应用程序。你的项目文件不会被删除。%1")
        .arg(details.isEmpty() ? QString() : QStringLiteral("\n%1").arg(details)), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_removeUserData = new QCheckBox(QStringLiteral("同时删除设置、缓存和登录状态"), this);
    m_removeUserData->setChecked(removeUserDataOverride.value_or(m_config.removeUserDataByDefault));
    layout->addWidget(m_removeUserData);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(200);
    m_log->setFixedHeight(150);
    layout->addWidget(m_log);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_uninstallButton = new QPushButton(QStringLiteral("卸载"), this);
    m_uninstallButton->setDefault(true);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_uninstallButton);
    layout->addLayout(buttons);

    connect(m_cancelButton, &QPushButton::clicked, this, &QWidget::close);
    connect(m_uninstallButton, &QPushButton::clicked, this, &UninstallerWindow::beginUninstall);

    appendLog(QStringLiteral("安装目录：%1").arg(m_installDirectory));
    appendLog(QStringLiteral("日志文件：%1").arg(m_logFilePath));
    appendLog(QStringLiteral("计划：%1 个进程，%2 个快捷方式，%3 个用户数据目录，%4 个环境项。")
        .arg(m_config.processNames.size()).arg(m_config.shortcutPaths.size())
        .arg(m_config.userDataPaths.size())
        .arg(m_config.pathEntries.size() + m_config.environmentVariables.size()));
}

void UninstallerWindow::startUninstall()
{
    beginUninstall();
}

void UninstallerWindow::beginUninstall()
{
    if (!QFileInfo::exists(QDir(m_installDirectory).filePath(QStringLiteral("uninstaller.marker")))) {
        const QString message = QStringLiteral("安装根目录缺少 uninstaller.marker，已拒绝执行卸载。");
        appendLog(message);
        if (!m_silent)
            QMessageBox::critical(this, QStringLiteral("安全检查失败"), message);
        else
            QCoreApplication::exit(3);
        return;
    }

    const QString extra = m_removeUserData->isChecked()
        ? QStringLiteral("\n\n设置、缓存和登录状态也会被永久删除。") : QString();
    if (!m_silent && QMessageBox::question(this, QStringLiteral("确认卸载"),
            QStringLiteral("确定要卸载 %1 吗？%2").arg(m_config.appName, extra),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    setBusy(true);
    QTimer::singleShot(0, this, [this] {
        bool success = true;
        m_progress->setValue(10);
        appendLog(QStringLiteral("正在结束应用进程……"));
        success = stopConfiguredProcesses() && success;

        m_progress->setValue(35);
        appendLog(QStringLiteral("正在删除快捷方式……"));
        success = removeConfiguredPaths(m_config.shortcutPaths, QStringLiteral("快捷方式")) && success;

        m_progress->setValue(55);
        appendLog(QStringLiteral("正在清理卸载注册项……"));
        success = removeRegistryKeys() && success;

        m_progress->setValue(65);
        appendLog(QStringLiteral("正在清理环境变量……"));
        success = removeEnvironmentSettings() && success;

        if (m_removeUserData->isChecked()) {
            m_progress->setValue(75);
            appendLog(QStringLiteral("正在删除用户数据……"));
            success = removeConfiguredPaths(m_config.userDataPaths, QStringLiteral("用户数据")) && success;
        }

            m_progress->setValue(88);
        if (m_config.removeInstallDirectory) {
            appendLog(QStringLiteral("正在启动自清理程序……"));
            success = launchCleanupHelper() && success;
        }

        if (!success) {
            m_progress->setValue(0);
            setBusy(false);
            if (!m_silent) {
                QMessageBox::warning(this, QStringLiteral("卸载未完成"),
                    QStringLiteral("部分项目未能删除。请查看日志，并尝试以管理员身份运行。"));
            } else {
                QCoreApplication::exit(2);
            }
            return;
        }

        m_progress->setValue(100);
        appendLog(QStringLiteral("卸载操作已提交，安装目录将在本窗口关闭后删除。"));
        if (!m_silent) {
            QMessageBox::information(this, QStringLiteral("卸载完成"),
                QStringLiteral("%1 已卸载。感谢使用。 ").arg(m_config.appName));
        }
        QCoreApplication::exit(0);
    });
}

void UninstallerWindow::appendLog(const QString &message)
{
    m_log->appendPlainText(message);
    QFile file(m_logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << QStringLiteral("  ") << message << u'\n';
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

bool UninstallerWindow::stopConfiguredProcesses()
{
#ifdef Q_OS_WIN
    bool success = true;
    const DWORD ownPid = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == ownPid)
                continue;
            const QString executable = QString::fromWCharArray(entry.szExeFile);
            bool configured = false;
            for (const QString &name : m_config.processNames) {
                if (!name.contains(u'/') && !name.contains(u'\\')
                    && executable.compare(name, Qt::CaseInsensitive) == 0) {
                    configured = true;
                    break;
                }
            }
            if (!configured)
                continue;

            const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process) {
                appendLog(QStringLiteral("无法结束进程：%1").arg(executable));
                success = false;
                continue;
            }
            if (!TerminateProcess(process, 0) || WaitForSingleObject(process, 3000) == WAIT_FAILED) {
                appendLog(QStringLiteral("结束进程失败：%1").arg(executable));
                success = false;
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return success;
#else
    return false;
#endif
}

bool UninstallerWindow::removeConfiguredPaths(const QStringList &paths, const QString &kind)
{
    bool success = true;
    for (const QString &configuredPath : paths) {
        const QString path = normalizedPath(expandEnvironmentVariables(configuredPath));
        if (!isSafeUserPath(path)) {
            appendLog(QStringLiteral("已拒绝不安全的%1路径：%2").arg(kind, path));
            success = false;
            continue;
        }
        if (!removeOnePath(path)) {
            appendLog(QStringLiteral("删除失败：%1").arg(path));
            success = false;
        } else {
            appendLog(QStringLiteral("已处理：%1").arg(path));
        }
    }
    return success;
}

bool UninstallerWindow::removeRegistryKeys()
{
    bool success = true;
    for (const QString &key : m_config.registryKeys) {
        if (!isSafeRegistryKey(key)) {
            appendLog(QStringLiteral("已拒绝不安全的注册表项：%1").arg(key));
            success = false;
            continue;
        }
#ifdef Q_OS_WIN
        if (!deleteRegistryTree(key)) {
            appendLog(QStringLiteral("清理注册表失败：%1").arg(key));
            success = false;
        }
#else
        Q_UNUSED(key);
        success = false;
#endif
    }
    return success;
}

bool UninstallerWindow::removeEnvironmentSettings()
{
#ifdef Q_OS_WIN
    bool success = true;
    QSet<QString> touchedScopes;

    for (const ScopedPathEntry &entry : m_config.pathEntries) {
        QSettings settings(environmentRegistryPath(entry.scope), QSettings::NativeFormat);
        const QString existing = settings.value(QStringLiteral("Path")).toString();
        const QString target = comparablePath(entry.value);
        QStringList kept;
        bool changed = false;
        for (const QString &part : existing.split(u';', Qt::SkipEmptyParts)) {
            if (comparablePath(part) == target)
                changed = true;
            else
                kept.append(part.trimmed());
        }
        if (changed) {
            settings.setValue(QStringLiteral("Path"), kept.join(u';'));
            settings.sync();
            if (settings.status() != QSettings::NoError) {
                appendLog(QStringLiteral("PATH 清理失败（%1）：%2").arg(entry.scope, entry.value));
                success = false;
            } else {
                appendLog(QStringLiteral("已移除 PATH 项（%1）：%2").arg(entry.scope, entry.value));
                touchedScopes.insert(entry.scope);
            }
        }
    }

    for (const ScopedVariable &entry : m_config.environmentVariables) {
        QSettings settings(environmentRegistryPath(entry.scope), QSettings::NativeFormat);
        if (!settings.contains(entry.name))
            continue;
        settings.remove(entry.name);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            appendLog(QStringLiteral("环境变量清理失败（%1）：%2").arg(entry.scope, entry.name));
            success = false;
        } else {
            appendLog(QStringLiteral("已删除环境变量（%1）：%2").arg(entry.scope, entry.name));
            touchedScopes.insert(entry.scope);
        }
    }

    if (!touchedScopes.isEmpty()) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
            reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 3000, &result);
    }
    return success;
#else
    return m_config.pathEntries.isEmpty() && m_config.environmentVariables.isEmpty();
#endif
}

bool UninstallerWindow::launchCleanupHelper()
{
    const QString source = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("cleanup_helper.exe"));
    if (!QFileInfo::exists(source)) {
        appendLog(QStringLiteral("缺少 cleanup_helper.exe"));
        return false;
    }

    const QString tempRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("%1-uninstall-%2").arg(
            m_config.appId, QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(tempRoot))
        return false;

    const QString helper = QDir(tempRoot).filePath(QStringLiteral("cleanup_helper.exe"));
    if (!QFile::copy(source, helper))
        return false;

    const QStringList arguments = {
        QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("--install-dir"), m_installDirectory,
        QStringLiteral("--helper-dir"), tempRoot,
        QStringLiteral("--log"), m_logFilePath
    };
    return QProcess::startDetached(helper, arguments, tempRoot);
}

bool UninstallerWindow::isSafeUserPath(const QString &path) const
{
    if (!QDir::isAbsolutePath(path))
        return false;

    const QString home = QDir::homePath();
    if (!isInside(path, home))
        return false;

    const QString clean = normalizedPath(path);
    const QStringList forbidden = {
        normalizedPath(home),
        normalizedPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/../..")),
        normalizedPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
    };
    for (const QString &root : forbidden) {
        if (clean.compare(root, Qt::CaseInsensitive) == 0)
            return false;
    }
    return true;
}

bool UninstallerWindow::isSafeRegistryKey(const QString &key) const
{
    const QString normalized = key.trimmed();
    return normalized.startsWith(
               QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"),
               Qt::CaseInsensitive)
        || normalized.startsWith(
               QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"),
               Qt::CaseInsensitive);
}

void UninstallerWindow::setBusy(bool busy)
{
    m_uninstallButton->setDisabled(busy);
    m_cancelButton->setDisabled(busy);
    m_removeUserData->setDisabled(busy);
}
