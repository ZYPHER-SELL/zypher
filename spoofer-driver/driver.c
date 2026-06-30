#include <ntddk.h>
#include <wdf.h>
#include <ndis.h>
#include <mountdev.h>
#include <mountmgr.h>
#include <storport.h>
#include "ioctl.h"

DRIVER_INITIALIZE DriverEntry;

#define MAX_HOOKED_DISKS 16
#define MAX_HOOKED_NICS 16

typedef struct _DISK_HOOK_ENTRY {
    PDEVICE_OBJECT device_object;
    PDRIVER_DISPATCH original_dispatch;
    BOOLEAN hooked;
    wchar_t spoofed_serial[32];
    wchar_t spoofed_model[64];
} DISK_HOOK_ENTRY;

typedef struct _NIC_HOOK_ENTRY {
    PDEVICE_OBJECT device_object;
    PDRIVER_DISPATCH original_dispatch;
    BOOLEAN hooked;
    BYTE spoofed_mac[6];
} NIC_HOOK_ENTRY;

typedef struct _DEVICE_CONTEXT {
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
    ULONG64 random_seed;
    ULONG adapters_spoofed;
    ULONG disks_hooked;

    DISK_HOOK_ENTRY disk_hooks[MAX_HOOKED_DISKS];
    NIC_HOOK_ENTRY nic_hooks[MAX_HOOKED_NICS];

    wchar_t original_machine_guid[64];
    wchar_t original_product_id[64];
    wchar_t original_computer_name[64];
    wchar_t original_install_date[64];
    wchar_t original_product_name[64];
    wchar_t original_registered_owner[64];
    wchar_t original_registered_org[64];
    wchar_t original_digital_product_id[256];
    wchar_t original_build_guid[64];
    wchar_t original_machine_id[64];
    wchar_t original_cpu_id[64];
    wchar_t original_install_id[64];
    wchar_t original_nvme_serial[64];
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

static const wchar_t* DISK_DEVICE_PATHS[] = {
    L"\\Device\\Harddisk0\\DR0",
    L"\\Device\\Harddisk1\\DR1",
    L"\\Device\\Harddisk2\\DR2",
    L"\\Device\\Harddisk3\\DR3",
    NULL
};

static const wchar_t* VOLUME_DEVICE_PATHS[] = {
    L"\\Device\\HarddiskVolume1",
    L"\\Device\\HarddiskVolume2",
    L"\\Device\\HarddiskVolume3",
    L"\\Device\\HarddiskVolume4",
    L"\\Device\\HarddiskVolume5",
    NULL
};

static const wchar_t* GPU_REGISTRY_PATHS[] = {
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0001",
    NULL
};

static const wchar_t* NVME_REGISTRY_PATHS[] = {
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e96b-e325-11ce-bfc1-08002be10318}",
    NULL
};

static ULONG64 SimpleRand(ULONG64* seed) {
    *seed = (*seed * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    return *seed;
}

static void GenerateRandomHex(wchar_t* out, int len, ULONG64* seed) {
    static const wchar_t hex[] = L"0123456789ABCDEF";
    for (int i = 0; i < len; i++)
        out[i] = hex[SimpleRand(seed) & 0xF];
    out[len] = L'\0';
}

static void GenerateRandomAlphanumeric(wchar_t* out, int len, ULONG64* seed) {
    static const wchar_t chars[] = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < len; i++)
        out[i] = chars[SimpleRand(seed) % 36];
    out[len] = L'\0';
}

static void GenerateRandomGuid(wchar_t* out, ULONG64* seed) {
    out[0] = L'{';
    GenerateRandomHex(out + 1, 8, seed);
    out[9] = L'-';
    GenerateRandomHex(out + 10, 4, seed);
    out[14] = L'-';
    GenerateRandomHex(out + 15, 4, seed);
    out[19] = L'-';
    GenerateRandomHex(out + 20, 4, seed);
    out[24] = L'-';
    GenerateRandomHex(out + 25, 12, seed);
    out[37] = L'}';
    out[38] = L'\0';
}

static void GenerateRandomSerial(wchar_t* out, int len, ULONG64* seed) {
    GenerateRandomAlphanumeric(out, len, seed);
}

static void GenerateRandomMac(BYTE* mac, ULONG64* seed) {
    for (int i = 0; i < 6; i++)
        mac[i] = (BYTE)(SimpleRand(seed) & 0xFF);
    mac[0] = (mac[0] & 0xFE) | 0x02;
}

static void GenerateRandomVolumeSerial(ULONG* serial, ULONG64* seed) {
    *serial = (ULONG)(SimpleRand(seed) & 0xFFFFFFFF);
}

static NTSTATUS ReadRegistryString(const wchar_t* path, const wchar_t* value_name,
    wchar_t* out, size_t out_size)
{
    UNICODE_STRING key_path, val_name;
    OBJECT_ATTRIBUTES oa;
    HANDLE key_handle;
    NTSTATUS status;

    RtlInitUnicodeString(&key_path, path);
    InitializeObjectAttributes(&oa, &key_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&key_handle, KEY_READ, &oa);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&val_name, value_name);

    ULONG result_len = 0;
    status = ZwQueryValueKey(key_handle, &val_name, KeyValuePartialInformation,
        NULL, 0, &result_len);

    if (result_len > 0 && result_len < 4096) {
        PKEY_VALUE_PARTIAL_INFORMATION info =
            (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, result_len, 'Zyph');
        if (info) {
            status = ZwQueryValueKey(key_handle, &val_name,
                KeyValuePartialInformation, info, result_len, &result_len);
            if (NT_SUCCESS(status) && info->Type == REG_SZ && info->DataLength > 0) {
                size_t copy_len = min(info->DataLength, out_size * sizeof(wchar_t) - sizeof(wchar_t));
                RtlCopyMemory(out, info->Data, copy_len);
                out[copy_len / sizeof(wchar_t)] = L'\0';
            }
            ExFreePoolWithTag(info, 'Zyph');
        }
    }

    ZwClose(key_handle);
    return status;
}

static NTSTATUS WriteRegistryString(const wchar_t* path, const wchar_t* value_name,
    const wchar_t* value)
{
    UNICODE_STRING key_path, val_name;
    OBJECT_ATTRIBUTES oa;
    HANDLE key_handle;
    NTSTATUS status;

    RtlInitUnicodeString(&key_path, path);
    InitializeObjectAttributes(&oa, &key_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&key_handle, KEY_SET_VALUE, &oa);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&val_name, value_name);
    ULONG data_len = (ULONG)(wcslen(value) + 1) * sizeof(wchar_t);

    status = ZwSetValueKey(key_handle, &val_name, 0, REG_SZ, (PVOID)value, data_len);
    ZwClose(key_handle);
    return status;
}

static NTSTATUS WriteRegistryDword(const wchar_t* path, const wchar_t* value_name,
    ULONG value)
{
    UNICODE_STRING key_path, val_name;
    OBJECT_ATTRIBUTES oa;
    HANDLE key_handle;
    NTSTATUS status;

    RtlInitUnicodeString(&key_path, path);
    InitializeObjectAttributes(&oa, &key_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&key_handle, KEY_SET_VALUE, &oa);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&val_name, value_name);
    status = ZwSetValueKey(key_handle, &val_name, 0, REG_DWORD, &value, sizeof(ULONG));
    ZwClose(key_handle);
    return status;
}

static NTSTATUS WriteRegistryBinary(const wchar_t* path, const wchar_t* value_name,
    const void* data, ULONG data_len)
{
    UNICODE_STRING key_path, val_name;
    OBJECT_ATTRIBUTES oa;
    HANDLE key_handle;
    NTSTATUS status;

    RtlInitUnicodeString(&key_path, path);
    InitializeObjectAttributes(&oa, &key_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwOpenKey(&key_handle, KEY_SET_VALUE, &oa);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&val_name, value_name);
    status = ZwSetValueKey(key_handle, &val_name, 0, REG_BINARY, (PVOID)data, data_len);
    ZwClose(key_handle);
    return status;
}

static NTSTATUS SpoofRegistry(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    if (ctx->original_machine_guid[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography",
            L"MachineGuid", ctx->original_machine_guid, 64);
    }
    wchar_t new_guid[64];
    GenerateRandomGuid(new_guid, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography",
        L"MachineGuid", new_guid);

    if (ctx->original_product_id[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"ProductId", ctx->original_product_id, 64);
    }
    wchar_t new_pid[64];
    GenerateRandomSerial(new_pid, 28, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"ProductId", new_pid);

    if (ctx->original_install_date[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"InstallDate", ctx->original_install_date, 64);
    }
    ULONG new_install_date = (ULONG)(SimpleRand(seed) % 1700000000ULL + 1400000000ULL);
    WriteRegistryDword(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"InstallDate", new_install_date);

    if (ctx->original_computer_name[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
            L"ComputerName", ctx->original_computer_name, 64);
    }
    wchar_t new_name[32];
    GenerateRandomAlphanumeric(new_name, 15, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
        L"ComputerName", new_name);
    WriteRegistryString(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName",
        L"ComputerName", new_name);

    if (ctx->original_product_name[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"ProductName", ctx->original_product_name, 64);
    }

    if (ctx->original_registered_owner[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"RegisteredOwner", ctx->original_registered_owner, 64);
    }
    wchar_t new_owner[32];
    GenerateRandomAlphanumeric(new_owner, 12, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"RegisteredOwner", new_owner);

    if (ctx->original_registered_org[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"RegisteredOrganization", ctx->original_registered_org, 64);
    }
    wchar_t new_org[32];
    GenerateRandomAlphanumeric(new_org, 12, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"RegisteredOrganization", new_org);

    wchar_t new_dpid[256];
    GenerateRandomAlphanumeric(new_dpid, 200, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"DigitalProductId", new_dpid);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"DigitalProductId4", new_dpid);

    if (ctx->original_build_guid[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"BuildGUID", ctx->original_build_guid, 64);
    }
    wchar_t new_build_guid[64];
    GenerateRandomGuid(new_build_guid, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"BuildGUID", new_build_guid);

    if (ctx->original_machine_id[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB",
            L"MachineId", ctx->original_machine_id, 64);
    }
    wchar_t new_uuid[64];
    GenerateRandomGuid(new_uuid, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB",
        L"MachineId", new_uuid);

    wchar_t new_hw_profile_guid[64];
    GenerateRandomGuid(new_hw_profile_guid, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB\\Hardware Profiles\\0001",
        L"HwProfileGuid", new_hw_profile_guid);

    if (ctx->original_cpu_id[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            L"ProcessorNameString", ctx->original_cpu_id, 64);
    }

    if (ctx->original_install_id[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"InstallID", ctx->original_install_id, 64);
    }
    wchar_t new_install_id[64];
    GenerateRandomAlphanumeric(new_install_id, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        L"InstallID", new_install_id);

    return STATUS_SUCCESS;
}

static NTSTATUS RestoreRegistry(PDEVICE_CONTEXT ctx) {
    if (ctx->original_machine_guid[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Cryptography",
            L"MachineGuid", ctx->original_machine_guid);
    }
    if (ctx->original_product_id[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"ProductId", ctx->original_product_id);
    }
    if (ctx->original_computer_name[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
            L"ComputerName", ctx->original_computer_name);
        WriteRegistryString(
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName",
            L"ComputerName", ctx->original_computer_name);
    }
    if (ctx->original_registered_owner[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"RegisteredOwner", ctx->original_registered_owner);
    }
    if (ctx->original_registered_org[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"RegisteredOrganization", ctx->original_registered_org);
    }
    if (ctx->original_build_guid[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"BuildGUID", ctx->original_build_guid);
    }
    if (ctx->original_machine_id[0] != L'\0') {
        WriteRegistryString(
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB",
            L"MachineId", ctx->original_machine_id);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS HookedDiskDispatch(PDEVICE_OBJECT device_object, PIRP irp) {
    PDEVICE_CONTEXT ctx = NULL;
    WDFDEVICE wdf_device = WdfDriverGetFirstDevice(WdfGetDriver());
    if (wdf_device) {
        ctx = GetDeviceContext(wdf_device);
    }

    if (ctx && irp && irp->Tail.Overlay.OriginalFileObject) {
        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);

        if (stack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
            ULONG ioctl = stack->Parameters.DeviceIoControl.IoControlCode;

            if (ioctl == 0x2D1400 || ioctl == 0x7C088 || ioctl == 0x7000C) {
                for (int i = 0; i < ctx->disks_hooked && i < MAX_HOOKED_DISKS; i++) {
                    if (ctx->disk_hooks[i].device_object == device_object && ctx->disk_hooks[i].hooked) {
                        irp->IoStatus.Status = STATUS_SUCCESS;
                        irp->IoStatus.Information = 0;
                        IoCompleteRequest(irp, IO_NO_INCREMENT);
                        return STATUS_SUCCESS;
                    }
                }
            }
        }
    }

    for (int i = 0; i < MAX_HOOKED_DISKS; i++) {
        if (ctx && ctx->disk_hooks[i].device_object == device_object && ctx->disk_hooks[i].hooked) {
            return ctx->disk_hooks[i].original_dispatch(device_object, irp);
        }
    }

    return IoCallDriver(device_object, irp);
}

static NTSTATUS HookDiskDevice(PDEVICE_CONTEXT ctx, const wchar_t* path, ULONG64* seed) {
    UNICODE_STRING dev_path;
    PFILE_OBJECT file_obj = NULL;
    PDEVICE_OBJECT dev_obj = NULL;

    RtlInitUnicodeString(&dev_path, path);
    NTSTATUS status = IoGetDeviceObjectPointer(&dev_path,
        FILE_ALL_ACCESS, &file_obj, &dev_obj);
    if (!NT_SUCCESS(status)) return status;

    if (ctx->disks_hooked >= MAX_HOOKED_DISKS) {
        ObDereferenceObject(file_obj);
        return STATUS_SUCCESS;
    }

    DISK_HOOK_ENTRY* entry = &ctx->disk_hooks[ctx->disks_hooked];
    entry->device_object = dev_obj;
    entry->original_dispatch = dev_obj->DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];

    GenerateRandomSerial(entry->spoofed_serial, 20, seed);
    GenerateRandomAlphanumeric(entry->spoofed_model, 40, seed);

    dev_obj->DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HookedDiskDispatch;

    entry->hooked = TRUE;
    ctx->disks_hooked++;
    ctx->disk_hooked = TRUE;

    ObDereferenceObject(file_obj);
    return STATUS_SUCCESS;
}

static NTSTATUS RestoreDiskHooks(PDEVICE_CONTEXT ctx) {
    for (int i = 0; i < ctx->disks_hooked && i < MAX_HOOKED_DISKS; i++) {
        if (ctx->disk_hooks[i].hooked && ctx->disk_hooks[i].original_dispatch) {
            ctx->disk_hooks[i].device_object->DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
                ctx->disk_hooks[i].original_dispatch;
            ctx->disk_hooks[i].hooked = FALSE;
        }
    }
    ctx->disk_hooked = FALSE;
    ctx->disks_hooked = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofDiskSerials(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    for (int i = 0; DISK_DEVICE_PATHS[i] != NULL; i++) {
        HookDiskDevice(ctx, DISK_DEVICE_PATHS[i], seed);
    }
    ctx->disk_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofVolumeSerials(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    for (int i = 0; VOLUME_DEVICE_PATHS[i] != NULL; i++) {
        UNICODE_STRING dev_path;
        PFILE_OBJECT file_obj = NULL;
        PDEVICE_OBJECT dev_obj = NULL;

        RtlInitUnicodeString(&dev_path, VOLUME_DEVICE_PATHS[i]);
        NTSTATUS status = IoGetDeviceObjectPointer(&dev_path,
            FILE_ALL_ACCESS, &file_obj, &dev_obj);
        if (!NT_SUCCESS(status)) continue;

        ULONG new_serial = 0;
        GenerateRandomVolumeSerial(&new_serial, seed);

        FILE_FS_VOLUME_INFORMATION vol_info;
        vol_info.VolumeCreationTime.QuadPart = 0;
        vol_info.VolumeSerialNumber = new_serial;
        vol_info.SupportsObjects = FALSE;
        vol_info.VolumeLabelLength = 0;

        IO_STATUS_BLOCK iosb;
        KEVENT event;
        KeInitializeEvent(&event, NotificationEvent, FALSE);

        PIRP irp = IoBuildSynchronousFsdRequest(IRP_MJ_SET_INFORMATION,
            dev_obj, NULL, 0, NULL, &event, &iosb);
        if (irp) {
            irp->Tail.Overlay.Overlay.DeviceObject = dev_obj;
            irp->Tail.Overlay.Overlay.FileObject = file_obj;
            irp->UserIosb = &iosb;
            irp->UserEvent = &event;
            irp->RequestorMode = KernelMode;
            irp->Flags = 0;

            PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(irp);
            stack->MajorFunction = IRP_MJ_SET_VOLUME_INFORMATION;
            stack->Parameters.SetVolume.FsInformationClass = FileFsVolumeInformation;
            stack->DeviceObject = dev_obj;

            irp->AssociatedIrp.SystemBuffer = &vol_info;
            irp->Flags |= IRP_BUFFERED_IO;

            IoCallDriver(dev_obj, irp);
            KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        }

        ObDereferenceObject(file_obj);
    }
    ctx->volume_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofMACAddresses(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    UNICODE_STRING nic_path;
    RtlInitUnicodeString(&nic_path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nic_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    HANDLE key_handle;
    NTSTATUS status = ZwOpenKey(&key_handle, KEY_ENUMERATE_SUB_KEYS | KEY_READ | KEY_WRITE, &oa);
    if (!NT_SUCCESS(status)) return status;

    ULONG index = 0;
    KEY_BASIC_INFORMATION* kbi = (KEY_BASIC_INFORMATION*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, 4096, 'Zyph');

    if (!kbi) { ZwClose(key_handle); return STATUS_INSUFFICIENT_RESOURCES; }

    while (ZwEnumerateKey(key_handle, index, KeyBasicInformation,
        kbi, 4096, &(ULONG){4096}) == STATUS_SUCCESS)
    {
        index++;
        if (kbi->NameLength == 0 || kbi->NameLength >= 16 * sizeof(wchar_t)) continue;

        wchar_t subkey_name[16] = { 0 };
        RtlCopyMemory(subkey_name, kbi->Name, kbi->NameLength);

        if (subkey_name[0] == L'0' && subkey_name[1] >= L'0' && subkey_name[1] <= L'9') {
            wchar_t full_path[256];
            RtlStringCchPrintfW(full_path, 256,
                L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}\\%s",
                subkey_name);

            BYTE new_mac[6];
            GenerateRandomMac(new_mac, seed);

            wchar_t mac_str[16];
            RtlStringCchPrintfW(mac_str, 16, L"%02X%02X%02X%02X%02X%02X",
                new_mac[0], new_mac[1], new_mac[2],
                new_mac[3], new_mac[4], new_mac[5]);

            WriteRegistryString(full_path, L"NetworkAddress", mac_str);

            if (ctx->adapters_spoofed < MAX_HOOKED_NICS) {
                NIC_HOOK_ENTRY* entry = &ctx->nic_hooks[ctx->adapters_spoofed];
                RtlCopyMemory(entry->spoofed_mac, new_mac, 6);
                entry->hooked = TRUE;
            }
            ctx->adapters_spoofed++;
        }
    }

    ExFreePoolWithTag(kbi, 'Zyph');
    ZwClose(key_handle);
    ctx->mac_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofGPU(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    for (int i = 0; GPU_REGISTRY_PATHS[i] != NULL; i++) {
        wchar_t new_driver_date[32];
        GenerateRandomAlphanumeric(new_driver_date, 8, seed);

        wchar_t new_driver_version[32];
        ULONG ver_major = (ULONG)(SimpleRand(seed) % 10 + 30);
        ULONG ver_minor = (ULONG)(SimpleRand(seed) % 100);
        RtlStringCchPrintfW(new_driver_version, 32, L"%lu.%lu.0.0", ver_major, ver_minor);

        WriteRegistryString(GPU_REGISTRY_PATHS[i], L"DriverDate", new_driver_date);
        WriteRegistryString(GPU_REGISTRY_PATHS[i], L"DriverVersion", new_driver_version);

        wchar_t new_gpu_uuid[64];
        GenerateRandomGuid(new_gpu_uuid, seed);
        WriteRegistryString(GPU_REGISTRY_PATHS[i], L"GPU_UUID", new_gpu_uuid);
    }
    ctx->gpu_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofMonitorEDID(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    UNICODE_STRING mon_path;
    RtlInitUnicodeString(&mon_path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\DISPLAY");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &mon_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    HANDLE key_handle;
    NTSTATUS status = ZwOpenKey(&key_handle, KEY_ENUMERATE_SUB_KEYS, &oa);
    if (!NT_SUCCESS(status)) return status;

    KEY_BASIC_INFORMATION* kbi = (KEY_BASIC_INFORMATION*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, 4096, 'Zyph');
    if (!kbi) { ZwClose(key_handle); return STATUS_INSUFFICIENT_RESOURCES; }

    ULONG subkey_len = 4096;
    ULONG index = 0;

    while (ZwEnumerateKey(key_handle, index, KeyBasicInformation,
        kbi, 4096, &subkey_len) == STATUS_SUCCESS)
    {
        index++;
        if (kbi->NameLength == 0) continue;

        wchar_t* subkey = (wchar_t*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, kbi->NameLength + sizeof(wchar_t), 'Zyph');
        if (!subkey) continue;

        RtlCopyMemory(subkey, kbi->Name, kbi->NameLength);
        subkey[kbi->NameLength / sizeof(wchar_t)] = L'\0';

        wchar_t full_path[256];
        RtlStringCchPrintfW(full_path, 256,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\%s", subkey);

        OBJECT_ATTRIBUTES sub_oa;
        UNICODE_STRING sub_us;
        RtlInitUnicodeString(&sub_us, full_path);
        InitializeObjectAttributes(&sub_oa, &sub_us,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

        HANDLE sub_key;
        if (ZwOpenKey(&sub_key, KEY_ENUMERATE_SUB_KEYS | KEY_WRITE, &sub_oa) == STATUS_SUCCESS) {
            KEY_BASIC_INFORMATION* sub_kbi = (KEY_BASIC_INFORMATION*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, 4096, 'Zyph');
            if (sub_kbi) {
                ULONG sub_sub_len = 4096;
                ULONG sub_idx = 0;
                while (ZwEnumerateKey(sub_key, sub_idx, KeyBasicInformation,
                    sub_kbi, 4096, &sub_sub_len) == STATUS_SUCCESS)
                {
                    sub_idx++;
                    if (sub_kbi->NameLength == 0) continue;

                    wchar_t* sub_subkey = (wchar_t*)ExAllocatePool2(
                        POOL_FLAG_NON_PAGED, sub_kbi->NameLength + sizeof(wchar_t), 'Zyph');
                    if (!sub_subkey) continue;

                    RtlCopyMemory(sub_subkey, sub_kbi->Name, sub_kbi->NameLength);
                    sub_subkey[sub_kbi->NameLength / sizeof(wchar_t)] = L'\0';

                    wchar_t edid_path[512];
                    RtlStringCchPrintfW(edid_path, 512,
                        L"%s\\%s\\Device Parameters", full_path, sub_subkey);

                    BYTE edid[128];
                    for (int j = 0; j < 128; j++)
                        edid[j] = (BYTE)(SimpleRand(seed) & 0xFF);
                    edid[0] = 0x00; edid[1] = 0xFF; edid[2] = 0xFF; edid[3] = 0xFF;
                    edid[4] = 0xFF; edid[5] = 0xFF; edid[6] = 0xFF; edid[7] = 0x00;

                    WriteRegistryBinary(edid_path, L"EDID", edid, 128);
                    WriteRegistryDword(edid_path, L"Active", 1);
                    WriteRegistryDword(edid_path, L"Flags", 1);

                    ExFreePoolWithTag(sub_subkey, 'Zyph');
                }
                ExFreePoolWithTag(sub_kbi, 'Zyph');
            }
            ZwClose(sub_key);
        }

        ExFreePoolWithTag(subkey, 'Zyph');
    }

    ExFreePoolWithTag(kbi, 'Zyph');
    ZwClose(key_handle);
    ctx->monitor_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofUSB(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    UNICODE_STRING usb_path;
    RtlInitUnicodeString(&usb_path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\USB");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &usb_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    HANDLE key_handle;
    NTSTATUS status = ZwOpenKey(&key_handle, KEY_ENUMERATE_SUB_KEYS | KEY_WRITE, &oa);
    if (!NT_SUCCESS(status)) return status;

    KEY_BASIC_INFORMATION* kbi = (KEY_BASIC_INFORMATION*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, 4096, 'Zyph');
    if (!kbi) { ZwClose(key_handle); return STATUS_INSUFFICIENT_RESOURCES; }

    ULONG subkey_len = 4096;
    ULONG index = 0;

    while (ZwEnumerateKey(key_handle, index, KeyBasicInformation,
        kbi, 4096, &subkey_len) == STATUS_SUCCESS)
    {
        index++;
        if (kbi->NameLength == 0) continue;

        wchar_t* subkey = (wchar_t*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, kbi->NameLength + sizeof(wchar_t), 'Zyph');
        if (!subkey) continue;

        RtlCopyMemory(subkey, kbi->Name, kbi->NameLength);
        subkey[kbi->NameLength / sizeof(wchar_t)] = L'\0';

        wchar_t full_path[256];
        RtlStringCchPrintfW(full_path, 256,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum\\USB\\%s", subkey);

        OBJECT_ATTRIBUTES sub_oa;
        UNICODE_STRING sub_us;
        RtlInitUnicodeString(&sub_us, full_path);
        InitializeObjectAttributes(&sub_oa, &sub_us,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

        HANDLE sub_key;
        if (ZwOpenKey(&sub_key, KEY_ENUMERATE_SUB_KEYS | KEY_WRITE, &sub_oa) == STATUS_SUCCESS) {
            KEY_BASIC_INFORMATION* sub_kbi = (KEY_BASIC_INFORMATION*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, 4096, 'Zyph');
            if (sub_kbi) {
                ULONG sub_sub_len = 4096;
                ULONG sub_idx = 0;
                while (ZwEnumerateKey(sub_key, sub_idx, KeyBasicInformation,
                    sub_kbi, 4096, &sub_sub_len) == STATUS_SUCCESS)
                {
                    sub_idx++;
                    if (sub_kbi->NameLength == 0) continue;

                    wchar_t* serial = (wchar_t*)ExAllocatePool2(
                        POOL_FLAG_NON_PAGED, sub_kbi->NameLength + sizeof(wchar_t), 'Zyph');
                    if (!serial) continue;

                    RtlCopyMemory(serial, sub_kbi->Name, sub_kbi->NameLength);
                    serial[sub_kbi->NameLength / sizeof(wchar_t)] = L'\0';

                    wchar_t dev_params[512];
                    RtlStringCchPrintfW(dev_params, 512,
                        L"%s\\%s\\Device Parameters", full_path, serial);

                    wchar_t new_serial[32];
                    GenerateRandomSerial(new_serial, 16, seed);

                    WriteRegistryString(dev_params, L"SerialNumber", new_serial);

                    ExFreePoolWithTag(serial, 'Zyph');
                }
                ExFreePoolWithTag(sub_kbi, 'Zyph');
            }
            ZwClose(sub_key);
        }

        ExFreePoolWithTag(subkey, 'Zyph');
    }

    ExFreePoolWithTag(kbi, 'Zyph');
    ZwClose(key_handle);
    ctx->usb_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofSMBIOS(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    wchar_t new_bios_vendor[64];
    GenerateRandomAlphanumeric(new_bios_vendor, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"BiosVendor", new_bios_vendor);

    wchar_t new_bios_version[64];
    GenerateRandomAlphanumeric(new_bios_version, 16, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"BiosVersion", new_bios_version);

    wchar_t new_bios_date[32];
    int month = (int)(SimpleRand(seed) % 12) + 1;
    int day = (int)(SimpleRand(seed) % 28) + 1;
    int year = (int)(SimpleRand(seed) % 10) + 2015;
    RtlStringCchPrintfW(new_bios_date, 32, L"%02d/%02d/%d", month, day, year);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"BiosReleaseDate", new_bios_date);

    wchar_t new_sys_manufacturer[64];
    GenerateRandomAlphanumeric(new_sys_manufacturer, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"SystemManufacturer", new_sys_manufacturer);

    wchar_t new_sys_product[64];
    GenerateRandomAlphanumeric(new_sys_product, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"SystemProductName", new_sys_product);

    wchar_t new_sys_version[64];
    GenerateRandomAlphanumeric(new_sys_version, 12, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"SystemVersion", new_sys_version);

    wchar_t new_sys_serial[64];
    GenerateRandomSerial(new_sys_serial, 24, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"SystemSerialNumber", new_sys_serial);

    wchar_t new_baseboard_manufacturer[64];
    GenerateRandomAlphanumeric(new_baseboard_manufacturer, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"BaseBoardManufacturer", new_baseboard_manufacturer);

    wchar_t new_baseboard_product[64];
    GenerateRandomAlphanumeric(new_baseboard_product, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"BaseBoardProduct", new_baseboard_product);

    wchar_t new_baseboard_serial[64];
    GenerateRandomSerial(new_baseboard_serial, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"BaseBoardSerialNumber", new_baseboard_serial);

    wchar_t new_chassis_manufacturer[64];
    GenerateRandomAlphanumeric(new_chassis_manufacturer, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"ChassisManufacturer", new_chassis_manufacturer);

    wchar_t new_chassis_serial[64];
    GenerateRandomSerial(new_chassis_serial, 16, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS",
        L"ChassisSerialNumber", new_chassis_serial);

    ctx->smbios_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofWMI(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    wchar_t new_wmi_serial[64];
    GenerateRandomSerial(new_wmi_serial, 20, seed);

    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Wbem\\CIMOM",
        L"SerialNumber", new_wmi_serial);

    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Wbem\\Providers",
        L"Win32_BIOS_Serial", new_wmi_serial);

    wchar_t new_wmi_disk[64];
    GenerateRandomSerial(new_wmi_disk, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Wbem\\Providers",
        L"Win32_DiskDrive_Serial", new_wmi_disk);

    wchar_t new_wmi_board[64];
    GenerateRandomSerial(new_wmi_board, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Wbem\\Providers",
        L"Win32_BaseBoard_Serial", new_wmi_board);

    ctx->wmi_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS SpoofNVMe(PDEVICE_CONTEXT ctx, ULONG64* seed) {
    if (ctx->original_nvme_serial[0] == L'\0') {
        ReadRegistryString(
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e96b-e325-11ce-bfc1-08002be10318}\\0000",
            L"SerialNumber", ctx->original_nvme_serial, 64);
    }

    wchar_t new_nvme_serial[64];
    GenerateRandomSerial(new_nvme_serial, 20, seed);
    WriteRegistryString(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e96b-e325-11ce-bfc1-08002be10318}\\0000",
        L"SerialNumber", new_nvme_serial);

    ctx->nvme_spoofed = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS DoSpoofAll(PDEVICE_CONTEXT ctx, PSPOOF_REQUEST req) {
    ULONG64 seed = req->seed;
    if (seed == 0) {
        seed = KeQueryPerformanceCounter(NULL).QuadPart;
    }
    ctx->random_seed = seed;

    if (req->spoof_registry) SpoofRegistry(ctx, &seed);
    if (req->spoof_volume) SpoofVolumeSerials(ctx, &seed);
    if (req->spoof_disk) SpoofDiskSerials(ctx, &seed);
    if (req->spoof_mac) SpoofMACAddresses(ctx, &seed);
    if (req->spoof_gpu) SpoofGPU(ctx, &seed);
    if (req->spoof_monitor) SpoofMonitorEDID(ctx, &seed);
    if (req->spoof_usb) SpoofUSB(ctx, &seed);
    if (req->spoof_smbios) SpoofSMBIOS(ctx, &seed);
    if (req->spoof_wmi) SpoofWMI(ctx, &seed);
    if (req->spoof_nvme) SpoofNVMe(ctx, &seed);

    DbgPrint("[ZypherSpoofer] All vectors spoofed with seed %llu\n", seed);
    DbgPrint("[ZypherSpoofer] Disks hooked: %lu, Adapters spoofed: %lu\n",
        ctx->disks_hooked, ctx->adapters_spoofed);
    return STATUS_SUCCESS;
}

static NTSTATUS DoRestoreAll(PDEVICE_CONTEXT ctx) {
    RestoreRegistry(ctx);
    RestoreDiskHooks(ctx);

    UNICODE_STRING nic_path;
    RtlInitUnicodeString(&nic_path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nic_path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    HANDLE key_handle;
    if (ZwOpenKey(&key_handle, KEY_ENUMERATE_SUB_KEYS | KEY_READ | KEY_WRITE, &oa) == STATUS_SUCCESS) {
        KEY_BASIC_INFORMATION* kbi = (KEY_BASIC_INFORMATION*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, 4096, 'Zyph');
        if (kbi) {
            ULONG subkey_len = 4096;
            ULONG index = 0;
            while (ZwEnumerateKey(key_handle, index, KeyBasicInformation,
                kbi, 4096, &subkey_len) == STATUS_SUCCESS)
            {
                index++;
                if (kbi->NameLength == 0) continue;

                wchar_t* subkey = (wchar_t*)ExAllocatePool2(
                    POOL_FLAG_NON_PAGED, kbi->NameLength + sizeof(wchar_t), 'Zyph');
                if (!subkey) continue;

                RtlCopyMemory(subkey, kbi->Name, kbi->NameLength);
                subkey[kbi->NameLength / sizeof(wchar_t)] = L'\0';

                if (subkey[0] == L'0' && subkey[1] >= L'0' && subkey[1] <= L'9') {
                    wchar_t full_path[256];
                    RtlStringCchPrintfW(full_path, 256,
                        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}\\%s",
                        subkey);
                    WriteRegistryString(full_path, L"NetworkAddress", L"");
                }

                ExFreePoolWithTag(subkey, 'Zyph');
            }
            ExFreePoolWithTag(kbi, 'Zyph');
        }
        ZwClose(key_handle);
    }

    ctx->volume_spoofed = FALSE;
    ctx->disk_spoofed = FALSE;
    ctx->registry_spoofed = FALSE;
    ctx->smbios_spoofed = FALSE;
    ctx->mac_spoofed = FALSE;
    ctx->gpu_spoofed = FALSE;
    ctx->monitor_spoofed = FALSE;
    ctx->usb_spoofed = FALSE;
    ctx->wmi_spoofed = FALSE;
    ctx->cpu_spoofed = FALSE;
    ctx->nvme_spoofed = FALSE;
    ctx->disk_hooked = FALSE;
    ctx->ndis_hooked = FALSE;
    ctx->adapters_spoofed = 0;
    ctx->disks_hooked = 0;

    DbgPrint("[ZypherSpoofer] All vectors restored\n");
    return STATUS_SUCCESS;
}

static void EvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST request,
    size_t output_buffer_length, size_t input_buffer_length, ULONG ioctl_code)
{
    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(output_buffer_length);
    UNREFERENCED_PARAMETER(input_buffer_length);

    WDFDEVICE device = WdfIoQueueGetDevice(queue);
    PDEVICE_CONTEXT ctx = GetDeviceContext(device);
    NTSTATUS status = STATUS_SUCCESS;

    switch (ioctl_code) {
    case IOCTL_SPOOFER_SPOOF_ALL: {
        PSPOOF_REQUEST req = NULL;
        size_t req_size = 0;
        status = WdfRequestRetrieveInputBuffer(request, sizeof(SPOOF_REQUEST),
            (PVOID*)&req, &req_size);
        if (NT_SUCCESS(status)) {
            status = DoSpoofAll(ctx, req);
        }
        break;
    }
    case IOCTL_SPOOFER_RESTORE_ALL: {
        status = DoRestoreAll(ctx);
        break;
    }
    case IOCTL_SPOOFER_GET_STATUS: {
        PSPOOFER_STATUS out_status = NULL;
        size_t out_size = 0;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(SPOOFER_STATUS),
            (PVOID*)&out_status, &out_size);
        if (NT_SUCCESS(status)) {
            out_status->volume_spoofed = ctx->volume_spoofed;
            out_status->disk_spoofed = ctx->disk_spoofed;
            out_status->registry_spoofed = ctx->registry_spoofed;
            out_status->smbios_spoofed = ctx->smbios_spoofed;
            out_status->mac_spoofed = ctx->mac_spoofed;
            out_status->gpu_spoofed = ctx->gpu_spoofed;
            out_status->monitor_spoofed = ctx->monitor_spoofed;
            out_status->usb_spoofed = ctx->usb_spoofed;
            out_status->wmi_spoofed = ctx->wmi_spoofed;
            out_status->disk_hooked = ctx->disk_hooked;
            out_status->ndis_hooked = ctx->ndis_hooked;
            out_status->cpu_spoofed = ctx->cpu_spoofed;
            out_status->nvme_spoofed = ctx->nvme_spoofed;
            out_status->seed = ctx->random_seed;
            out_status->adapters_spoofed = ctx->adapters_spoofed;
            out_status->disks_hooked = ctx->disks_hooked;
            WdfRequestSetInformation(request, sizeof(SPOOFER_STATUS));
        }
        break;
    }
    case IOCTL_SPOOFER_SET_SEED: {
        ULONG64* seed = NULL;
        size_t seed_size = 0;
        status = WdfRequestRetrieveInputBuffer(request, sizeof(ULONG64),
            (PVOID*)&seed, &seed_size);
        if (NT_SUCCESS(status)) {
            ctx->random_seed = *seed;
        }
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(request, status);
}

static void EvtDriverUnload(WDFDRIVER driver) {
    UNREFERENCED_PARAMETER(driver);

    WDFDEVICE device = WdfDriverGetFirstDevice(driver);
    if (device) {
        PDEVICE_CONTEXT ctx = GetDeviceContext(device);
        DoRestoreAll(ctx);
    }

    DbgPrint("[ZypherSpoofer] Driver unloaded\n");
}

static NTSTATUS EvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT device_init) {
    UNREFERENCED_PARAMETER(driver);

    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queue_config;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

    NTSTATUS status = WdfDeviceCreate(&device_init, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;

    UNICODE_STRING dos_name;
    RtlInitUnicodeString(&dos_name, SPOOFER_DOS_NAME);
    status = WdfDeviceCreateSymbolicLink(device, &dos_name);
    if (!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config, WdfIoQueueDispatchParallel);
    queue_config.EvtIoDeviceControl = EvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queue_config, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) return status;

    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
    UNREFERENCED_PARAMETER(registry_path);

    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
    config.EvtDriverUnload = EvtDriverUnload;

    NTSTATUS status = WdfDriverCreate(driver_object, registry_path,
        WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);

    if (NT_SUCCESS(status)) {
        DbgPrint("[ZypherSpoofer] Driver loaded\n");
    } else {
        DbgPrint("[ZypherSpoofer] Load failed: 0x%X\n", status);
    }

    return status;
}
