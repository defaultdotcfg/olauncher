#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

// ============================================================================
// ContextAllocator Allocation Tracker
// Dedicated, thread-safe, non-allocating open-addressing tracker table.
// Stores (ptr -> allocated_size) in static BSS memory without invoking malloc,
// operator new, or CRT heap, completely eliminating recursive allocator calls.
// ============================================================================

namespace AllocTracker
{
    struct Entry
    {
        void* ptr;
        size_t size;
    };

    static const size_t kCapacity = 131072; // 128K entries (2MB table), power of 2
    static Entry s_table[kCapacity] = {};
    static SRWLOCK s_lock = SRWLOCK_INIT;
    static volatile LONG s_count = 0;

    static inline size_t Hash(void* p)
    {
        uintptr_t v = reinterpret_cast<uintptr_t>(p);
        v ^= v >> 16;
        v *= 0x45d9f3b;
        v ^= v >> 16;
        return static_cast<size_t>(v);
    }

    static inline void Record(void* ptr, size_t size)
    {
        if (!ptr) return;
        AcquireSRWLockExclusive(&s_lock);
        size_t mask = kCapacity - 1;
        size_t idx = Hash(ptr) & mask;
        for (size_t i = 0; i < kCapacity; ++i)
        {
            size_t pos = (idx + i) & mask;
            if (s_table[pos].ptr == nullptr)
            {
                s_table[pos].ptr = ptr;
                s_table[pos].size = size;
                InterlockedIncrement(&s_count);
                break;
            }
            else if (s_table[pos].ptr == ptr)
            {
                s_table[pos].size = size;
                break;
            }
        }
        ReleaseSRWLockExclusive(&s_lock);
    }

    static inline bool Lookup(void* ptr, size_t* outSize)
    {
        if (!ptr || !outSize) return false;
        AcquireSRWLockShared(&s_lock);
        size_t mask = kCapacity - 1;
        size_t idx = Hash(ptr) & mask;
        bool found = false;
        for (size_t i = 0; i < kCapacity; ++i)
        {
            size_t pos = (idx + i) & mask;
            if (s_table[pos].ptr == ptr)
            {
                *outSize = s_table[pos].size;
                found = true;
                break;
            }
            if (s_table[pos].ptr == nullptr)
            {
                break;
            }
        }
        ReleaseSRWLockShared(&s_lock);
        return found;
    }

    static inline void Reset()
    {
        AcquireSRWLockExclusive(&s_lock);
        memset(s_table, 0, sizeof(s_table));
        s_count = 0;
        ReleaseSRWLockExclusive(&s_lock);
    }

    static inline LONG GetCount()
    {
        return s_count;
    }
}
