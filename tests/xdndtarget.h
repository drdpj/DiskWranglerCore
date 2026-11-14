/*
 * A strict XDND drop target for the drag-out tests - part of DiskWranglerCore, used by Acorn Disk Wrangler and
 * CP/M Disk Wrangler
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
#pragma once

// A drop target for the programs' xdragtest (series 6c-2), run in a process of its own on a real X server (Xvfb): a
// window that takes drops (XdndAware, version 5) and, written straight on xcb, asks for the data the way the targets
// that failed do. It accepts every position (copy) and, on the drop, with the drop's time as GTK uses it:
//   1. TARGETS, as mutter's XWayland bridge asks first (which hands an X11 drag to Wayland programs on GNOME);
//   2. application/vnd.portal.filetransfer, which nothing offers, as GTK 4 (Nautilus) asks first;
//   3. text/uri-list.
// Like GTK 4 and mutter it takes only a SelectionNotify whose target is the one it asked for (an answer with another
// target, such as None, is not an answer), and waits at most 2 seconds for each. Then it sends XdndFinished. Mode
// "silent" accepts the drop and asks for nothing. Each step is a line in the log file:
//   targets <names> | targets no answer
//   portal refused | portal no answer | portal data
//   uri-list <n> <file name>... | uri-list refused | uri-list no answer
// Linux / X11 only; link with -lxcb.

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <xcb/xcb.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace xdndtarget {

inline void logLine(const QString &file, const QString &s) {
    QFile l(file);
    if (l.open(QIODevice::Append)) l.write((s + "\n").toUtf8());
}

class Target {
public:
    Target(const QString &log, bool silent) : m_log(log), m_silent(silent) {}

    // A window at (x, y, w, h) on $DISPLAY; then events until the process is killed. Returns non-zero if it can't start.
    int run(int x, int y, int w, int h) {
        int screenNo = 0;
        c = xcb_connect(nullptr, &screenNo);
        if (!c || xcb_connection_has_error(c)) return 2;
        xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(c));
        for (int i = 0; i < screenNo; ++i) xcb_screen_next(&it);
        xcb_screen_t *screen = it.data;
        win = xcb_generate_id(c);
        const uint32_t values[] = {screen->white_pixel, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY};
        xcb_create_window(c, XCB_COPY_FROM_PARENT, win, screen->root, int16_t(x), int16_t(y), uint16_t(w), uint16_t(h), 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
        const uint32_t version = 5;
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, atom("XdndAware"), XCB_ATOM_ATOM, 32, 1, &version);
        const QByteArray title = "xdndtarget";
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, uint32_t(title.size()), title.constData());
        xcb_map_window(c, win);
        // no window manager: put it where asked
        const uint32_t pos[] = {uint32_t(x), uint32_t(y)};
        xcb_configure_window(c, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, pos);
        xcb_flush(c);
        for (;;) {
            xcb_generic_event_t *e = xcb_wait_for_event(c);
            if (!e) return 3;
            handle(e);
            free(e);
        }
    }

private:
    xcb_atom_t atom(const char *name) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, uint16_t(strlen(name)), name), nullptr);
        const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
        free(r);
        return a;
    }
    QByteArray name(xcb_atom_t a) {
        xcb_get_atom_name_reply_t *r = xcb_get_atom_name_reply(c, xcb_get_atom_name(c, a), nullptr);
        QByteArray n = r ? QByteArray(xcb_get_atom_name_name(r), xcb_get_atom_name_name_length(r)) : QByteArray();
        free(r);
        return n;
    }
    void send(xcb_window_t to, const char *type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4) {
        xcb_client_message_event_t m;
        memset(&m, 0, sizeof m);
        m.response_type = XCB_CLIENT_MESSAGE;
        m.format = 32;
        m.window = to;
        m.type = atom(type);
        m.data.data32[0] = win;
        m.data.data32[1] = d1;
        m.data.data32[2] = d2;
        m.data.data32[3] = d3;
        m.data.data32[4] = d4;
        xcb_send_event(c, 0, to, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&m));
        xcb_flush(c);
    }
    // Asks for the drag's data in this format; waits up to 2 s for a SelectionNotify for exactly this target.
    // 1: data (in *out), 0: refused, -1: no answer.
    int ask(const char *target, uint32_t time, QByteArray *out, xcb_atom_t *type = nullptr) {
        const xcb_atom_t t = atom(target), prop = atom("XDNDTARGET_DATA");
        xcb_convert_selection(c, win, atom("XdndSelection"), t, prop, time);
        xcb_flush(c);
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 2000) {
            xcb_generic_event_t *e = xcb_poll_for_event(c);
            if (!e) { usleep(5000); continue; }
            const bool notify = (e->response_type & 0x7f) == XCB_SELECTION_NOTIFY;
            const auto *n = reinterpret_cast<xcb_selection_notify_event_t *>(e);
            if (notify && n->requestor == win && n->target == t) {          // what GTK 4 and mutter look for
                const bool refused = n->property == XCB_ATOM_NONE;
                free(e);
                if (refused) return 0;
                xcb_get_property_reply_t *p = xcb_get_property_reply(c, xcb_get_property(c, 1, win, prop, XCB_GET_PROPERTY_TYPE_ANY, 0, 1 << 24), nullptr);
                if (p) {
                    *out = QByteArray(static_cast<const char *>(xcb_get_property_value(p)), xcb_get_property_value_length(p));
                    if (type) *type = p->type;
                }
                free(p);
                return 1;
            }
            if (!notify) handle(e);                // another drag event meanwhile
            free(e);                               // a SelectionNotify for another target: not an answer
        }
        return -1;
    }
    void dropped(xcb_window_t source, uint32_t time) {
        if (!m_silent) {
            QByteArray data;
            xcb_atom_t type = XCB_ATOM_NONE;
            const int t = ask("TARGETS", time, &data, &type);
            if (t == 1 && type == XCB_ATOM_ATOM) {
                QStringList names;
                const auto *atoms = reinterpret_cast<const xcb_atom_t *>(data.constData());
                for (int i = 0; i < int(data.size() / sizeof(xcb_atom_t)); ++i) names << QString::fromUtf8(name(atoms[i]));
                logLine(m_log, "targets " + names.join(' '));
            } else {
                logLine(m_log, t == -1 ? "targets no answer" : "targets refused");
            }
            const int p = ask("application/vnd.portal.filetransfer", time, &data);
            logLine(m_log, p == 0 ? "portal refused" : p == -1 ? "portal no answer" : "portal data");
            const int u = ask("text/uri-list", time, &data);
            if (u == 1) {
                QStringList files;
                for (const QByteArray &line : data.split('\n')) {
                    const QByteArray l = line.trimmed();
                    if (l.isEmpty() || l.startsWith('#')) continue;
                    const QString path = QUrl::fromEncoded(l).toLocalFile();
                    files << (QFile::exists(path) ? path.section('/', -1) : "missing:" + path.section('/', -1));
                }
                logLine(m_log, QString("uri-list %1 %2").arg(files.size()).arg(files.join(' ')));
            } else {
                logLine(m_log, u == 0 ? "uri-list refused" : "uri-list no answer");
            }
        } else {
            logLine(m_log, "dropped, asked for nothing");
        }
        send(source, "XdndFinished", 1, atom("XdndActionCopy"), 0, 0);
    }
    void handle(xcb_generic_event_t *e) {
        if ((e->response_type & 0x7f) != XCB_CLIENT_MESSAGE) return;
        const auto *m = reinterpret_cast<xcb_client_message_event_t *>(e);
        const QByteArray type = name(m->type);
        const xcb_window_t source = m->data.data32[0];
        if (type == "XdndPosition")                // accept, copy
            send(source, "XdndStatus", 1, 0, 0, atom("XdndActionCopy"));
        else if (type == "XdndDrop")
            dropped(source, m->data.data32[2]);
    }

    xcb_connection_t *c = nullptr;
    xcb_window_t win = 0;
    QString m_log;
    bool m_silent = false;
};

} // namespace xdndtarget
