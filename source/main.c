#include <switch.h>
#include <stdbool.h>
#include <stdio.h>

#include "protocol/dlna_control.h"
// #include "protocol/airplay/discovery/mdns.h"

static bool initialize_network(void)
{
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc))
    {
        printf("[net] socketInitializeDefault() failed: 0x%08X\n", rc);
        printf("[net] Network features unavailable.\n");
        return false;
    }

    printf("[net] Network stack initialized.\n");
    printf("[net] Ensure Wi-Fi is connected before streaming.\n");
    return true;
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);

    bool networkReady = initialize_network();
    bool dlnaRunning = false;

    if (networkReady)
    {
        dlnaRunning = dlna_control_start();
        // mdns_discover_airplay();
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    printf("NX-Cast starting...\n");
    printf("Press + to exit.\n");

    while (appletMainLoop())
    {
        padUpdate(&pad);

        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus)
            break;

        consoleUpdate(NULL);
    }

    if (networkReady)
    {
        if (dlnaRunning)
            dlna_control_stop();
        socketExit();
    }

    consoleExit(NULL);
    return 0;
}
