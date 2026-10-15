#include <switch.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PROBE_REVISION
#define PROBE_REVISION "unknown"
#endif

#define REPORT_PATH "sdmc:/switch/NX-Cast/miracast-probe.log"

static FILE *report_file;
static bool report_failed;

static void report(const char *format, ...)
{
    va_list args;
    va_list file_args;
    va_start(args, format);
    va_copy(file_args, args);
    vprintf(format, args);
    if (report_file)
    {
        bool failed = vfprintf(report_file, format, file_args) < 0;
        if (fflush(report_file) != 0)
            failed = true;
        if (failed)
        {
            fclose(report_file);
            report_file = NULL;
            report_failed = true;
        }
    }
    va_end(file_args);
    va_end(args);
    consoleUpdate(NULL);
}

static bool ensure_directory(const char *path)
{
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

/* Atmosphere SM extension: TIPC 65100, eight-byte name in, flag byte out.
 * See README.md for the pinned interface reference. No upstream code is bundled.
 * A query failure is UNKNOWN, never treated as an absent service. */
static Result query_registered(const char *name, bool *registered)
{
    SmServiceName encoded = smEncodeName(name);
    u8 flags = 0;
    Result result = tipcDispatchInOut(smGetServiceSessionTipc(), 65100,
                                      encoded, flags);
    if (R_SUCCEEDED(result))
        *registered = (flags & 1u) != 0;
    return result;
}

static void probe_service(const char *name, bool inspect_wireless_child)
{
    bool registered = false;
    Result result = query_registered(name, &registered);
    if (R_FAILED(result))
    {
        report("%-8s registry=UNKNOWN rc=0x%08lX\n", name,
               (unsigned long)result);
        return;
    }
    if (!registered)
    {
        report("%-8s registry=ABSENT access=SKIPPED\n", name);
        return;
    }

    /* Flush before IPC so the last attempted operation survives a failed run.
     * The registry check avoids deliberate waits for nonexistent services;
     * it cannot eliminate service-removal races or bound kernel IPC latency. */
    report("%-8s registry=PRESENT opening...\n", name);
    Service service = {0};
    result = smGetService(&service, name);
    if (R_FAILED(result))
    {
        report("         access=FAILED rc=0x%08lX\n", (unsigned long)result);
        return;
    }
    report("         access=OPEN\n");

    if (inspect_wireless_child)
    {
        Service wireless = {0};
        report("         creating wireless service (cmd 0)...\n");
        result = serviceDispatch(&service, 0,
                                  .out_num_objects = 1,
                                  .out_objects = &wireless);
        report("         child=%s rc=0x%08lX\n",
               R_SUCCEEDED(result) ? "OPEN" : "FAILED",
               (unsigned long)result);
        if (R_SUCCEEDED(result))
            serviceClose(&wireless);
    }
    serviceClose(&service);
}

static void run_probe(void)
{
    consoleClear();
    if (ensure_directory("sdmc:/switch") &&
        ensure_directory("sdmc:/switch/NX-Cast"))
        report_file = fopen(REPORT_PATH, "w");
    report_failed = report_file == NULL;

    const u32 version = hosversionGet();
    report("NX-Cast Miracast M0 probe (NOT a receiver)\n");
    report("source=%s HOS=%u.%u.%u Atmosphere=%s\n", PROBE_REVISION,
           HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version),
           hosversionIsAtmosphere() ? "yes" : "no");

    if (!hosversionIsAtmosphere())
    {
        report("SKIPPED: Atmosphere registry extension is required.\n");
    }
    else if (version == 0)
    {
        report("SKIPPED: HOS version unknown.\n");
    }
    else
    {
        probe_service("nifm:u", false);
        probe_service("ldn:u", false);
        if (hosversionAtLeast(15, 0, 0))
        {
            probe_service("wlan", true);
            probe_service("wlan:nd", false);
            probe_service("wlan:p", false);
        }
        else
        {
            probe_service("wlan:inf", false);
            probe_service("wlan:lcl", false);
            probe_service("wlan:lga", false);
        }
    }

    report("\nWFD advertise / P2P group / sender connection: UNTESTED\n");
    report("Service access alone does NOT establish Miracast support.\n");
    if (report_file)
    {
        if (fclose(report_file) != 0)
            report_failed = true;
        report_file = NULL;
    }
    printf("\n%s\n", report_failed ? "SD report failed; use the screen output."
                                        : "Saved: " REPORT_PATH);
    printf("+ Exit\n");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    printf("NX-Cast Miracast M0 platform probe\n\n");
    printf("A: Query registry, open service handles, close them.\n");
    printf("No radio configuration, discovery or media playback.\n");
    printf("The previous miracast-probe.log is replaced on each run.\n");
    printf("\n+ Exit\n");
    bool ran = false;
    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus)
            break;
        if (!ran && (pressed & HidNpadButton_A))
        {
            ran = true;
            run_probe();
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
