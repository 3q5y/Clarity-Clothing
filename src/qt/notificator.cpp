// Copyright (c) 2011-2013 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018-2019 The GIANT developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "notificator.h"

#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QImageWriter>
#include <QMessageBox>
#include <QMetaType>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTemporaryFile>
#include <QVariant>
#ifdef USE_DBUS
#include <QtDBus>
#include <stdint.h>
#endif
// Include ApplicationServices.h after QtDbus to avoid redefinition of check().
// This affects at least OSX 10.6. See /usr/include/AssertMacros.h for details.
// Note: This could also be worked around using:
// #define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#ifdef Q_OS_MAC
#include "macnotificationhandler.h"
#include <ApplicationServices/ApplicationServices.h>
#endif


#ifdef USE_DBUS
// https://wiki.ubuntu.com/NotificationDevelopmentGuidelines recommends at least 128
const int FREEDESKTOP_NOTIFICATION_ICON_SIZE = 128;
#endif

Notificator::Notificator(const QString& programName, QSystemTrayIcon* trayicon, QWidget* parent) : QObject(parent),
                                                                                                   parent(parent),
                                                                                                   programName(programName),
                                                                                                   mode(None),
                                                                                                   trayIcon(trayicon)
#ifdef USE_DBUS
                                                                                                   ,
                                                                                                   interface(0)
#endif
{
    if (trayicon && trayicon->supportsMessages()) {
        mode = QSystemTray;
    }
#ifdef USE_DBUS
    interface = new QDBusInterface("org.freedesktop.Notifications",
        "/org/freedesktop/Notifications", "org.freedesktop.Notifications");
    if (interface->isValid()) {
        mode = Freedesktop;
    }
#endif
#ifdef Q_OS_MAC
    // check if users OS has support for NSUserNotification
    if (MacNotificationHandler::instance()->hasUserNotificationCenterSupport()) {
        mode = UserNotificationCenter;
    }
#endif
}

Notificator::~Notificator()
{
#ifdef USE_DBUS
    delete interface;
#endif
}

#ifdef USE_DBUS

// Loosely based on http://www.qtcentre.org/archive/index.php/t-25879.html
class FreedesktopImage
{
public:
    FreedesktopImage() {}
    FreedesktopImage(const QImage& img);

    static int metaType();

    // Image to variant that can be marshalled over DBus
    static QVariant toVariant(const QImage& img);

private:
    int width, height, stride;
    bool hasAlpha;
    int channels;
    int bitsPerSample;
    QByteArray image;

    friend QDBusArgument& operator<<(QDBusArgument& a, const FreedesktopImage& i);
    friend const QDBusA