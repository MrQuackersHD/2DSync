#include "app.h"

#include <string.h>

int main(void) {
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSet3D(false);
    romfsInit();
    cfguInit();
    acInit();
    amInit();
    httpcInit(0x10000);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    AppState app;
    if (app_init(&app) != 0) {
        httpcExit();
        amExit();
        acExit();
        cfguExit();
        romfsExit();
        gfxExit();
        return 1;
    }

    while (aptMainLoop()) {
        if (!app_frame(&app)) break;
    }

    app_shutdown(&app);
    C2D_Fini();
    C3D_Fini();
    httpcExit();
    amExit();
    acExit();
    cfguExit();
    romfsExit();
    gfxExit();
    return 0;
}
