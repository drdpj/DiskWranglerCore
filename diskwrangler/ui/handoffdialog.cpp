/*
 * Detection questions and hand-off messages - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "handoffdialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/app/handoff.h>

namespace dw {
namespace ui {

// "a CP/M disc", "an Acorn disc": what the owner's discs are called in the message
static QString discWord(const fs::Candidate &c) {
    const QString n = c.name.isEmpty() ? c.owner.name : c.name;
    const QChar f = n.isEmpty() ? QChar('x') : n[0].toUpper();
    return QString(QString("AEIOU").contains(f) ? "an " : "a ") + n + " disc";
}

void showStarted(QWidget *parent, const fs::Candidate &c, const QString &imagePath, const QString &title) {
    const QString key = "handoff/quiet/" + c.owner.id;
    if (AppSettings().value(key, false).toBool()) return;
    QMessageBox box(QMessageBox::Information, title.isEmpty() ? "Opened in " + c.owner.name : title,
                    QString("This is %1, so it has been opened in %2.").arg(discWord(c), c.owner.name),
                    QMessageBox::Ok, parent);
    box.setInformativeText(QFileInfo(imagePath).fileName() + ": " + c.description);
    auto *quiet = new QCheckBox("Don't show this again");
    box.setCheckBox(quiet);
    box.exec();
    if (quiet->isChecked()) AppSettings().setValue(key, true);
}

bool handOff(QWidget *parent, const fs::Candidate &c, const QString &imagePath, const QString &title, const QString &text,
             bool interactive, bool allowStart) {
    QString why;
    const bool started = allowStart && !app::handedBack(imagePath) && app::startOtherApp(c.owner, imagePath, &why);
    if (!interactive) return started;
    if (started) {
        showStarted(parent, c, imagePath, title);
        return true;
    }
    if (title.isEmpty()) return false;
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(releasesLink(c.owner));
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPushButton *locate = addLocateButton(&box, c.owner);
    box.exec();
    return locateAndStart(parent, &box, locate, c, imagePath, LocateChooser(), title);
}

QString chooseOtherApp(QWidget *parent, const fs::OtherApp &app) {
#if defined(Q_OS_WIN)
    const QString filter = "Programs (*.exe);;All files (*)";
    const QString from = qEnvironmentVariable("ProgramFiles");
#elif defined(Q_OS_MACOS)
    const QString filter = "Applications (*.app);;All files (*)";
    const QString from = "/Applications";
#else
    const QString filter = "All files (*)";
    const QString from = QCoreApplication::applicationDirPath();
#endif
    return QFileDialog::getOpenFileName(parent, "Locate " + app.name, from, filter);
}

QPushButton *addLocateButton(QMessageBox *box, const fs::OtherApp &app) {
    if (app.isNull() || !app::findOtherApp(app).isEmpty()) return nullptr;
    QPushButton *b = box->addButton("&Locate...", QMessageBox::ActionRole);
    b->setToolTip("Show where " + app.name + " is on this computer; it is remembered");
    return b;
}

bool locateAndStart(QWidget *parent, QMessageBox *box, QPushButton *locate, const fs::Candidate &c, const QString &imagePath,
                    const LocateChooser &choose, const QString &startedTitle) {
    if (!locate || box->clickedButton() != locate) return false;
    const QString file = choose ? choose(parent, c.owner) : chooseOtherApp(parent, c.owner);
    if (file.isEmpty()) return false;
    QString why;
    if (!app::setOtherAppPath(c.owner, file, &why)) {
        QMessageBox::warning(parent, "Locate " + c.owner.name,
                             QString("%1 is not %2: %3.").arg(QDir::toNativeSeparators(file), c.owner.name, why));
        return false;
    }
    if (!app::startOtherApp(c.owner, imagePath, &why)) {
        QMessageBox::warning(parent, "Locate " + c.owner.name, why + ".");
        return false;
    }
    showStarted(parent, c, imagePath, startedTitle);
    return true;
}

QString releasesLink(const fs::OtherApp &app) {
    return QString("<a href=\"%1\">Get %2</a>").arg(app.releases.toHtmlEscaped(), app.name.toHtmlEscaped());
}

int askWhich(QWidget *parent, const QString &imagePath, const fs::Candidate &a, const fs::Candidate &b) {
    auto line = [](const fs::Candidate &c) {
        return "- " + c.description + (c.foreign() ? " (" + c.owner.name + " opens these)" : QString());
    };
    QMessageBox box(QMessageBox::Question, "Which kind of disc?",
                    QString("%1 could be either of these, and its contents don't say which:\n\n%2\n%3")
                        .arg(QFileInfo(imagePath).fileName(), line(a), line(b)),
                    QMessageBox::NoButton, parent);
    auto button = [](const fs::Candidate &c) {
        return c.foreign() ? "Open in " + c.owner.name : "Open as " + (c.name.isEmpty() ? c.fsId : c.name);
    };
    QPushButton *pa = box.addButton(button(a), QMessageBox::AcceptRole);
    QPushButton *pb = box.addButton(button(b), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(pa);
    box.exec();
    if (box.clickedButton() == pa) return 0;
    if (box.clickedButton() == pb) return 1;
    return -1;
}

} // namespace ui
} // namespace dw
