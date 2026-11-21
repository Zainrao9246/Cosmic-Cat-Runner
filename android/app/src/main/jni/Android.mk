# Builds the game for Android (driven by gradle's ndk-build).
#
#   1. android_native_app_glue — NDK helper that gives raylib the activity
#      lifecycle and touch/input events (raylib bundles it, and its Android
#      backend expects it)
#   2. raylib — built as a static library from the sources the workflow
#     cloned into jni/raylib (version 5.5). No GLFW on Android: raylib uses
#     its own EGL/GLES2 backend (PLATFORM_ANDROID, OpenGL ES 2).
#   3. the game — main.cpp (the same single file the desktop build uses),
#      linked into libmain.so which NativeActivity loads. NOTE: raylib's
#      rcore_android.c (compiled with -DPLATFORM_ANDROID) already defines
#      android_main() and calls the user's main() on its own thread, so the
#      old main_android.c glue was removed — keeping it would be a duplicate
#      symbol at link time.
LOCAL_PATH := $(call my-dir)
RAYLIB_PATH := $(LOCAL_PATH)/raylib
GLUE_PATH   := $(RAYLIB_PATH)/src/external/android/native_app_glue

# ---- native app glue (activity lifecycle + input plumbing) ----
include $(CLEAR_VARS)
LOCAL_MODULE := android_native_app_glue
LOCAL_SRC_FILES := $(GLUE_PATH)/android_native_app_glue.c
LOCAL_EXPORT_C_INCLUDES := $(GLUE_PATH)
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_STATIC_LIBRARY)

# ---- raylib static library ----
include $(CLEAR_VARS)
LOCAL_MODULE := raylib
LOCAL_C_INCLUDES := $(RAYLIB_PATH)/src $(RAYLIB_PATH) $(GLUE_PATH)
LOCAL_SRC_FILES := \
    $(RAYLIB_PATH)/src/core.c \
    $(RAYLIB_PATH)/src/shapes.c \
    $(RAYLIB_PATH)/src/textures.c \
    $(RAYLIB_PATH)/src/text.c \
    $(RAYLIB_PATH)/src/utils.c \
    $(RAYLIB_PATH)/src/models.c \
    $(RAYLIB_PATH)/src/raudio.c
LOCAL_CFLAGS := -O2 -Wall -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2
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
