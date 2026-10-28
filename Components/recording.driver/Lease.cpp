#include <sysvad.h>
#include "simple.h"
#include "mintopo.h"
#include <wdmsec.h>
#include <initializer_list>
#include "RecordingDriverContract.h"
#include "Lease.h"

namespace
{
    PDEVICE_OBJECT control = nullptr;
    PDRIVER_OBJECT driverObject = nullptr;
    ULONG generation = 0;
    struct ControlState final { PFILE_OBJECT owner; bool removed; };
    CMiniportTopology* topology = nullptr;
    KMUTEX mutex;
    volatile LONG connected = 0;
    PDRIVER_DISPATCH previous[IRP_MJ_MAXIMUM_FUNCTION + 1]{};
    PDRIVER_UNLOAD previousUnload = nullptr;
    UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\DosDevices\\VmexRecording");

    class Lock final
    {
    public:
        Lock() { KeWaitForSingleObject(&mutex, Executive, KernelMode, FALSE, nullptr); }
        ~Lock() { KeReleaseMutex(&mutex, FALSE); }
    };

    NTSTATUS CreateControl()
    {
        if (control) return STATUS_SUCCESS;
        wchar_t name[64]{};
        auto status = RtlStringCchPrintfW(name, ARRAYSIZE(name), L"\\Device\\VmexRecording%lu", ++generation);
        if (!NT_SUCCESS(status)) return status;
        UNICODE_STRING deviceName;
        RtlInitUnicodeString(&deviceName, name);
        UNICODE_STRING security = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)");
        status = IoCreateDeviceSecure(driverObject, sizeof(ControlState), &deviceName, VMEX_RECORDING_DEVICE_TYPE,
            FILE_DEVICE_SECURE_OPEN, TRUE, &security, &VMEX_RECORDING_INTERFACE, &control);
        if (!NT_SUCCESS(status)) return status;
        RtlZeroMemory(control->DeviceExtension, sizeof(ControlState));
        control->Flags |= DO_BUFFERED_IO;
        status = IoCreateSymbolicLink(&linkName, &deviceName);
        if (!NT_SUCCESS(status)) { IoDeleteDevice(control); control = nullptr; return status; }
        control->Flags &= ~DO_DEVICE_INITIALIZING;
        return STATUS_SUCCESS;
    }

    void SetConnected(bool value)
    {
        const auto next = value ? 1L : 0L;
        if (InterlockedExchange(&connected, next) == next) return;
        if (topology)
            topology->GenerateEventList(const_cast<GUID*>(&KSEVENTSETID_PinCapsChange),
                KSEVENT_PINCAPS_JACKINFOCHANGE, TRUE, 1, FALSE, 0);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "VMEX recording jack=%ld\n", next);
    }

    NTSTATUS Dispatch(PDEVICE_OBJECT device, PIRP irp)
    {
        auto* stack = IoGetCurrentIrpStackLocation(irp);
        if (device->DeviceType != VMEX_RECORDING_DEVICE_TYPE)
        {
            if (previous[stack->MajorFunction]) return previous[stack->MajorFunction](device, irp);
            irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            irp->IoStatus.Information = 0;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return STATUS_INVALID_DEVICE_REQUEST;
        }
        auto* state = static_cast<ControlState*>(device->DeviceExtension);
        NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
        ULONG_PTR information = 0;
        {
            Lock lock;
            switch (stack->MajorFunction)
            {
            case IRP_MJ_CREATE:
                if (state->removed) status = STATUS_DELETE_PENDING;
                else if (state->owner) status = STATUS_SHARING_VIOLATION;
                else { state->owner = stack->FileObject; status = STATUS_SUCCESS; }
                break;
            case IRP_MJ_CLEANUP:
            case IRP_MJ_CLOSE:
                if (state->owner == stack->FileObject)
                {
                    if (!state->removed) SetConnected(false);
                    state->owner = nullptr;
                }
                status = STATUS_SUCCESS;
                break;
            case IRP_MJ_DEVICE_CONTROL:
                if (state->removed) { status = STATUS_DELETE_PENDING; break; }
                if (state->owner != stack->FileObject) { status = STATUS_ACCESS_DENIED; break; }
                if (stack->Parameters.DeviceIoControl.IoControlCode == VMEX_RECORDING_QUERY)
                {
                    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(VmexRecordingRequest))
                        status = STATUS_BUFFER_TOO_SMALL;
                    else
                    {
                        auto* response = static_cast<VmexRecordingRequest*>(irp->AssociatedIrp.SystemBuffer);
                        response->version = VMEX_RECORDING_VERSION;
                        response->connected = VmexRecordingConnected();
                        information = sizeof(*response);
                        status = STATUS_SUCCESS;
                    }
                }
                else if (stack->Parameters.DeviceIoControl.IoControlCode == VMEX_RECORDING_CONNECT)
                {
                    if (stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(VmexRecordingRequest))
                        status = STATUS_INVALID_BUFFER_SIZE;
                    else
                    {
                        const auto* request = static_cast<const VmexRecordingRequest*>(irp->AssociatedIrp.SystemBuffer);
                        if (request->version != VMEX_RECORDING_VERSION || request->connected > 1)
                            status = STATUS_INVALID_PARAMETER;
                        else if (!topology && request->connected) status = STATUS_DEVICE_NOT_READY;
                        else
                        {
                            SetConnected(request->connected != 0);
                            status = STATUS_SUCCESS;
                        }
                    }
                }
                break;
            }
        }
        irp->IoStatus.Status = status;
        irp->IoStatus.Information = information;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
    }

    void Unload(PDRIVER_OBJECT driver)
    {
        VmexRecordingRemove();
        for (ULONG major = 0; major <= IRP_MJ_MAXIMUM_FUNCTION; ++major)
            driver->MajorFunction[major] = previous[major];
        if (previousUnload) previousUnload(driver);
    }
}

NTSTATUS VmexRecordingInitialize(PDRIVER_OBJECT driver)
{
    KeInitializeMutex(&mutex, 0);
    driverObject = driver;
    for (ULONG major = 0; major <= IRP_MJ_MAXIMUM_FUNCTION; ++major)
    {
        previous[major] = driver->MajorFunction[major];
        driver->MajorFunction[major] = Dispatch;
    }
    previousUnload = driver->DriverUnload;
    driver->DriverUnload = Unload;
    return STATUS_SUCCESS;
}

void VmexRecordingAttach(CMiniportTopology* value)
{
    Lock lock;
    topology = value;
    const auto status = CreateControl();
    if (!NT_SUCCESS(status))
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "VMEX control creation failed=%08lX\n", status);
}

void VmexRecordingDetach(CMiniportTopology* value)
{
    Lock lock;
    if (topology == value)
    {
        SetConnected(false);
        topology = nullptr;
    }
}

bool VmexRecordingConnected()
{
    return InterlockedCompareExchange(&connected, 0, 0) != 0;
}

void VmexRecordingRemove()
{
    Lock lock;
    SetConnected(false);
    topology = nullptr;
    if (control)
    {
        auto* state = static_cast<ControlState*>(control->DeviceExtension);
        state->removed = true;
        IoDeleteSymbolicLink(&linkName);
        IoDeleteDevice(control);
        control = nullptr;
    }
}
