#!/usr/bin/env python3
"""Safety-net patches for the Cosmic Cat Runner Android build.

Run by .github/workflows/build-apk.yml before gradle. Every patch is
idempotent: it checks first and only applies if needed, so it works
whether or not the repo files have already been fixed.

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
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


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
print('[1/5] android_main glue')
glue = rel('android/app/src/main/jni/main_android.c')
if os.path.exists(glue):
    os.remove(glue)
    print('  -> removed android/app/src/main/jni/main_android.c')
else:
    print('  -> already absent')
mk = 'android/app/src/main/jni/Android.mk'
if os.path.exists(rel(mk)):
    s = read(mk)
    if '$(LOCAL_PATH)/main_android.c' in s:
        write(mk, s.replace('$(LOCAL_PATH)/main_android.c ', ''))
    else:
        print('  -> Android.mk already clean')

# 2. Application.mk inline comments ----------------------------------------
print('[2/5] Application.mk inline comments')
mk_app = 'android/app/src/main/jni/Application.mk'
s = read(mk_app)
fixed = re.sub(r'(?m)(:=\s*\S+)[ \t]+#.*$', r'\1', s)
if fixed != s:
    write(mk_app, fixed)
else:
    print('  -> already clean')

# 3. Manifest android:exported ----------------------------------------------
print('[3/5] AndroidManifest.xml android:exported')
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
print('[4/5] main.cpp asset loading')
main = 'android/app/src/main/jni/main.cpp'
s = read(main)
if 'AAssetManager' in s:
    print('  -> already patched')
    raise SystemExit(0)

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
print('[5/5] Android.mk raylib source names')
if os.path.exists(rel(mk)):
    s = read(mk)
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
        write(mk, s)
    else:
        print('  -> already using raylib 5.x names')
