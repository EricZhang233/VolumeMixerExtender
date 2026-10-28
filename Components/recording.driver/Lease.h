#pragma once

#include <ntddk.h>
#include <portcls.h>

NTSTATUS CMiniportTopology_EventHandler_JackState(PPCEVENT_REQUEST request);

class CMiniportTopology;

NTSTATUS VmexRecordingInitialize(PDRIVER_OBJECT driver);
void VmexRecordingAttach(CMiniportTopology* topology);
void VmexRecordingDetach(CMiniportTopology* topology);
void VmexRecordingRemove();
bool VmexRecordingConnected();
