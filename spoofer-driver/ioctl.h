#pragma once

#define SPOOFER_DEVICE_NAME L"\\Device\\ZypherSpoofer"
#define SPOOFER_DOS_NAME    L"\\DosDevices\\ZypherSpoofer"
#define SPOOFER_USER_NAME   L"\\\\.\\ZypherSpoofer"

#define IOCTL_SPOOFER_BASE 0x800

#define IOCTL_SPOOFER_SPOOF_ALL \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_SPOOFER_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SPOOFER_RESTORE_ALL \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_SPOOFER_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SPOOFER_SET_SEED \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_SPOOFER_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SPOOFER_GET_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_SPOOFER_BASE + 4, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SPOOFER_CLEAN_BOOT \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_SPOOFER_BASE + 5, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _SPOOFER_STATUS {
    BOOLEAN volume_spoofed;
    BOOLEAN disk_spoofed;
    BOOLEAN registry_spoofed;
    BOOLEAN smbios_spoofed;
    BOOLEAN mac_spoofed;
    BOOLEAN gpu_spoofed;
    BOOLEAN monitor_spoofed;
    BOOLEAN usb_spoofed;
    BOOLEAN wmi_spoofed;
    BOOLEAN disk_hooked;
    BOOLEAN ndis_hooked;
    BOOLEAN cpu_spoofed;
    BOOLEAN nvme_spoofed;
    ULONG64 seed;
    ULONG adapters_spoofed;
    ULONG disks_hooked;
} SPOOFER_STATUS, *PSPOOFER_STATUS;

typedef struct _SPOOF_REQUEST {
    ULONG64 seed;
    BOOLEAN spoof_volume;
    BOOLEAN spoof_disk;
    BOOLEAN spoof_registry;
    BOOLEAN spoof_smbios;
    BOOLEAN spoof_mac;
    BOOLEAN spoof_gpu;
    BOOLEAN spoof_monitor;
    BOOLEAN spoof_usb;
    BOOLEAN spoof_wmi;
    BOOLEAN spoof_cpu;
    BOOLEAN spoof_nvme;
    BOOLEAN clean_boot;
} SPOOF_REQUEST, *PSPOOF_REQUEST;
