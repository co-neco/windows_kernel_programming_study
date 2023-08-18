#include "DevMonManager.h"

void DevMonManager::Init(PDRIVER_OBJECT driverObject, PDEVICE_OBJECT deviceObject) {

	_driverObject = driverObject;
	_controlDeviceObject = deviceObject;

	_devicesCount = 0;
	RtlZeroMemory(_devices, sizeof(_devices));

	_mutex.Init();
}

NTSTATUS DevMonManager::AddDevice(PCWSTR devName) {

	LockRAII locker(_mutex);

	if (_devicesCount == g_maxMonitoredDevices)
		return STATUS_TOO_MANY_NAMES;

	if (FindDevice(devName) >= 0)
		return STATUS_SUCCESS;

	int index = 0;
	for (; index < _devicesCount; ++index) {
		if (!_devices[index].deviceObject)
			break;
	}

	NT_ASSERT(index != g_maxMonitoredDevices);

	UNICODE_STRING targetDevName;
	RtlInitUnicodeString(&targetDevName, devName);

	PFILE_OBJECT fileObject;
	PDEVICE_OBJECT lowerDeviceObject = nullptr;
	
	auto status = IoGetDeviceObjectPointer(&targetDevName, FILE_READ_DATA, &fileObject, &lowerDeviceObject);
	if (!NT_SUCCESS(status)) {
		// ws need PASSIVE_LEVEL, but why it's fine here?
		KdPrint((DRIVER_PREFIX "Failed to get device object with name: %ws(0x%08X)", devName, status));
		return status;
	}

	PDEVICE_OBJECT deviceObject = nullptr;
	WCHAR* buffer = nullptr;

	do {
		status = IoCreateDevice(_driverObject, sizeof(DeviceExtension), 
			nullptr, lowerDeviceObject->DeviceType, 0, FALSE, &deviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Failed to create device with device type: %d, status: 0x%08X", lowerDeviceObject->DeviceType, status));
			break;
		}

		deviceObject->DeviceType = lowerDeviceObject->DeviceType;
		deviceObject->Flags |= lowerDeviceObject->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO);

		auto ext = (DeviceExtension*)deviceObject->DeviceExtension;
		status = IoAttachDeviceToDeviceStackSafe(deviceObject, lowerDeviceObject, &ext->lowerDeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Failed to attach device with name: %ws(0x%08X)", devName, status));
			break;
		}

		buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, targetDevName.Length, DRIVER_TAG);
		if (!buffer) {
			KdPrint((DRIVER_PREFIX "Failed to allocate buffer for target device name: %ws", devName));
			break;
		}

		_devices[index].deviceObject = deviceObject;
		_devices[index].deviceName.Buffer = buffer;
		_devices[index].deviceName.MaximumLength = targetDevName.Length;
		RtlCopyUnicodeString(&_devices[index].deviceName, &targetDevName);
		
		_devices[index].lowerDeviceObject = ext->lowerDeviceObject;
		deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
		deviceObject->Flags |= DO_POWER_PAGABLE;

		++_devicesCount;

	} while (false);

	if (!NT_SUCCESS(status)) {
		if (buffer)
			ExFreePool(buffer);
		if (deviceObject)
			IoDeleteDevice(deviceObject);
	}
	if (lowerDeviceObject)
		ObDereferenceObject(fileObject);

	return status;
}

int DevMonManager::FindDevice(PCWSTR devName) {

	int index = -1;

	UNICODE_STRING targetDevName;
	RtlInitUnicodeString(&targetDevName, devName);

	for (int i = 0; i < _devicesCount; ++i) {
		if (!_devices[i].deviceObject)
			continue;

		if (RtlEqualUnicodeString(&_devices[i].deviceName, &targetDevName, TRUE)) {
			index = i;
			break;
		}
	}

	return index;
}

bool DevMonManager::RemoveDevice(PCWSTR devName) {

	LockRAII locker(_mutex);
	auto index = FindDevice(devName);
	if (index == -1) {
		KdPrint((DRIVER_PREFIX "No such device name: %ws", devName));
		return false;
	}

	return RemoveDevice(index);
}

void DevMonManager::RemoveAllDevices() {

	LockRAII locker(_mutex);
	if (_devicesCount == 0)
		return;

	for (int i = 0; i < _devicesCount; ++i) {
		if (_devices[i].deviceObject)
			RemoveDevice(i);
	}
}

PDEVICE_OBJECT DevMonManager::GetCDO() const {
	return _controlDeviceObject;
}

bool DevMonManager::RemoveDevice(int index) {

	auto& device = _devices[index];
	if (!device.deviceObject)
		return true;

	if (device.deviceName.Buffer)
		ExFreePool(device.deviceName.Buffer);

	IoDetachDevice(device.lowerDeviceObject);
	IoDeleteDevice(device.deviceObject);
	device.deviceObject = nullptr;

	--_devicesCount;
	return true;
}


