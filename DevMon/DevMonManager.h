#pragma once

#include <fltKernel.h>
#include "types.h"

#define DRIVER_PREFIX "DevMon"
#define DRIVER_TAG 'devM'

class FastMutex;

struct MonitoredDevice {
	UNICODE_STRING deviceName;
	PDEVICE_OBJECT deviceObject;
	PDEVICE_OBJECT lowerDeviceObject;
};

struct DeviceExtension {
	PDEVICE_OBJECT lowerDeviceObject;
};


class DevMonManager {
public:
	void Init(PDRIVER_OBJECT driverObject, PDEVICE_OBJECT deviceObject);
	NTSTATUS AddDevice(PCWSTR devName);
	int FindDevice(PCWSTR devName);
	bool RemoveDevice(PCWSTR devName);
	void RemoveAllDevices();
	MonitoredDevice& GetDevice(int index);
	PDEVICE_OBJECT GetCDO() const;

private:
	bool RemoveDevice(int index);

private:
	static const int g_maxMonitoredDevices = 32;
	
private:
	MonitoredDevice _devices[g_maxMonitoredDevices];
	int _devicesCount;
	FastMutex _mutex;
	PDRIVER_OBJECT _driverObject;
	PDEVICE_OBJECT _controlDeviceObject;
};
