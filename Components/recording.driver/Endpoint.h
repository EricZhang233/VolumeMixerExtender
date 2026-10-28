#pragma once

NTSTATUS VmexRecordingNodeProperty(PPCPROPERTY_REQUEST request);

static PCPROPERTY_ITEM VmexVolumeProperties[] =
{
    { &KSPROPSETID_Audio, KSPROPERTY_AUDIO_VOLUMELEVEL,
      KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT, VmexRecordingNodeProperty }
};
static PCPROPERTY_ITEM VmexMuteProperties[] =
{
    { &KSPROPSETID_Audio, KSPROPERTY_AUDIO_MUTE,
      KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT, VmexRecordingNodeProperty }
};
DEFINE_PCAUTOMATION_TABLE_PROP(VmexVolumeAutomation, VmexVolumeProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(VmexMuteAutomation, VmexMuteProperties);
static PCNODE_DESCRIPTOR VmexTopologyNodes[] =
{
    { 0, &VmexVolumeAutomation, &KSNODETYPE_VOLUME, nullptr },
    { 0, &VmexMuteAutomation, &KSNODETYPE_MUTE, nullptr }
};
static PCCONNECTION_DESCRIPTOR VmexTopologyConnections[] =
{
    { PCFILTER_NODE, 0, 0, 1 },
    { 0, 0, 1, 1 },
    { 1, 0, PCFILTER_NODE, 1 }
};
static PCFILTER_DESCRIPTOR VmexTopologyFilter =
{
    0, &AutomationSpeakerTopoFilter, sizeof(PCPIN_DESCRIPTOR), ARRAYSIZE(SpeakerTopoMiniportPins), SpeakerTopoMiniportPins,
    sizeof(PCNODE_DESCRIPTOR), ARRAYSIZE(VmexTopologyNodes), VmexTopologyNodes,
    ARRAYSIZE(VmexTopologyConnections), VmexTopologyConnections, 0, nullptr
};
static PCPIN_DESCRIPTOR VmexWavePins[] =
{
    { 1, 1, 0, &AutomationSpeakerHostPin,
      { 0, nullptr, 0, nullptr, ARRAYSIZE(SpeakerPinDataRangePointersStream), SpeakerPinDataRangePointersStream,
        KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_SINK, &KSCATEGORY_AUDIO, nullptr, 0 } },
    { 0, 0, 0, nullptr,
      { 0, nullptr, 0, nullptr, ARRAYSIZE(SpeakerPinDataRangePointersBridge), SpeakerPinDataRangePointersBridge,
        KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, nullptr, 0 } }
};
static PIN_DEVICE_FORMATS_AND_MODES VmexPinFormats[] =
{
    { SystemRenderPin, SpeakerHostPinSupportedDeviceFormats, ARRAYSIZE(SpeakerHostPinSupportedDeviceFormats),
      SpeakerHostPinSupportedDeviceModes, ARRAYSIZE(SpeakerHostPinSupportedDeviceModes) },
    { BridgePin, nullptr, 0, nullptr, 0 }
};
static PCCONNECTION_DESCRIPTOR VmexWaveConnections[] =
{
    { PCFILTER_NODE, 0, PCFILTER_NODE, 1 }
};
static PCPROPERTY_ITEM VmexWaveProperties[] =
{
    { &KSPROPSETID_Pin, KSPROPERTY_PIN_PROPOSEDATAFORMAT,
      KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT, PropertyHandler_WaveFilter },
    { &KSPROPSETID_Pin, KSPROPERTY_PIN_PROPOSEDATAFORMAT2,
      KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT, PropertyHandler_WaveFilter }
};
DEFINE_PCAUTOMATION_TABLE_PROP(VmexWaveAutomation, VmexWaveProperties);
static PCFILTER_DESCRIPTOR VmexWaveFilter =
{
    0, &VmexWaveAutomation, sizeof(PCPIN_DESCRIPTOR), ARRAYSIZE(VmexWavePins), VmexWavePins,
    sizeof(PCNODE_DESCRIPTOR), 0, nullptr,
    ARRAYSIZE(VmexWaveConnections), VmexWaveConnections, 0, nullptr
};
static PHYSICALCONNECTIONTABLE VmexPhysicalConnections[] =
{
    { 0, 1, CONNECTIONTYPE_WAVE_OUTPUT }
};
