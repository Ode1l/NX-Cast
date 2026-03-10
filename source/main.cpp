#include <switch.h>
#include <stdio.h>

namespace
{
    bool initializeNetwork()
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
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);

    bool networkReady = initializeNetwork();

    // Configure one standard controller layout.
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    // Initialize the default gamepad.
    PadState pad;
    padInitializeDefault(&pad);

    printf("NXCast starting...\n");
    printf("Press + to exit.\n");

    while (appletMainLoop())
    {
        // Update controller state once per frame.
        padUpdate(&pad);

        // Get newly pressed buttons this frame.
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus)
            break;

        consoleUpdate(NULL);
    }

    if (networkReady)
        socketExit();

    consoleExit(NULL);
    return 0;
}
