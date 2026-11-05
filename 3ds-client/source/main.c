#include "app.h"

#include <string.h>

/* libctru places this stack inside the heap. 32KB is the library default,
   and main's state alone is larger than that. */
u32 __stacksize__ = 0x40000;

static AppState app;

int main(void) {
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSet3D(false);
    hidInit();
    romfsInit();
    cfguInit();
    acInit();
    amInit();
    httpcInit(0x10000);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    if (app_init(&app) != 0) {
        httpcExit();
        amExit();
        acExit();
        cfguExit();
        romfsExit();
        hidExit();
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
    hidExit();
    gfxExit();
    return 0;
}
