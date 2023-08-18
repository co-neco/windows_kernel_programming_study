#pragma once

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
