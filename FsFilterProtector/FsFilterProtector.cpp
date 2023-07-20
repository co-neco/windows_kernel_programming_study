/*++

Module Name:

    FsFilterProtector.cpp

Abstract:

    This is the main module of the FsFilterProtector miniFilter driver.

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <dontuse.h>

#include "common.h"
#include "utils.h"
#include "types.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

const int g_maxDirectories = 32;
struct GlobalData {

    DirectoryEntry DirNames[g_maxDirectories] = {0};
    int dirNamesCount = 0;
    FastMutex mutex;
};

GlobalData g_data;

void ClearDirectories();
int FindDirectory(PCUNICODE_STRING name, bool dosName);
bool IsDeleteAllowedByDirectories(PFLT_CALLBACK_DATA data);
NTSTATUS ConvertDosNameToNtName(PCWSTR dosName, PUNICODE_STRING ntName);

PFLT_FILTER gFilterHandle = NULL;
ULONG_PTR OperationStatusCtx = 1;

#define PTDBG_TRACE_ROUTINES            0x00000001
#define PTDBG_TRACE_OPERATION_STATUS    0x00000002

ULONG gTraceFlags = 0;


#define PT_DBG_PRINT( _dbgLevel, _string )          \
    (FlagOn(gTraceFlags,(_dbgLevel)) ?              \
        DbgPrint _string :                          \
        ((int)0))

/*************************************************************************
    Prototypes
*************************************************************************/

EXTERN_C_START

DRIVER_DISPATCH DelProtectCreateClose, DelProtectDeviceControl;
DRIVER_UNLOAD DelProtectUnload;

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

NTSTATUS
FsFilterProtectorInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

VOID
FsFilterProtectorInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

VOID
FsFilterProtectorInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

NTSTATUS
FsFilterProtectorUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
FsFilterProtectorInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
FsFilterProtectorPreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

FLT_PREOP_CALLBACK_STATUS
DelProtectPreCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

FLT_PREOP_CALLBACK_STATUS
DelProtectPreSetInformation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

VOID
FsFilterProtectorOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    );

FLT_POSTOP_CALLBACK_STATUS
FsFilterProtectorPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
FsFilterProtectorPreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

BOOLEAN
FsFilterProtectorDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    );

EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, FsFilterProtectorUnload)
#pragma alloc_text(PAGE, FsFilterProtectorInstanceQueryTeardown)
#pragma alloc_text(PAGE, FsFilterProtectorInstanceSetup)
#pragma alloc_text(PAGE, FsFilterProtectorInstanceTeardownStart)
#pragma alloc_text(PAGE, FsFilterProtectorInstanceTeardownComplete)
#endif

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

    { IRP_MJ_CREATE, 
      0, 
      DelProtectPreCreate, 
      NULL},

    { IRP_MJ_SET_INFORMATION, 
      0, 
      DelProtectPreSetInformation, 
      NULL},

#if 0 // TODO - List all of the requests to filter.
    { IRP_MJ_CREATE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_CLOSE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_READ,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_WRITE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_SET_INFORMATION,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_SET_EA,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_DIRECTORY_CONTROL,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      FsFilterProtectorPreOperationNoPostOperation,
      NULL },                               //post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_CLEANUP,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_PNP,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_MDL_READ,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      FsFilterProtectorPreOperation,
      FsFilterProtectorPostOperation },

#endif // TODO

    { IRP_MJ_OPERATION_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags

    NULL,                               //  Context
    Callbacks,                          //  Operation callbacks

    FsFilterProtectorUnload,                           //  MiniFilterUnload

    FsFilterProtectorInstanceSetup,                    //  InstanceSetup
    FsFilterProtectorInstanceQueryTeardown,            //  InstanceQueryTeardown
    FsFilterProtectorInstanceTeardownStart,            //  InstanceTeardownStart
    FsFilterProtectorInstanceTeardownComplete,         //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent

};



NTSTATUS
FsFilterProtectorInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Routine Description:

    This routine is called whenever a new instance is created on a volume. This
    gives us a chance to decide if we need to attach to this volume or not.

    If this routine is not defined in the registration structure, automatic
    instances are always created.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Flags describing the reason for this attach request.

Return Value:

    STATUS_SUCCESS - attach
    STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeDeviceType );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorInstanceSetup: Entered\n") );

    return STATUS_SUCCESS;
}


NTSTATUS
FsFilterProtectorInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This is called when an instance is being manually deleted by a
    call to FltDetachVolume or FilterDetach thereby giving us a
    chance to fail that detach request.

    If this routine is not defined in the registration structure, explicit
    detach requests via FltDetachVolume or FilterDetach will always be
    failed.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Indicating where this detach request came from.

Return Value:

    Returns the status of this operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorInstanceQueryTeardown: Entered\n") );

    return STATUS_SUCCESS;
}


VOID
FsFilterProtectorInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the start of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorInstanceTeardownStart: Entered\n") );
}


VOID
FsFilterProtectorInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the end of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorInstanceTeardownComplete: Entered\n") );
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT driverObject,
    _In_ PUNICODE_STRING registryPath
    )
/*++

Routine Description:

    This is the initialization routine for this miniFilter driver.  This
    registers with FltMgr and initializes all global data structures.

Arguments:

    DriverObject - Pointer to driver object created by the system to
        represent this driver.

    RegistryPath - Unicode string identifying where the parameters for this
        driver are located in the registry.

Return Value:

    Routine can return non success error codes.

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER( registryPath );

    RtlZeroMemory(g_data.DirNames, sizeof(g_data.DirNames));
    g_data.mutex.Init();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterDelProtector!DriverEntry: Entered\n") );

	PDEVICE_OBJECT deviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\del_protect");
	bool bSymLinkCreated = false;

	do {
		UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\DelProtect");
		status = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, TRUE, &deviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device 0x%08X\n", status));
			break;
		}

		deviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &deviceName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create symbolic link 0x%08X", status));
			break;
		}

		bSymLinkCreated = true;

		driverObject->DriverUnload = DelProtectUnload;
		driverObject->MajorFunction[IRP_MJ_CREATE] = DelProtectCreateClose;
		driverObject->MajorFunction[IRP_MJ_CLOSE] = DelProtectCreateClose;
		driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DelProtectDeviceControl;

		//
		//  Register with FltMgr to tell it our callback routines
		//

		status = FltRegisterFilter( driverObject,
									&FilterRegistration,
									&gFilterHandle );

		FLT_ASSERT( NT_SUCCESS( status ) );

		if (NT_SUCCESS( status )) {

			//
			//  Start filtering i/o
			//

			status = FltStartFiltering( gFilterHandle );

			if (!NT_SUCCESS( status )) {

				FltUnregisterFilter( gFilterHandle );
			}
		}
	} while (false);

	if (!NT_SUCCESS(status)) {
		if (bSymLinkCreated)
			IoDeleteSymbolicLink(&symLink);

		if (deviceObject)
			IoDeleteDevice(deviceObject);

        if (gFilterHandle)
			FltUnregisterFilter(gFilterHandle);
	}

    return status;
}

void DelProtectUnload(PDRIVER_OBJECT DriverObject) {
	ClearDirectories();
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\del_protect");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS DelProtectCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DelProtectDeviceControl(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_DELPROTECT_ADD_DIR:
	{
		auto name = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		if (!name) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		auto bufferLen = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (bufferLen > 1024) {
			// just too long for a directory
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		// make sure there is a NULL terminator somewhere
		name[bufferLen / sizeof(WCHAR) - 1] = L'\0';

		auto dosNameLen = ::wcslen(name);
		if (dosNameLen < 3) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		LockRAII locker(g_data.mutex);
		UNICODE_STRING strName;
		RtlInitUnicodeString(&strName, name);
		if (FindDirectory(&strName, true) >= 0) {
			break;
		}

		if (g_data.dirNamesCount== g_maxDirectories) {
			status = STATUS_TOO_MANY_NAMES;
			break;
		}

		for (int i = 0; i < g_maxDirectories; i++) {
			if (g_data.DirNames[i].DosName.Buffer == nullptr) {
				auto len = (dosNameLen + 1) * sizeof(WCHAR);
				auto buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, len, DRIVER_TAG);
				if (!buffer) {
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}
				::wcscpy_s(buffer, len / sizeof(WCHAR), name);

				status = ConvertDosNameToNtName(buffer, &g_data.DirNames[i].NtName);
				if (!NT_SUCCESS(status)) {
					ExFreePool(buffer);
					break;
				}

				RtlInitUnicodeString(&g_data.DirNames[i].DosName, buffer);
				KdPrint((DRIVER_PREFIX "Add: %wZ <=> %wZ\n", &g_data.DirNames[i].DosName, &g_data.DirNames[i].NtName));
				++g_data.dirNamesCount;
				break;
			}
		}
		break;
	}

	case IOCTL_DELPROTECT_REMOVE_DIR:
	{
		auto name = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		if (!name) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		auto bufferLen = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (bufferLen > 1024) {
			// just too long for a directory
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		// make sure there is a NULL terminator somewhere
		name[bufferLen / sizeof(WCHAR) - 1] = L'\0';

		auto dosNameLen = ::wcslen(name);
		if (dosNameLen < 3) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		LockRAII locker(g_data.mutex);
		UNICODE_STRING strName;
		RtlInitUnicodeString(&strName, name);
		int found = FindDirectory(&strName, true);
		if (found >= 0) {
			g_data.DirNames[found].Free();
			--g_data.dirNamesCount;
		}
		else {
			status = STATUS_NOT_FOUND;
		}
		break;
	}

	case IOCTL_DELPROTECT_CLEAR_DIR:
		ClearDirectories();
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;

}

int FindDirectory(PCUNICODE_STRING name, bool dosName) {
	if (g_data.dirNamesCount == 0)
		return -1;

	for (int i = 0; i < g_maxDirectories; i++) {
		const auto& dir = dosName ? g_data.DirNames[i].DosName : g_data.DirNames[i].NtName;
		if (dir.Buffer && wcsstr(name->Buffer, dir.Buffer) != 0)
			return i;
	}
	return -1;
}

bool IsDeleteAllowedByDirectories(PFLT_CALLBACK_DATA data) {

	PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
	bool allow = true;

	do {
		auto status = FltGetFileNameInformation(data,
			FLT_FILE_NAME_QUERY_DEFAULT | FLT_FILE_NAME_NORMALIZED, &nameInfo);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "FltGetFileNameInformation failed"));
			break;
		}

		status = FltParseFileNameInformation(nameInfo);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "FltParseFileNameInformation failed"));
			break;
		}

		UNICODE_STRING path;
		path.Length = path.MaximumLength = nameInfo->Volume.Length + nameInfo->Share.Length + nameInfo->ParentDir.Length;
		path.Buffer = nameInfo->Volume.Buffer;

		LockRAII lock(g_data.mutex);
        if (FindDirectory(&path, false) >= 0) {
            allow = false;
            KdPrint((DRIVER_PREFIX "File not allowed to delete: %wZ\n", &nameInfo->Name));
        }
    } while (false);

    if (nameInfo)
        FltReleaseFileNameInformation(nameInfo);

	return allow;
}

NTSTATUS ConvertDosNameToNtName(PCWSTR dosName, PUNICODE_STRING ntName) {

    ntName->Buffer = nullptr;
    auto dosNameLen = wcslen(dosName);

    if (dosNameLen < 3)
        return STATUS_BUFFER_TOO_SMALL;

    if (dosName[2] != L'\\' || dosName[1] != L':')
        return STATUS_INVALID_PARAMETER;

    int symLinkLen = 1024;
    WCHAR* symLink = (WCHAR*)ExAllocatePoolWithTag(PagedPool, symLinkLen, DRIVER_TAG);
    if (!symLink) {
        KdPrint((DRIVER_PREFIX "ExAllocatePoolWithTag failed"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    wcscpy_s(symLink, symLinkLen / sizeof(WCHAR), L"\\??\\");
    WCHAR volume[3] = { dosName[0] , dosName[1], L'\0' };
    wcscat_s(symLink, symLinkLen / sizeof(WCHAR), volume);

    UNICODE_STRING symLinkFull;
    RtlInitUnicodeString(&symLinkFull, symLink);
    
    OBJECT_ATTRIBUTES objAttri;
    InitializeObjectAttributes(&objAttri, &symLinkFull, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE hSymLink = NULL;
    auto status = STATUS_SUCCESS;

    do {
        status = ZwOpenSymbolicLinkObject(&hSymLink, GENERIC_READ, &objAttri);
        if (!NT_SUCCESS(status)) {
            KdPrint((DRIVER_PREFIX "ZwOpenSymbolicLinkObject falied"));
            break;
        }

        USHORT maxLen = 1024;
        ntName->Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, maxLen, DRIVER_TAG);
        if (!ntName->Buffer) {
            KdPrint((DRIVER_PREFIX "ExAllocatePoolWithTag failed"));
            break;
        }

        ntName->MaximumLength = maxLen;

        status = ZwQuerySymbolicLinkObject(hSymLink, ntName, NULL);
        if (!NT_SUCCESS(status)) {
            KdPrint((DRIVER_PREFIX "ZwQuerySymbolicLinkObject failed"));
            break;
        }
    } while (false);

    if (!NT_SUCCESS(status)) {
        if (ntName->Buffer) {
            ExFreePool(ntName->Buffer);
            ntName->Buffer = nullptr;
        }
    }
    else {
        RtlAppendUnicodeToString(ntName, dosName + 2);
    }

    if (hSymLink)
        ZwClose(hSymLink);

    if (symLink)
        ExFreePool(symLink);
    
    return status;
}

NTSTATUS
FsFilterProtectorUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++

Routine Description:

    This is the unload routine for this miniFilter driver. This is called
    when the minifilter is about to be unloaded. We can fail this unload
    request if this is not a mandatory unload indicated by the Flags
    parameter.

Arguments:

    Flags - Indicating if this is a mandatory unload.

Return Value:

    Returns STATUS_SUCCESS.

--*/
{
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorUnload: Entered\n") );

    FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}


/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
FsFilterProtectorPreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorPreOperation: Entered\n") );

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (FsFilterProtectorDoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    FsFilterProtectorOperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                          ("FsFilterProtector!FsFilterProtectorPreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
DelProtectPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->RequestorMode == KernelMode)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    const auto& params = Data->Iopb->Parameters.Create;
    if (!(params.Options & FILE_DELETE_ON_CLOSE))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    KdPrint((DRIVER_PREFIX "delete on close: %wZ\n", &Data->Iopb->TargetFileObject->FileName));

    auto bOk = IsDeleteAllowedByProcess(NtCurrentProcess());
    if (bOk)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    bOk = IsDeleteAllowedByDirectories(Data);
    if (bOk)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

	Data->IoStatus.Status = STATUS_ACCESS_DENIED;
    KdPrint((DRIVER_PREFIX "prevent create deletion"));
    return FLT_PREOP_COMPLETE;
}

FLT_PREOP_CALLBACK_STATUS
DelProtectPreSetInformation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
	UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (Data->RequestorMode == KernelMode)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

    auto& params = Data->Iopb->Parameters.SetFileInformation;
    if (params.FileInformationClass != FileDispositionInformation
        && 
        params.FileInformationClass != FileDispositionInformationEx)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    auto info = (FILE_DISPOSITION_INFORMATION*)params.InfoBuffer;
    if (!info->DeleteFile)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    auto process = PsGetThreadProcess(Data->Thread);
    NT_ASSERT(process);

	HANDLE hProcess;
	NTSTATUS status = ObOpenObjectByPointer(process, OBJ_KERNEL_HANDLE, NULL, 0, *PsProcessType, KernelMode, &hProcess);
    if (!NT_SUCCESS(status)) {
        KdPrint((DRIVER_PREFIX "ObOpenObjectByPointer failed "));
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    auto bOk = IsDeleteAllowedByProcess(hProcess);
    ZwClose(hProcess);

    if (bOk)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    bOk = IsDeleteAllowedByDirectories(Data);
    if (bOk)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    KdPrint((DRIVER_PREFIX "prevent SetFileInformation deletion"));
	Data->IoStatus.Status = STATUS_ACCESS_DENIED;
    return FLT_PREOP_COMPLETE;
}

void ClearDirectories() {
	LockRAII locker(g_data.mutex);
	for (int i = 0; i < g_maxDirectories; i++) {
		if (g_data.DirNames[i].DosName.Buffer) {
			ExFreePool(g_data.DirNames[i].DosName.Buffer);
			g_data.DirNames[i].DosName.Buffer = nullptr;
		}
		if (g_data.DirNames[i].NtName.Buffer) {
			ExFreePool(g_data.DirNames[i].NtName.Buffer);
			g_data.DirNames[i].NtName.Buffer = nullptr;
		}
	}
	g_data.dirNamesCount= 0;
}

VOID
FsFilterProtectorOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    )
/*++

Routine Description:

    This routine is called when the given operation returns from the call
    to IoCallDriver.  This is useful for operations where STATUS_PENDING
    means the operation was successfully queued.  This is useful for OpLocks
    and directory change notification operations.

    This callback is called in the context of the originating thread and will
    never be called at DPC level.  The file object has been correctly
    referenced so that you can access it.  It will be automatically
    dereferenced upon return.

    This is non-pageable because it could be called on the paging path

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    RequesterContext - The context for the completion routine for this
        operation.

    OperationStatus -

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorOperationStatusCallback: Entered\n") );

    PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                  ("FsFilterProtector!FsFilterProtectorOperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
FsFilterProtectorPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Routine Description:

    This routine is the post-operation completion routine for this
    miniFilter.

    This is non-pageable because it may be called at DPC level.

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The completion context set in the pre-operation routine.

    Flags - Denotes whether the completion is successful or is being drained.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorPostOperation: Entered\n") );

    return FLT_POSTOP_FINISHED_PROCESSING;
}


FLT_PREOP_CALLBACK_STATUS
FsFilterProtectorPreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("FsFilterProtector!FsFilterProtectorPreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
FsFilterProtectorDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    )
/*++

Routine Description:

    This identifies those operations we want the operation status for.  These
    are typically operations that return STATUS_PENDING as a normal completion
    status.

Arguments:

Return Value:

    TRUE - If we want the operation status
    FALSE - If we don't

--*/
{
    PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

    //
    //  return boolean state based on which operations we are interested in
    //

    return (BOOLEAN)

            //
            //  Check for oplock operations
            //

             (((iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) &&
               ((iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_FILTER_OPLOCK)  ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_BATCH_OPLOCK)   ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_1) ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_2)))

              ||

              //
              //    Check for directy change notification
              //

              ((iopb->MajorFunction == IRP_MJ_DIRECTORY_CONTROL) &&
               (iopb->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY))
             );
}
