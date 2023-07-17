#pragma once

#define IOCTL_PTMON_ADD_EXECUTABLE CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PTMON_REMOVE_EXECUTABLE CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PTMON_CLEAR CTL_CODE(0x8000, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad,
	RegPostSetValue,
};

struct ItemHeader {
	ItemType type;
	ULONG size;
	LARGE_INTEGER time;
};

struct ProcessExitInfo : ItemHeader {
	ULONG processId;
};

struct ProcessCreateInfo : ItemHeader {
	ULONG processId;
	ULONG parentProcessId;
	USHORT cmdLineLength;
	USHORT cmdLineOffset;
	USHORT imageFileNameLength;
	USHORT imageFileNameOffset;
};

struct ThreadCreateInfo : ItemHeader {
	ULONG threadId;
	ULONG processId;
	ULONG remoteProcessId;
	USHORT remoteProcessNameLength;
	USHORT remoteProcessNameOffset;
	bool remoteParentProcess;
};

struct ThreadExitInfo : ItemHeader {
	ULONG threadId;
	ULONG processId;
};

struct ImageLoadInfo : ItemHeader {
	ULONG processId;
	PVOID imageBase;
	ULONG imageFileNameLength;
	ULONG imageFileNameOffset;
};

struct RegPostSetValueInfo : ItemHeader {
	ULONG processId;
	ULONG threadId;
	USHORT keyNameLength;
	USHORT keyNameOffset;
	USHORT valueNameLength;
	USHORT valueNameOffset;
	ULONG dataLength;
	USHORT dataOffset;
	ULONG dataType;
};
