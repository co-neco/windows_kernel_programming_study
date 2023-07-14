
#pragma once

extern "C" NTSTATUS ZwQueryInformationProcess(
_In_ HANDLE ProcessHandle,
_In_ PROCESSINFOCLASS ProcessInformationClass,
_Out_ PVOID ProcessInformation,
_In_ ULONG ProcessInformationLength,
_Out_opt_ PULONG ReturnLength);

class FastMutex {
public:
	inline void Init() { ExInitializeFastMutex(&_mutex); }

	inline void Lock() { ExAcquireFastMutex(&_mutex); }
	inline void Unlock() { ExReleaseFastMutex(&_mutex); }

private:
	FAST_MUTEX _mutex;
};

template <typename T>
class LockRAII {
public:
	inline LockRAII(T& lock): _lock(lock) { _lock.Lock(); }
	inline ~LockRAII() { _lock.Unlock(); }

private:
	T& _lock;
};