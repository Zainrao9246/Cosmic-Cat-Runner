// Glue between Android's NativeActivity and the game's main().
// NativeActivity loads libmain.so and calls android_main(); raylib wants
// main() on its own thread while this thread pumps Android's event queue
// so the OS keeps feeding us lifecycle + touch events.
#include <pthread.h>
#include <android/native_activity.h>
#include <android/native_app_glue/android_native_app_glue.h>

extern int main(int argc, char* argv[]);   // defined in main.cpp (C++)

static void* run_game(void* arg)
{
    (void)arg;
    main(0, 0);                       // the game runs here forever
    return 0;
}

void android_main(struct android_app* app)
{
    pthread_t thread;
    pthread_create(&thread, 0, run_game, app);

    // pump the event queue forever (the app is killed by the system,
    // not by us)
    int events;
    struct android_poll_source* source;
    while (1)
    {
        while (ALooper_pollAll(-1, 0, &events, (void**)&source) >= 0)
            if (source != 0) source->process(app, source);
    }
}
