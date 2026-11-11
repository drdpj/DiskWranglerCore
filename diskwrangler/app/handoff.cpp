/*
 * Handing a disc to another program - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "handoff.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <diskwrangler/app/appsettings.h>

namespace dw {
namespace app {

static bool runnable(const QString &p) {
    QFileInfo fi(p);
#ifdef Q_OS_MACOS
    if (fi.isDir() && p.endsWith(".app")) return true;
#endif
    return fi.isFile() && fi.isExecutable();
}

#ifdef Q_OS_WIN
static const char *const Exe = ".exe";
#else
static const char *const Exe = "";
#endif

// The program in folder: <id>, <id>.exe, <id>.app, or the newest <id>-*.AppImage
static QString inFolder(const QString &folder, const fs::OtherApp &app) {
    if (folder.isEmpty()) return QString();
    const QDir d(folder);
    for (const QString &n : {app.id + Exe, app.id + ".app"})
        if (runnable(d.filePath(n))) return d.filePath(n);
#if defined(Q_OS_LINUX)
    const QFileInfoList ai = d.entryInfoList({app.id + "-*.AppImage"}, QDir::Files, QDir::Time);
    for (const QFileInfo &f : ai)
        if (f.isExecutable()) return f.absoluteFilePath();
#endif
    return QString();
}

#if defined(Q_OS_LINUX)
// Exec= of <id>.desktop where the desktop looks for it, first word, if it names a program that is there
static QString fromDesktopFile(const fs::OtherApp &app) {
    const QString df = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, app.id + ".desktop");
    QFile f(df);
    if (df.isEmpty() || !f.open(QIODevice::ReadOnly)) return QString();
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.startsWith("Exec=")) continue;
        const QString prog = line.mid(5).section(' ', 0, 0);
        const QString p = QFileInfo(prog).isAbsolute() ? prog : QStandardPaths::findExecutable(prog);
        if (!p.isEmpty() && runnable(p)) return p;
    }
    return QString();
}
#endif

QString findOtherApp(const fs::OtherApp &app) {
    if (app.isNull()) return QString();
    const QString set = AppSettings().value("otherApps/" + app.id).toString();
    if (!set.isEmpty() && runnable(set)) return set;
    QString p = inFolder(AppSettings::programFolder(), app);
    if (p.isEmpty()) p = inFolder(QCoreApplication::applicationDirPath(), app);
    if (!p.isEmpty()) return p;
#if defined(Q_OS_WIN)
    for (const char *root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        QSettings reg(QString("%1\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%2.exe").arg(root, app.id),
                      QSettings::NativeFormat);
        const QString v = QDir::fromNativeSeparators(reg.value(".").toString().remove('"'));
        if (!v.isEmpty() && runnable(v)) return v;
    }
    for (const QString &base : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("LOCALAPPDATA") + "/Programs"}) {
        if (base.isEmpty()) continue;
        const QString v = QDir::fromNativeSeparators(base) + "/" + app.folder + "/" + app.id + Exe;
        if (runnable(v)) return v;
    }
#elif defined(Q_OS_MACOS)
    for (const QString &base : {QString("/Applications"), QDir::homePath() + "/Applications"}) {
        const QString v = base + "/" + app.id + ".app";
        if (runnable(v)) return v;
    }
#elif defined(Q_OS_LINUX)
    p = fromDesktopFile(app);
    if (!p.isEmpty()) return p;
#endif
    return QStandardPaths::findExecutable(app.id);
}

bool startOtherApp(const fs::OtherApp &app, const QString &imagePath, QString *why) {
    QString prog = findOtherApp(app);
    if (prog.isEmpty()) { *why = app.name + " was not found on this computer"; return false; }
    const QString image = QFileInfo(imagePath).absoluteFilePath();
#ifdef Q_OS_MACOS
    if (prog.endsWith(".app")) prog += "/Contents/MacOS/" + app.id;       // the program itself, so it gets the environment
#endif
    QProcess p;
    p.setProgram(prog);
    p.setArguments({QDir::toNativeSeparators(image)});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(HandoffVariable, image);
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(QFileInfo(image).absolutePath());
    if (!p.startDetached()) { *why = app.name + " (" + QDir::toNativeSeparators(prog) + ") could not be started"; return false; }
    return true;
}

bool handedBack(const QString &imagePath) {
    const QString h = qEnvironmentVariable(HandoffVariable);
    if (h.isEmpty()) return false;
    const QFileInfo a(h), b(imagePath);
    return a.canonicalFilePath() == b.canonicalFilePath() || a.absoluteFilePath() == b.absoluteFilePath();
}

// The .app bundle a file is (or is inside, as <bundle>/Contents/MacOS/<program>); else file itself
static QString bundleOf(const QString &file) {
#ifdef Q_OS_MACOS
    const QFileInfo fi(file);
    const QDir macos = fi.absoluteDir();
    if (fi.isFile() && macos.dirName() == "MacOS") {
        QDir b = macos;
        if (b.cdUp() && b.dirName() == "Contents" && b.cdUp() && b.dirName().endsWith(".app")) return b.absolutePath();
    }
#endif
    return file;
}

bool isOtherApp(const QString &file, const fs::OtherApp &app, QString *why) {
    const QString f = bundleOf(file);
    const QFileInfo fi(f);
    if (app.isNull()) { *why = "no program to look for"; return false; }
    if (!fi.exists()) { *why = "there is no such file"; return false; }
    // the names inFolder() looks for, in any case
    const QString id = QRegularExpression::escape(app.id);
    const QRegularExpression names("^" + id + "(\\.exe|\\.app|-.*\\.appimage)?$", QRegularExpression::CaseInsensitiveOption);
    if (!names.match(fi.fileName()).hasMatch()) {
#if defined(Q_OS_WIN)
        *why = "its program file is called " + app.id + ".exe";
#elif defined(Q_OS_MACOS)
        *why = "it is called " + app.id + ".app";
#else
        *why = QString("its program file is called %1 (or %1-<version>.AppImage)").arg(app.id);
#endif
        return false;
    }
    if (!runnable(f)) { *why = "it is not a program that can be run"; return false; }
    return true;
}

bool setOtherAppPath(const fs::OtherApp &app, const QString &file, QString *why) {
    if (!isOtherApp(file, app, why)) return false;
    AppSettings().setValue("otherApps/" + app.id, QFileInfo(bundleOf(file)).absoluteFilePath());
    return true;
}

} // namespace app
} // namespace dw
