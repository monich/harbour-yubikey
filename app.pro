NAME = yubikey
PREFIX = harbour

TARGET = $${PREFIX}-$${NAME}
CONFIG += sailfishapp link_pkgconfig
PKGCONFIG += sailfishapp mlite5 glib-2.0 gobject-2.0 gio-unix-2.0
QT += qml quick dbus multimedia concurrent

DEFINES += NFCDC_NEED_PEER_SERVICE=0
QMAKE_CXXFLAGS += -Wno-unused-parameter
QMAKE_CFLAGS += -Wno-unused-parameter
LIBS += -ldl

TARGET_DATA_DIR = /usr/share/$${TARGET}

app_settings {
    # This path is hardcoded in jolla-settings
    TRANSLATIONS_PATH = /usr/share/translations
} else {
    TRANSLATIONS_PATH = $${TARGET_DATA_DIR}/translations
}

CONFIG(debug, debug|release) {
    DEFINES += DEBUG HARBOUR_DEBUG
} else {
    QMAKE_CXXFLAGS += -flto -fPIC
    QMAKE_CFLAGS += -flto -fPIC
    QMAKE_LFLAGS += -flto -fPIC
}

equals(QT_ARCH, arm64){
    message(Linking with OpenSSL)
    PKGCONFIG += libcrypto
}

# Directories

HARBOUR_LIB_DIR = $${_PRO_FILE_PWD_}/harbour-lib
LIBGLIBUTIL_DIR = $${_PRO_FILE_PWD_}/libglibutil
LIBGNFCDC_DIR = $${_PRO_FILE_PWD_}/libgnfcdc
LIBQNFCDC_DIR = $${_PRO_FILE_PWD_}/libqnfcdc
FOIL_DIR = $${_PRO_FILE_PWD_}/foil
ZBAR_DIR = $${_PRO_FILE_PWD_}/zbar/zbar

# Files

OTHER_FILES += \
    harbour.yubikey.conf \
    _harbour-yubikey.conf \
    *.desktop \
    qml/*.qml \
    qml/images/*.svg \
    qml/components/*.js \
    qml/components/*.qml \
    icons/*.svg \
    translations/*.ts

# App

INCLUDEPATH += \
    src

HEADERS += \
    src/QrCodeDecoder.h \
    src/QrCodeScanner.h \
    src/YubiKey.h \
    src/YubiKeyAuth.h \
    src/YubiKeyAuthListModel.h \
    src/YubiKeyCard.h \
    src/YubiKeyDefs.h \
    src/YubiKeyConstants.h \
    src/YubiKeyRecognizer.h \
    src/YubiKeySettings.h \
    src/YubiKeyTag.h \
    src/YubiKeyTypes.h \
    src/YubiKeyUtil.h

SOURCES += \
    src/main.cpp \
    src/QrCodeDecoder.cpp \
    src/QrCodeScanner.cpp \
    src/YubiKey.cpp \
    src/YubiKeyAuth.cpp \
    src/YubiKeyAuthListModel.cpp \
    src/YubiKeyCard.cpp \
    src/YubiKeyRecognizer.cpp \
    src/YubiKeyTag.cpp \
    src/YubiKeySettings.cpp \
    src/YubiKeyUtil.cpp

# libfoil

LIBFOIL_DIR = $${FOIL_DIR}/libfoil
LIBFOIL_INCLUDE = $${LIBFOIL_DIR}/include
LIBFOIL_SRC = $${LIBFOIL_DIR}/src
LIBFOIL_OPENSSL_SRC = $${LIBFOIL_SRC}/openssl

INCLUDEPATH += \
    $${LIBFOIL_INCLUDE} \
    $${LIBFOIL_SRC}

HEADERS += \
    $${LIBFOIL_INCLUDE}/*.h

SOURCES += \
    $${LIBFOIL_SRC}/foil_digest.c \
    $${LIBFOIL_SRC}/foil_digest_sha1.c \
    $${LIBFOIL_SRC}/foil_digest_sha256.c \
    $${LIBFOIL_SRC}/foil_digest_sha512.c \
    $${LIBFOIL_SRC}/foil_hmac.c \
    $${LIBFOIL_SRC}/foil_input.c \
    $${LIBFOIL_SRC}/foil_input_mem.c \
    $${LIBFOIL_SRC}/foil_input_base64.c \
    $${LIBFOIL_SRC}/foil_kdf.c \
    $${LIBFOIL_SRC}/foil_output.c \
    $${LIBFOIL_SRC}/foil_random.c \
    $${LIBFOIL_SRC}/foil_util.c \
    $${LIBFOIL_OPENSSL_SRC}/foil_openssl_digest_sha1.c \
    $${LIBFOIL_OPENSSL_SRC}/foil_openssl_digest_sha256.c \
    $${LIBFOIL_OPENSSL_SRC}/foil_openssl_digest_sha512.c \
    $${LIBFOIL_OPENSSL_SRC}/foil_openssl_random.c

# libglibutil

LIBGLIBUTIL_SRC = $${LIBGLIBUTIL_DIR}/src
LIBGLIBUTIL_INCLUDE = $${LIBGLIBUTIL_DIR}/include

INCLUDEPATH += \
    $${LIBGLIBUTIL_INCLUDE}

HEADERS += \
    $${LIBGLIBUTIL_INCLUDE}/*.h

SOURCES += \
    $${LIBGLIBUTIL_SRC}/gutil_log.c \
    $${LIBGLIBUTIL_SRC}/gutil_misc.c \
    $${LIBGLIBUTIL_SRC}/gutil_strv.c \
    $${LIBGLIBUTIL_SRC}/gutil_timenotify.c

# libgnfcdc

LIBGNFCDC_INCLUDE = $${LIBGNFCDC_DIR}/include
LIBGNFCDC_SRC = $${LIBGNFCDC_DIR}/src
LIBGNFCDC_SPEC = $${LIBGNFCDC_DIR}/spec

INCLUDEPATH += \
    $${LIBGNFCDC_INCLUDE}

HEADERS += \
    $${LIBGNFCDC_INCLUDE}/*.h \
    $${LIBGNFCDC_SRC}/*.h

SOURCES += \
    $${LIBGNFCDC_SRC}/nfcdc_adapter.c \
    $${LIBGNFCDC_SRC}/nfcdc_base.c \
    $${LIBGNFCDC_SRC}/nfcdc_daemon.c \
    $${LIBGNFCDC_SRC}/nfcdc_default_adapter.c \
    $${LIBGNFCDC_SRC}/nfcdc_error.c \
    $${LIBGNFCDC_SRC}/nfcdc_isodep.c \
    $${LIBGNFCDC_SRC}/nfcdc_log.c \
    $${LIBGNFCDC_SRC}/nfcdc_tag.c

OTHER_FILES += \
    $${LIBGNFCDC_SPEC}/*.xml

defineTest(generateStub) {
    xml = $${LIBGNFCDC_SPEC}/org.sailfishos.nfc.$${1}.xml
    cmd = gdbus-codegen --generate-c-code org.sailfishos.nfc.$${1} $${xml}

    gen_h = org.sailfishos.nfc.$${1}.h
    gen_c = org.sailfishos.nfc.$${1}.c
    target_h = org_sailfishos_nfc_$${1}_h
    target_c = org_sailfishos_nfc_$${1}_c

    $${target_h}.target = $${gen_h}
    $${target_h}.depends = $${xml}
    $${target_h}.commands = $${cmd}
    export($${target_h}.target)
    export($${target_h}.depends)
    export($${target_h}.commands)

    GENERATED_HEADERS += $${gen_h}
    PRE_TARGETDEPS += $${gen_h}
    QMAKE_EXTRA_TARGETS += $${target_h}

    $${target_c}.target = $${gen_c}
    $${target_c}.depends = $${target_h}
    export($${target_c}.target)
    export($${target_c}.depends)

    GENERATED_SOURCES += $${gen_c}
    QMAKE_EXTRA_TARGETS += $${target_c}
    PRE_TARGETDEPS += $${gen_c}

    export(QMAKE_EXTRA_TARGETS)
    export(GENERATED_SOURCES)
    export(PRE_TARGETDEPS)
}

generateStub(Adapter)
generateStub(Daemon)
generateStub(IsoDep)
generateStub(Settings)
generateStub(Tag)

# libqnfcdc

LIBQNFCDC_INCLUDE = $${LIBQNFCDC_DIR}/include
LIBQNFCDC_SRC = $${LIBQNFCDC_DIR}/src

INCLUDEPATH += \
    $${LIBQNFCDC_INCLUDE}

HEADERS += \
    $${LIBQNFCDC_INCLUDE}/NfcAdapter.h \
    $${LIBQNFCDC_INCLUDE}/NfcMode.h \
    $${LIBQNFCDC_INCLUDE}/NfcSystem.h \
    $${LIBQNFCDC_INCLUDE}/NfcTag.h

SOURCES += \
    $${LIBQNFCDC_SRC}/NfcAdapter.cpp \
    $${LIBQNFCDC_SRC}/NfcMode.cpp \
    $${LIBQNFCDC_SRC}/NfcSystem.cpp \
    $${LIBQNFCDC_SRC}/NfcTag.cpp

# harbour-lib

HARBOUR_LIB_INCLUDE = $${HARBOUR_LIB_DIR}/include
HARBOUR_LIB_SRC = $${HARBOUR_LIB_DIR}/src
HARBOUR_LIB_QML = $${HARBOUR_LIB_DIR}/qml

INCLUDEPATH += \
    $${HARBOUR_LIB_INCLUDE}

HEADERS += \
    $${HARBOUR_LIB_INCLUDE}/HarbourBase32.h \
    $${HARBOUR_LIB_INCLUDE}/HarbourDebug.h \
    $${HARBOUR_LIB_INCLUDE}/HarbourSingleImageProvider.h \
    $${HARBOUR_LIB_INCLUDE}/HarbourSystemInfo.h

SOURCES += \
    $${HARBOUR_LIB_SRC}/HarbourBase32.cpp \
    $${HARBOUR_LIB_SRC}/HarbourSingleImageProvider.cpp \
    $${HARBOUR_LIB_SRC}/HarbourSystemInfo.cpp

HARBOUR_QML_COMPONENTS = \
    $${HARBOUR_LIB_QML}/HarbourFitLabel.qml \
    $${HARBOUR_LIB_QML}/HarbourHighlightIcon.qml \
    $${HARBOUR_LIB_QML}/HarbourPasswordInputField.qml \
    $${HARBOUR_LIB_QML}/HarbourShakeAnimation.qml \
    $${HARBOUR_LIB_QML}/HarbourTextFlip.qml

OTHER_FILES += $${HARBOUR_QML_COMPONENTS}

qml_components.files = $${HARBOUR_QML_COMPONENTS}
qml_components.path = $${TARGET_DATA_DIR}/qml/harbour
INSTALLS += qml_components

# openssl

!equals(QT_ARCH, arm64){
SOURCES += \
    $${HARBOUR_LIB_SRC}/libcrypto.c
}

# zbar

INCLUDEPATH += \
    $${ZBAR_DIR}/include \
    $${ZBAR_DIR}/zbar

LIBS += zbar/libzbar.a

# Config files

dbus_config.files = $${_PRO_FILE_PWD_}/harbour.yubikey.conf
dbus_config.path = /etc/dbus-1/system.d
INSTALLS += dbus_config

nfcd_config.files = $${_PRO_FILE_PWD_}/_harbour-yubikey.conf
nfcd_config.path = /etc/nfcd/ndef-handlers
INSTALLS += nfcd_config

# Icons

ICON_SIZES = 86 108 128 172 256
for(s, ICON_SIZES) {
    icon_target = icon_$${s}
    icon_dir = icons/$${s}x$${s}
    $${icon_target}.files = $${icon_dir}/$${TARGET}.png
    $${icon_target}.path = /usr/share/icons/hicolor/$${s}x$${s}/apps
    INSTALLS += $${icon_target}
}

# Translations

TRANSLATION_IDBASED=-idbased
TRANSLATION_SOURCES = \
  $${_PRO_FILE_PWD_}/qml

defineTest(addTrFile) {
    rel = translations/$${1}
    OTHER_FILES += $${rel}.ts
    export(OTHER_FILES)

    in = $${_PRO_FILE_PWD_}/$$rel
    out = $${OUT_PWD}/$$rel

    s = $$replace(1,-,_)
    lupdate_target = lupdate_$$s
    qm_target = qm_$$s

    $${lupdate_target}.commands = lupdate -noobsolete -locations none $${TRANSLATION_SOURCES} -ts \"$${in}.ts\" && \
        mkdir -p \"$${OUT_PWD}/translations\" &&  [ \"$${in}.ts\" != \"$${out}.ts\" ] && \
        cp -af \"$${in}.ts\" \"$${out}.ts\" || :

    $${qm_target}.path = $$TRANSLATIONS_PATH
    $${qm_target}.depends = $${lupdate_target}
    $${qm_target}.commands = lrelease $$TRANSLATION_IDBASED \"$${out}.ts\" && \
        $(INSTALL_FILE) \"$${out}.qm\" $(INSTALL_ROOT)$${TRANSLATIONS_PATH}/

    QMAKE_EXTRA_TARGETS += $${lupdate_target} $${qm_target}
    INSTALLS += $${qm_target}

    export($${lupdate_target}.commands)
    export($${qm_target}.path)
    export($${qm_target}.depends)
    export($${qm_target}.commands)
    export(QMAKE_EXTRA_TARGETS)
    export(INSTALLS)
}

LANGUAGES = sv

addTrFile($${TARGET})
for(l, LANGUAGES) {
    addTrFile($${TARGET}-$$l)
}

qm.path = $$TRANSLATIONS_PATH
qm.CONFIG += no_check_exist
INSTALLS += qm
