#include <sysvad.h>
#include "simple.h"

NTSTATUS VmexRecordingNodeProperty(PPCPROPERTY_REQUEST request)
{
    if (!request || !request->PropertyItem) return STATUS_INVALID_PARAMETER;
    const auto id = request->PropertyItem->Id;
    if (id != KSPROPERTY_AUDIO_VOLUMELEVEL && id != KSPROPERTY_AUDIO_MUTE) return STATUS_INVALID_DEVICE_REQUEST;
    if (request->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        if (id == KSPROPERTY_AUDIO_VOLUMELEVEL) return PropertyHandler_BasicSupportVolume(request, 2);
        return PropertyHandler_BasicSupport(request, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT, VT_BOOL);
    }
    const auto valid = ValidatePropertyParams(request, sizeof(LONG), sizeof(ULONG));
    if (!NT_SUCCESS(valid)) return valid;
    const auto channel = *static_cast<const ULONG*>(request->Instance);
    if (channel >= 2 && channel != ALL_CHANNELS_ID) return STATUS_INVALID_PARAMETER;
    auto* value = static_cast<LONG*>(request->Value);
    if (request->Verb & KSPROPERTY_TYPE_GET)
    {
        *value = 0;
        request->ValueSize = sizeof(LONG);
        return STATUS_SUCCESS;
    }
    if (request->Verb & KSPROPERTY_TYPE_SET) return *value == 0 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    return STATUS_INVALID_DEVICE_REQUEST;
}
