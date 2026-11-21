#!/usr/bin/env python3
"""Safety-net patches for the Cosmic Cat Runner Android build.

Run by .github/workflows/build-apk.yml before gradle. Every patch is
idempotent: it checks first and only applies if needed, so it works
whether or not the repo files have already been fixed. All patches
always run (no early exits).

  1. Android.mk / main_android.c — raylib's rcore_android.c (compiled with
     -DPLATFORM_ANDROID) already defines android_main(); a second
     definition in main_android.c would be a duplicate symbol at link time.
  2. Application.mk — AGP's ndk-build parser cannot strip inline '#'
     comments ("Unable to parse APP_STL" warnings otherwise).
  3. AndroidManifest.xml — targetSdk 34 (Android 12+) requires an explicit
     android:exported on any activity that has an intent filter.
  4. main.cpp — load APK assets through raylib's AAssetManager (relative
     "assets/..." paths); FileExists()/GetApplicationDirectory() cannot see
     inside an APK on Android.
  5. Android.mk — raylib 5.0 renamed its sources (core.c -> rcore.c etc.);
     an Android.mk still listing the old names fails with "No rule to make
     target .../raylib/src/core.c".
  6. Android.mk — raylib 5.5 REMOVED its bundled native_app_glue (the
     folder src/external/android/native_app_glue no longer exists in the
     5.5 tag), so a glue block pointing there fails with "No rule to make
     target .../android_native_app_glue.c". The glue ships with the NDK at
     sources/android/native_app_glue; this patch rewrites Android.mk to the
     canonical layout that pulls the glue in via
     $(call import-module,android/native_app_glue).
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MK = 'android/app/src/main/jni/Android.mk'

# Canonical Android.mk (used by patch 6). Matches raylib 5.5 + NDK r26:
# raylib sources live in jni/raylib/src (clone target), the platform backend
# (platforms/rcore_android.c, which defines android_main()) is pulled in by
# rcore.c when PLATFORM_ANDROID is defined, and the native app glue comes
# from the NDK itself via import-module.
CANONICAL_MK = """\
# Builds the game for Android (driven by gradle's ndk-build).
#
#   1. native app glue — comes from the NDK itself (import-module at the
#      bottom of this file). raylib 5.5 removed its bundled copy from
#      src/external/android/native_app_glue.
#   2. raylib 5.5 — static library from the sources the workflow cloned
#      into jni/raylib. PLATFORM_ANDROID makes rcore.c pull in
#      platforms/rcore_android.c, which defines android_main() and runs
#      the game on its own thread (the old main_android.c glue was
#      removed — keeping it would duplicate android_main at link time).
#   3. the game — main.cpp (the same single file the desktop build uses),
#      linked into libmain.so which NativeActivity loads.
LOCAL_PATH := $(call my-dir)
RAYLIB_PATH := $(LOCAL_PATH)/raylib

# ---- raylib static library ----
# NOTE: raylib 5.0+ renamed its sources (core.c -> rcore.c etc.); these are
# the 5.5 names. rcore.c includes platforms/rcore_android.c when
# PLATFORM_ANDROID is defined, and that file includes
# <android_native_app_glue.h>, so the glue (via its exported include dir)
# is listed as a dependency below.
include $(CLEAR_VARS)
LOCAL_MODULE := raylib
LOCAL_C_INCLUDES := $(RAYLIB_PATH)/src $(RAYLIB_PATH)
LOCAL_SRC_FILES := \\
    $(RAYLIB_PATH)/src/rcore.c \\
    $(RAYLIB_PATH)/src/rshapes.c \\
    $(RAYLIB_PATH)/src/rtextures.c \\
    $(RAYLIB_PATH)/src/rtext.c \\
    $(RAYLIB_PATH)/src/utils.c \\
    $(RAYLIB_PATH)/src/rmodels.c \\
    $(RAYLIB_PATH)/src/raudio.c
LOCAL_CFLAGS := -O2 -Wall -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2
LOCAL_STATIC_LIBRARIES := android_native_app_glue
include $(BUILD_STATIC_LIBRARY)

# ---- the game ----
include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_SRC_FILES := $(LOCAL_PATH)/main.cpp
LOCAL_C_INCLUDES := $(RAYLIB_PATH)/src $(LOCAL_PATH)
LOCAL_STATIC_LIBRARIES := raylib android_native_app_glue
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv2 -lOpenSLES
LOCAL_CFLAGS := -O2 -Wall
include $(BUILD_SHARED_LIBRARY)

# The native app glue ships with the NDK at sources/android/native_app_glue.
# import-module builds it and exports its include dir to every module above
# that lists it in LOCAL_STATIC_LIBRARIES. Must stay at the end of the file.
 $(call import-module,android/native_app_glue)
"""


def rel(path):
    return os.path.join(ROOT, path)


def read(path):
    with io.open(rel(path), encoding='utf-8') as f:
        return f.read()


def write(path, s):
    with io.open(rel(path), 'w', encoding='utf-8') as f:
        f.write(s)
    print('  -> patched ' + path)


# 1. stale Android glue file -----------------------------------------------
print('[1/6] android_main glue')
glue = rel('android/app/src/main/jni/main_android.c')
if os.path.exists(glue):
    os.remove(glue)
    print('  -> removed android/app/src/main/jni/main_android.c')
else:
    print('  -> already absent')
if os.path.exists(rel(MK)):
    s = read(MK)
    if '$(LOCAL_PATH)/main_android.c' in s:
        write(MK, s.replace('$(LOCAL_PATH)/main_android.c ', ''))
    else:
        print('  -> Android.mk already clean')

# 2. Application.mk inline comments ----------------------------------------
print('[2/6] Application.mk inline comments')
mk_app = 'android/app/src/main/jni/Application.mk'
s = read(mk_app)
fixed = re.sub(r'(?m)(:=\s*\S+)[ \t]+#.*$', r'\1', s)
if fixed != s:
    write(mk_app, fixed)
else:
    print('  -> already clean')

# 3. Manifest android:exported ----------------------------------------------
print('[3/6] AndroidManifest.xml android:exported')
man = 'android/app/src/main/AndroidManifest.xml'
s = read(man)
if 'android:exported' in s:
    print('  -> already present')
else:
    s = s.replace(
        'android:name="android.app.NativeActivity"',
        'android:name="android.app.NativeActivity"\n'
        '            android:exported="true"')
    write(man, s)

# 4. main.cpp Android asset loading -----------------------------------------
print('[4/6] main.cpp asset loading')
main = 'android/app/src/main/jni/main.cpp'
if not os.path.exists(rel(main)):
    print('  !! android/app/src/main/jni/main.cpp missing (workflow copies it '
          'from src/main.cpp before this script runs) — skipping')
else:
    s = read(main)
    if 'AAssetManager' in s:
        print('  -> already patched')
    else:
        old = (
            '    const char* base = GetApplicationDirectory();\n'
            '    snprintf(path, sizeof(path), "%sassets/%s", base ? base : "", file);\n'
            '    if (FileExists(path)) return LoadTexture(path);\n'
            '    Texture2D empty = {};\n'
            '    return empty;'
        )
        new = (
            '#ifdef __ANDROID__\n'
            "    // On Android the assets live inside the APK. raylib's LoadFileData()\n"
            '    // reads them through the AAssetManager using paths relative to the\n'
            "    // APK 'assets/' folder. FileExists() uses access(), which cannot see\n"
            '    // inside an APK, and GetApplicationDirectory() is meaningless here.\n'
            '    snprintf(path, sizeof(path), "assets/%s", file);\n'
            '    return LoadTexture(path);\n'
            '#else\n'
            '    const char* base = GetApplicationDirectory();\n'
            '    snprintf(path, sizeof(path), "%sassets/%s", base ? base : "", file);\n'
            '    if (FileExists(path)) return LoadTexture(path);\n'
            '    Texture2D empty = {};\n'
            '    return empty;\n'
            '#endif'
        )
        if old in s:
            write(main, s.replace(old, new))
        else:
            print('  !! LoadAssetTex pattern not found — leaving main.cpp unchanged')

# 5. raylib 5.x source names in Android.mk ----------------------------------
print('[5/6] Android.mk raylib source names')
if os.path.exists(rel(MK)):
    s = read(MK)
    renames = {
        '/src/core.c': '/src/rcore.c',
        '/src/shapes.c': '/src/rshapes.c',
        '/src/textures.c': '/src/rtextures.c',
        '/src/text.c': '/src/rtext.c',
        '/src/models.c': '/src/rmodels.c',
    }
    changed = False
    for old_name, new_name in renames.items():
        if old_name in s:
            s = s.replace(old_name, new_name)
            changed = True
    if changed:
        write(MK, s)
    else:
        print('  -> already using raylib 5.x names')

# 6. native_app_glue source (raylib 5.5 removed its bundled copy) ------------
print('[6/6] Android.mk native app glue from the NDK')
if os.path.exists(rel(MK)):
    s = read(MK)
    if 'import-module,android/native_app_glue' in s:
        print('  -> already using the NDK glue (import-module)')
    elif 'native_app_glue' in s:
        write(MK, CANONICAL_MK)
    else:
        print('  !! no native_app_glue reference found — leaving Android.mk unchanged')
else:
    print('  !! Android.mk missing — leaving it alone')
