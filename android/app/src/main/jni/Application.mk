# Toolchain settings for ndk-build.
# NOTE: no inline comments on setting lines — AGP's ndk-build parser
# cannot strip them ("Unable to parse APP_STL" warnings otherwise).
APP_STL := c++_static
APP_ABI := arm64-v8a armeabi-v7a
APP_PLATFORM := android-21
