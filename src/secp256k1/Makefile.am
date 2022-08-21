ACLOCAL_AMFLAGS = -I build-aux/m4

lib_LTLIBRARIES = libsecp256k1.la
if USE_JNI
JNI_LIB = libsecp256k1_jni.la
noinst_LTLIBRARIES = $(JNI_LIB)
else
JNI_LIB =
endif
include_HEADERS = include/secp256k1.h
noinst_HEADERS =
noinst_HEADERS += src/scalar.h
noinst_HEADERS += src/scalar_4x64.h
noinst_HEADERS += src/scalar_8x32.h
noinst_HEADERS += src/scalar_low.h
noinst_HEADERS += src/scalar_impl.h
noinst_HEADERS += src/scalar_4x64_impl.h
noinst_HEADERS += src/scalar_8x32_impl.h
noinst_HEADERS += src/scalar_low_impl.h
noinst_HEADERS += src/group.h
noinst_HEADERS += src/group_impl.h
noinst_HEADERS += s