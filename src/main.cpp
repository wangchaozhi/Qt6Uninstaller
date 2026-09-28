#include "uninstallconfig.h"
#include "uninstallerwindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
void writeValidationReport(const QString &appId, const QString &message)
{
    const QString safeId = appId.isEmpty() ? QStringLiteral("Qt6Uninstaller") : appId;
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("%1-validation.log").arg(safeId));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << message << u'\n';
    }
}

#ifdef Q_OS_WIN
bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation,
        sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

QString quoteWindowsArgument(const QString &argument)
{
    QString escaped = argument;
    escaped.replace(u'"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

bool relaunchElevated(const QStringList &arguments)
{
    QStringList quoted;
    for (qsizetype i = 1; i < arguments.size(); ++i)
        quoted.append(quoteWindowsArgument(arguments.at(i)));
    const QString parameters = quoted.join(u' ');
    const HINSTANCE result = ShellExecuteW(nullptr, L"runas",
        reinterpret_cast<LPCWSTR>(QCoreApplication::applicationFilePath().utf16()),
        reinterpret_cast<LPCWSTR>(parameters.utf16()),
        reinterpret_cast<LPCWSTR>(QCoreApplication::applicationDirPath().utf16()), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}
#endif
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Qt6 Uninstaller"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.3.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Example Company"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt 6 Windows 卸载器"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption silentOption(QStringLiteral("silent"), QStringLiteral("静默卸载，不显示确认对话框。"));
    const QCommandLineOption removeDataOption(QStringLiteral("remove-user-data"), QStringLiteral("同时删除用户配置和缓存。"));
    const QCommandLineOption keepDataOption(QStringLiteral("keep-user-data"), QStringLiteral("保留用户配置和缓存。"));
    const QCommandLineOption validateOption(QStringLiteral("validate-config"),
        QStringLiteral("验证配置和部署文件，不执行卸载。"));
    parser.addOption(silentOption);
    parser.addOption(removeDataOption);
    parser.addOption(keepDataOption);
    parser.addOption(validateOption);
    parser.process(application);

    const bool silent = parser.isSet(silentOption);

    if (parser.isSet(removeDataOption) && parser.isSet(keepDataOption)) {
        const QString message = QStringLiteral("--remove-user-data 与 --keep-user-data 不能同时使用。");
        writeValidationReport(QString(), message);
        if (!silent)
            QMessageBox::critical(nullptr, QStringLiteral("参数错误"), message);
        return 64;
    }

    const QString configPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("uninstall-config.json"));
    QString error;
    const UninstallConfig config = UninstallConfig::load(configPath, &error);
    if (!error.isEmpty()) {
        writeValidationReport(QString(), error);
        if (!silent)
            QMessageBox::critical(nullptr, QStringLiteral("无法启动卸载器"), error);
        return 1;
    }

    if (parser.isSet(validateOption)) {
        const QString installRoot = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(config.installRootRelative));
        QStringList problems;
        if (!QFileInfo::exists(QDir(installRoot).filePath(QStringLiteral("uninstaller.marker"))))
            problems.append(QStringLiteral("安装根目录缺少 uninstaller.marker。"));
        if (!QFileInfo::exists(QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("cleanup_helper.exe"))))
            problems.append(QStringLiteral("卸载器目录缺少 cleanup_helper.exe。"));

        const QString message = problems.isEmpty()
            ? QStringLiteral("配置与部署文件验证通过。")
            : problems.join(u'\n');
        writeValidationReport(config.appId, message);
        if (!silent) {
            const auto icon = problems.isEmpty() ? QMessageBox::Information : QMessageBox::Critical;
            QMessageBox box(icon, QStringLiteral("配置验证"), message, QMessageBox::Ok);
            box.exec();
        }
        return problems.isEmpty() ? 0 : 3;
    }

#ifdef Q_OS_WIN
    if (config.requiresAdmin && !isElevated()) {
        if (relaunchElevated(QCoreApplication::arguments()))
            return 0;
        if (!parser.isSet(silentOption)) {
            QMessageBox::critical(nullptr, QStringLiteral("需要管理员权限"),
                QStringLiteral("卸载需要管理员权限，但提权请求未成功。"));
        }
        return 5;
    }
#endif

    std::optional<bool> removeDataOverride;
    if (parser.isSet(removeDataOption))
        removeDataOverride = true;
    else if (parser.isSet(keepDataOption))
        removeDataOverride = false;

    UninstallerWindow window(config, silent, removeDataOverride);
    if (silent)
        QTimer::singleShot(0, &window, &UninstallerWindow::startUninstall);
    else
        window.show();
    return application.exec();
}
