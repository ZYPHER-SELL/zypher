#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <shlwapi.h>
#include "ioctl.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

#define SERVICE_NAME L"ZypherSpoofer"
#define DRIVER_PATH  L"ZypherSpoofer.sys"

static SC_HANDLE g_scm = NULL;
static SC_HANDLE g_service = NULL;
static HANDLE g_device = INVALID_HANDLE_VALUE;

static bool InstallDriver() {
    WCHAR driver_path[MAX_PATH];
    GetFullPathNameW(DRIVER_PATH, MAX_PATH, driver_path, NULL);

    g_scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!g_scm) {
        printf("[-] Failed to open SCManager (run as admin)\n");
        return false;
    }

    g_service = CreateServiceW(g_scm, SERVICE_NAME, L"Zypher HWID Spoofer",
        SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL, driver_path, NULL, NULL, NULL, NULL, NULL);

    if (!g_service) {
        if (GetLastError() == ERROR_SERVICE_EXISTS) {
            g_service = OpenServiceW(g_scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
        }
        if (!g_service) {
            printf("[-] Failed to create/open service: %lu\n", GetLastError());
            return false;
        }
    }

    if (!StartServiceW(g_service, 0, NULL)) {
        if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            printf("[-] Failed to start service: %lu\n", GetLastError());
            return false;
        }
    }

    g_device = CreateFileW(SPOOFER_USER_NAME, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (g_device == INVALID_HANDLE_VALUE) {
        printf("[-] Failed to open device: %lu\n", GetLastError());
        return false;
    }

    return true;
}

static void UninstallDriver() {
    if (g_device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
    }

    if (g_service) {
        SERVICE_STATUS status;
        ControlService(g_service, SERVICE_CONTROL_STOP, &status);
        DeleteService(g_service);
        CloseServiceHandle(g_service);
        g_service = NULL;
    }

    if (g_scm) {
        CloseServiceHandle(g_scm);
        g_scm = NULL;
    }
}

static bool SpoofAll() {
    if (g_device == INVALID_HANDLE_VALUE) return false;

    SPOOF_REQUEST req = { 0 };
    req.seed = (ULONG64)time(NULL) ^ GetCurrentProcessId();
    req.spoof_volume = TRUE;
    req.spoof_disk = TRUE;
    req.spoof_registry = TRUE;
    req.spoof_smbios = TRUE;
    req.spoof_mac = TRUE;
    req.spoof_gpu = TRUE;
    req.spoof_monitor = TRUE;
    req.spoof_usb = TRUE;
    req.spoof_wmi = TRUE;
    req.spoof_cpu = TRUE;
    req.spoof_nvme = TRUE;

    DWORD bytes_returned = 0;
    bool ok = DeviceIoControl(g_device, IOCTL_SPOOFER_SPOOF_ALL,
        &req, sizeof(req), NULL, 0, &bytes_returned, NULL);

    if (ok) {
        printf("[+] All HWID vectors spoofed (seed: %llu)\n", req.seed);
    } else {
        printf("[-] Spoof failed: %lu\n", GetLastError());
    }

    return ok;
}

static bool SpoofSelective() {
    if (g_device == INVALID_HANDLE_VALUE) return false;

    SPOOF_REQUEST req = { 0 };
    req.seed = (ULONG64)time(NULL) ^ GetCurrentProcessId();

    printf("Select vectors to spoof:\n");
    printf("  [1] Volume serials    [2] Disk serials\n");
    printf("  [3] Registry          [4] SMBIOS\n");
    printf("  [5] MAC addresses     [6] GPU\n");
    printf("  [7] Monitor EDID      [8] USB\n");
    printf("  [9] WMI               [A] NVMe\n");
    printf("  [0] Cancel\n");
    printf("Enter choices (e.g. 135A): ");

    char choices[32];
    fgets(choices, sizeof(choices), stdin);

    for (char* p = choices; *p; p++) {
        switch (*p) {
            case '1': req.spoof_volume = TRUE; break;
            case '2': req.spoof_disk = TRUE; break;
            case '3': req.spoof_registry = TRUE; break;
            case '4': req.spoof_smbios = TRUE; break;
            case '5': req.spoof_mac = TRUE; break;
            case '6': req.spoof_gpu = TRUE; break;
            case '7': req.spoof_monitor = TRUE; break;
            case '8': req.spoof_usb = TRUE; break;
            case '9': req.spoof_wmi = TRUE; break;
            case 'A': case 'a': req.spoof_nvme = TRUE; break;
            case '0': return false;
        }
    }

    DWORD bytes_returned = 0;
    bool ok = DeviceIoControl(g_device, IOCTL_SPOOFER_SPOOF_ALL,
        &req, sizeof(req), NULL, 0, &bytes_returned, NULL);

    if (ok) {
        printf("[+] Selected vectors spoofed (seed: %llu)\n", req.seed);
    } else {
        printf("[-] Spoof failed: %lu\n", GetLastError());
    }

    return ok;
}

static bool RestoreAll() {
    if (g_device == INVALID_HANDLE_VALUE) return false;

    DWORD bytes_returned = 0;
    bool ok = DeviceIoControl(g_device, IOCTL_SPOOFER_RESTORE_ALL,
        NULL, 0, NULL, 0, &bytes_returned, NULL);

    if (ok) {
        printf("[+] All HWID values restored\n");
    } else {
        printf("[-] Restore failed: %lu\n", GetLastError());
    }

    return ok;
}

static void GetStatus() {
    if (g_device == INVALID_HANDLE_VALUE) return;

    SPOOFER_STATUS status;
    DWORD bytes_returned = 0;

    if (DeviceIoControl(g_device, IOCTL_SPOOFER_GET_STATUS,
        NULL, 0, &status, sizeof(status), &bytes_returned, NULL))
    {
        printf("  Volume:   %s\n", status.volume_spoofed ? "SPOOFED" : "original");
        printf("  Disk:     %s (%d hooked)\n",
            status.disk_spoofed ? "SPOOFED" : "original", status.disks_hooked);
        printf("  Registry: %s\n", status.registry_spoofed ? "SPOOFED" : "original");
        printf("  SMBIOS:   %s\n", status.smbios_spoofed ? "SPOOFED" : "original");
        printf("  MAC:      %s (%d adapters)\n",
            status.mac_spoofed ? "SPOOFED" : "original", status.adapters_spoofed);
        printf("  GPU:      %s\n", status.gpu_spoofed ? "SPOOFED" : "original");
        printf("  Monitor:  %s\n", status.monitor_spoofed ? "SPOOFED" : "original");
        printf("  USB:      %s\n", status.usb_spoofed ? "SPOOFED" : "original");
        printf("  WMI:      %s\n", status.wmi_spoofed ? "SPOOFED" : "original");
        printf("  CPU:      %s\n", status.cpu_spoofed ? "SPOOFED" : "original");
        printf("  NVMe:     %s\n", status.nvme_spoofed ? "SPOOFED" : "original");
        printf("  Disk hook: %s\n", status.disk_hooked ? "ACTIVE" : "inactive");
        printf("  NDIS hook: %s\n", status.ndis_hooked ? "ACTIVE" : "inactive");
        printf("  Seed:     %llu\n", status.seed);
    }
}

static void CleanBootArtifacts() {
    printf("[*] Cleaning boot artifacts...\n");

    const char* event_logs[] = {
        "wevtutil cl Application",
        "wevtutil cl Security",
        "wevtutil cl System",
        "wevtutil cl \"Microsoft-Windows-Windows Defender/Operational\"",
        "wevtutil cl \"Microsoft-Windows-Sysmon/Operational\"",
    };

    for (size_t i = 0; i < sizeof(event_logs) / sizeof(event_logs[0]); i++) {
        system(event_logs[i]);
    }

    system("del /q /f C:\\Windows\\Prefetch\\*");
    system("del /q /f C:\\Users\\%USERNAME%\\AppData\\Roaming\\Microsoft\\Windows\\Recent\\*");
    system("del /q /f C:\\Users\\%USERNAME%\\AppData\\Local\\Temp\\*");
    system("ipconfig /flushdns");

    system("fsutil usn deletejournal /D /N C:");

    printf("[+] Boot artifacts cleaned\n");
}

static void PrintUsage() {
    printf("Zypher HWID Spoofer - Top-Tier Anti-Cheat Bypass\n");
    printf("Covers: EAC, BattlEye, Vanguard, Steam, FaceIT, Ricochet\n");
    printf("\n");
    printf("Usage: zypher-spoofer [command]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  spoof     - Spoof all HWID vectors\n");
    printf("  selective - Choose which vectors to spoof\n");
    printf("  restore   - Restore all original values\n");
    printf("  status    - Show current spoof status\n");
    printf("  clean     - Clean boot artifacts (logs, prefetch, USN journal)\n");
    printf("  unload    - Unload driver and restore\n");
    printf("\n");
    printf("Vectors spoofed:\n");
    printf("  - NTFS Volume serial numbers\n");
    printf("  - Disk serial numbers (IRP hook - blocks SMART queries)\n");
    printf("  - Registry: MachineGuid, ProductId, InstallDate,\n");
    printf("    ComputerName, RegisteredOwner/Org, DigitalProductId,\n");
    printf("    BuildGUID, MachineId, HwProfileGuid, InstallID\n");
    printf("  - SMBIOS: BIOS vendor/version/date, system serial,\n");
    printf("    baseboard serial/manufacturer, chassis serial\n");
    printf("  - MAC addresses (all network adapters)\n");
    printf("  - GPU: driver date/version/UUID (safe - no DriverDesc)\n");
    printf("  - Monitor EDID serials (REG_BINARY)\n");
    printf("  - USB device serials\n");
    printf("  - WMI provider spoofing\n");
    printf("  - NVMe controller serials\n");
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    if (_wcsicmp(argv[1], L"spoof") == 0) {
        if (InstallDriver()) {
            SpoofAll();
            printf("\n[*] HWID is spoofed. Driver stays loaded.\n");
            printf("[*] Run 'zypher-spoofer restore' when done.\n");
        }
    }
    else if (_wcsicmp(argv[1], L"selective") == 0) {
        if (InstallDriver()) {
            SpoofSelective();
            printf("\n[*] Press any key to restore and unload...\n");
            getchar();
            RestoreAll();
            UninstallDriver();
        }
    }
    else if (_wcsicmp(argv[1], L"restore") == 0) {
        if (InstallDriver()) {
            RestoreAll();
            UninstallDriver();
        }
    }
    else if (_wcsicmp(argv[1], L"status") == 0) {
        if (InstallDriver()) {
            GetStatus();
            UninstallDriver();
        }
    }
    else if (_wcsicmp(argv[1], L"clean") == 0) {
        CleanBootArtifacts();
    }
    else if (_wcsicmp(argv[1], L"unload") == 0) {
        UninstallDriver();
    }
    else {
        PrintUsage();
        return 1;
    }

    return 0;
}
