
#
# Common configuration options
#
#

QT += dbus
QT -= gui
CONFIG += debug
CONFIG += thread

# Maemo specific options
#CONFIG += contextprovider

# DEPRECATED: CompassChain is now provided by separate package
#             sensorfw-plugins-compasslogic
### Note: compass now works for x86 and armel both. debian/rules takes care
### of figuring out which platform binary to pick. If building manually, the
### default is armel. If internal sensors are included in manual build for x86,
### uncomment the following line:
#### CONFIG+= x86build

profile {
  QMAKE_CXXFLAGS += -pg
  QMAKE_LFLAGS += -pg
}

profile-libc {
  QMAKE_LFLAGS += -lc_p
}

equals(QT_MAJOR_VERSION, 5):{
    TARGET = $$TARGET-qt5
}

equals(QT_MAJOR_VERSION, 6):{
    TARGET = $$TARGET-qt5
}


OTHER_FILES += \
    ../../common.pri

contains(CONFIG,hybris)|contains(CONFIG,autohybris) {
    CONFIG += link_pkgconfig
    contains(CONFIG,binder) {
        DEFINES += USE_BINDER=1
        PKGCONFIG += libgbinder libglibutil gobject-2.0 glib-2.0
    } else {
        PKGCONFIG += android-headers libhardware
    }
}

