#include "app.h"

#include <string.h>

/* libctru places this stack inside the heap. 32KB is the library default,
   and main's state alone is larger than that. */
u32 __stacksize__ = 0x40000;

static AppState app;

static void services_exit(int gpu) {
    if (gpu) {
        C2D_Fini();
        C3D_Fini();
    }
    httpcExit();
    amExit();
    sdmcExit();
    acExit();
    cfguExit();
    romfsExit();
    hidExit();
    gfxExit();
}

int main(void) {
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSet3D(false);
    hidInit();
    romfsInit();
    cfguInit();
    acInit();
    if (R_FAILED(sdmcInit())) {
        services_exit(0);
        return 1;
    }
    amInit();
    httpcInit(0x10000);
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        services_exit(0);
        return 1;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        services_exit(0);
        return 1;
    }
    C2D_Prepare();

    if (app_init(&app) != 0) {
        services_exit(1);
        return 1;
    }

    while (aptMainLoop()) {
        if (!app_frame(&app)) break;
    }

    app_shutdown(&app);
    services_exit(1);
    return 0;
}
