/*
 * Serving a drag's data on X11 - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include <diskwrangler/ui/xdndsource.h>

#include <QtGlobal>

#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN) && !defined(Q_OS_ANDROID)
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QLibrary>
#include <QPointer>
#include <QVector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace dw {
namespace ui {

namespace {

// ----- the X protocol as libxcb lays it out (xcb/xproto.h), so nothing is needed to build
struct XcbConnection;                                          // xcb_connection_t
struct Cookie { unsigned int sequence; };                      // xcb_*_cookie_t
struct InternAtomReply { uint8_t type, pad; uint16_t sequence; uint32_t length, atom; };
struct AtomNameReply;                                          // read through xcb_get_atom_name_name()
struct SelectionRequest {                                      // xcb_selection_request_event_t, 32 bytes
    uint8_t type, pad;
    uint16_t sequence;
    uint32_t time, owner, requestor, selection, target, property;
};
struct SelectionNotify {                                       // xcb_selection_notify_event_t, sent as 32 bytes
    uint8_t type, pad;
    uint16_t sequence;
    uint32_t time, requestor, selection, target, property;
    uint8_t unused[8];
};
static_assert(sizeof(SelectionRequest) == 28, "SelectionRequest");
static_assert(sizeof(SelectionNotify) == 32, "SelectionNotify");
const uint8_t SelectionRequestCode = 30, SelectionNotifyCode = 31;
const uint32_t AtomNone = 0, AtomAtom = 4;                     // predefined atoms

// The xcb calls used, from the libxcb already in the process (Qt's xcb plugin links it).
struct Xcb {
    XcbConnection *(*connect)(const char *, int *) = nullptr;
    int (*hasError)(XcbConnection *) = nullptr;
    Cookie (*internAtom)(XcbConnection *, uint8_t, uint16_t, const char *) = nullptr;
    InternAtomReply *(*internAtomReply)(XcbConnection *, Cookie, void **) = nullptr;
    Cookie (*getAtomName)(XcbConnection *, uint32_t) = nullptr;
    AtomNameReply *(*getAtomNameReply)(XcbConnection *, Cookie, void **) = nullptr;
    char *(*atomNameName)(const AtomNameReply *) = nullptr;
    int (*atomNameLength)(const AtomNameReply *) = nullptr;
    Cookie (*changeProperty)(XcbConnection *, uint8_t, uint32_t, uint32_t, uint32_t, uint8_t, uint32_t, const void *) = nullptr;
    Cookie (*sendEvent)(XcbConnection *, uint8_t, uint32_t, uint32_t, const char *) = nullptr;
    int (*flush)(XcbConnection *) = nullptr;

    bool load() {
        QLibrary lib(QStringLiteral("xcb"), 1);                 // libxcb.so.1: loaded already, so this only finds it
        if (!lib.load()) return false;
        auto get = [&lib](auto &fn, const char *name) { fn = reinterpret_cast<std::decay_t<decltype(fn)>>(lib.resolve(name)); return fn != nullptr; };
        return get(connect, "xcb_connect") && get(hasError, "xcb_connection_has_error") && get(internAtom, "xcb_intern_atom") &&
               get(internAtomReply, "xcb_intern_atom_reply") && get(getAtomName, "xcb_get_atom_name") &&
               get(getAtomNameReply, "xcb_get_atom_name_reply") && get(atomNameName, "xcb_get_atom_name_name") &&
               get(atomNameLength, "xcb_get_atom_name_name_length") && get(changeProperty, "xcb_change_property") &&
               get(sendEvent, "xcb_send_event") && get(flush, "xcb_flush");
    }
};

class Server : public QAbstractNativeEventFilter {
public:
    QPointer<QMimeData> data;                    // the drag being served (the QDrag owns it)

    bool start() {
        if (m_c) return true;
        if (!m_xcb.load()) return false;
        XcbConnection *c = m_xcb.connect(nullptr, nullptr);          // $DISPLAY, as Qt's
        if (!c) return false;
        if (m_xcb.hasError(c)) return false;      // (a failed connection is an error object xcb keeps; not freed)
        m_c = c;
        m_selection = atom("XdndSelection");
        m_targets = atom("TARGETS");
        if (!m_selection || !m_targets) { m_c = nullptr; return false; }
        return true;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override {
#else
    bool nativeEventFilter(const QByteArray &type, void *message, long *) override {
#endif
        if (!data || !m_c || type != "xcb_generic_event_t") return false;
        const auto *e = static_cast<const SelectionRequest *>(message);
        if ((e->type & 0x7f) != SelectionRequestCode || e->selection != m_selection) return false;
        answer(*e);
        return true;                              // Qt's own answer (target None, or no TARGETS) would only confuse
    }

private:
    uint32_t atom(const QByteArray &name) {
        if (m_atoms.contains(name)) return m_atoms.value(name);
        InternAtomReply *r = m_xcb.internAtomReply(m_c, m_xcb.internAtom(m_c, 0, uint16_t(name.size()), name.constData()), nullptr);
        const uint32_t a = r ? r->atom : AtomNone;
        free(r);
        if (a) { m_atoms.insert(name, a); m_names.insert(a, name); }
        return a;
    }
    QByteArray name(uint32_t a) {
        if (m_names.contains(a)) return m_names.value(a);
        AtomNameReply *r = m_xcb.getAtomNameReply(m_c, m_xcb.getAtomName(m_c, a), nullptr);
        const QByteArray n = r ? QByteArray(m_xcb.atomNameName(r), m_xcb.atomNameLength(r)) : QByteArray();
        free(r);
        if (!n.isEmpty()) { m_names.insert(a, n); m_atoms.insert(n, a); }
        return n;
    }

    // The formats this drag is served in: what it offers, and text/x-moz-url beside text/uri-list (Qt lists both).
    QList<QByteArray> offered() const {
        QList<QByteArray> f;
        for (const QString &s : data->formats()) f << s.toUtf8();
        if (f.contains("text/uri-list")) f << "text/x-moz-url";
        return f;
    }

    void answer(const SelectionRequest &e) {
        const QByteArray target = name(e.target);
        const uint32_t property = e.property ? e.property : e.target;      // an obsolete client names no property
        uint32_t type = e.target;
        uint8_t format = 8;
        QByteArray bytes;
        bool ok = true;
        if (target == "TARGETS") {
            QVector<uint32_t> atoms{m_targets};
            for (const QByteArray &f : offered()) if (const uint32_t a = atom(f)) atoms << a;
            bytes = QByteArray(reinterpret_cast<const char *>(atoms.constData()), int(atoms.size() * sizeof(uint32_t)));
            type = AtomAtom;
            format = 32;
        } else if (target == "text/x-moz-url" && data->hasFormat(QStringLiteral("text/uri-list"))) {
            // as Qt writes it (qxcbmime.cpp): the first URL and a newline, in UTF-16
            const QString first = QString::fromUtf8(data->data(QStringLiteral("text/uri-list")).split('\n').value(0).trimmed()) + QLatin1Char('\n');
            bytes = QByteArray(reinterpret_cast<const char *>(first.utf16()), int(first.size() * 2));
        } else if (!target.isEmpty() && offered().contains(target)) {
            bytes = data->data(QString::fromUtf8(target));           // a DragOutMime writes its files here, if not yet
        } else {
            ok = false;                                               // not offered: refused, as X expects
        }
        if (ok)
            m_xcb.changeProperty(m_c, 0 /* Replace */, e.requestor, property, type, format,
                                 uint32_t(bytes.size() / (format / 8)), bytes.constData());
        SelectionNotify n;
        std::memset(&n, 0, sizeof n);
        n.type = SelectionNotifyCode;
        n.time = e.time;
        n.requestor = e.requestor;
        n.selection = e.selection;
        n.target = e.target;                      // the target asked for, also when refused (what GTK 4 and mutter look for)
        n.property = ok ? property : AtomNone;
        m_xcb.sendEvent(m_c, 0, e.requestor, 0, reinterpret_cast<const char *>(&n));
        m_xcb.flush(m_c);
    }

    Xcb m_xcb;
    XcbConnection *m_c = nullptr;
    uint32_t m_selection = AtomNone, m_targets = AtomNone;
    QHash<QByteArray, uint32_t> m_atoms;
    QHash<uint32_t, QByteArray> m_names;
};

Server *server() {
    static Server *s = nullptr;
    if (!s) {
        s = new Server;                           // for the life of the program, as the event filter must be
        if (s->start()) qApp->installNativeEventFilter(s);
    }
    return s;
}

} // namespace

void serveXdnd(QMimeData *data) {
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;
    server()->data = data;
}

} // namespace ui
} // namespace dw

#else

namespace dw {
namespace ui {
void serveXdnd(QMimeData *) {}
} // namespace ui
} // namespace dw

#endif
