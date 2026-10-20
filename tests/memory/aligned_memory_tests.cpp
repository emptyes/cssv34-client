#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#ifdef TEST_DEBUG
#define Assert(value) assert(value)
#else
#define Assert(value) ((void)0)
#endif
#define COMPILE_TIME_ASSERT(value) static_assert(value, #value)
#define UTLMEMORY_TRACK_ALLOC() ((void)0)
#define UTLMEMORY_TRACK_FREE() ((void)0)
#define MEM_ALLOC_CREDIT_CLASS() ((void)0)
static void *MemAlloc_AllocAligned(size_t size, size_t alignment) {
    void *memory = nullptr;
    if (posix_memalign(&memory, std::max(alignment, sizeof(void *)), size)) throw std::bad_alloc();
    return memory;
}
static void MemAlloc_FreeAligned(void *memory) { std::free(memory); }
#define _aligned_malloc MemAlloc_AllocAligned
inline void *MemAlloc_ReallocAligned(void *memory, size_t size, size_t alignment) {
    if (memory) throw std::runtime_error("unexpected owned reallocation");
    return MemAlloc_AllocAligned(size, alignment);
}
inline int UtlMemory_CalcNewAllocationCount(int, int, int requested, int) { return requested; }
template <class T> class CUtlMemory {
public:
    enum { EXTERNAL_BUFFER_MARKER = -1, EXTERNAL_CONST_BUFFER_MARKER = -2 };
    T *m_pMemory = nullptr;
    int m_nAllocationCount = 0, m_nGrowSize = 0;
    bool IsExternallyAllocated() const { return m_nGrowSize < 0; }
    bool IsReadOnly() const { return m_nGrowSize == EXTERNAL_CONST_BUFFER_MARKER; }
    void ValidateGrowSize() {}
    T *Base() { return m_pMemory; }
    int NumAllocated() const { return m_nAllocationCount; }
};
#include "memory_under_test.inc"
static void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template <class T, int alignment> static void Check(T *pointer, int count) {
    const size_t address = reinterpret_cast<size_t>(pointer);
    const size_t aligned = (address + alignment - 1) & ~(size_t(alignment) - 1);
    const size_t padding = aligned - address;
    const size_t bytes = size_t(count) * sizeof(T);
    const int expected = bytes > padding ? int((bytes - padding) / sizeof(T)) : 0;
    auto check = [&](auto &memory, bool readonly) {
        Require(reinterpret_cast<size_t>(memory.Base()) == aligned, "alignment");
        Require(memory.NumAllocated() == expected && memory.NumAllocated() <= count, "external capacity");
        Require(memory.IsReadOnly() == readonly && memory.IsExternallyAllocated(), "external ownership");
    };
    CUtlMemoryAligned<T, alignment> mutableMemory(pointer, count); check(mutableMemory, false);
    CUtlMemoryAligned<T, alignment> constMemory(static_cast<const T *>(pointer), count); check(constMemory, true);
    CUtlMemoryAligned<T, alignment> mutableAttached(0, 32);
    mutableAttached.SetExternalBuffer(pointer, count); check(mutableAttached, false);
    CUtlMemoryAligned<T, alignment> constAttached(0, 32);
    constAttached.SetExternalBuffer(static_cast<const T *>(pointer), count); check(constAttached, true);
}
struct ThreeBytes { unsigned char value[3]; };
int main() {
    if (sizeof(void *) == 8)
        Check<unsigned char, 32>(reinterpret_cast<unsigned char *>(uintptr_t(0x17ffffff1ULL)), 1);
    alignas(64) unsigned char memory[2048] = {};
    size_t cases = 0;
    for (int offset = 0; offset < 64; ++offset) for (int count = 0; count <= 65; ++count) {
        Check<unsigned char, 16>(memory + offset, count);
        Check<unsigned char, 32>(memory + offset, count);
        Check<ThreeBytes, 32>(reinterpret_cast<ThreeBytes *>(memory + offset), count);
        ++cases;
    }
    for (int offset = 0; offset < 16; ++offset) for (int count : {0, 1, 2, 3, 32, 127}) {
        Check<uint32_t, 32>(reinterpret_cast<uint32_t *>(memory + offset * 4), count); ++cases;
    }
    if (sizeof(void *) == 8) {
        for (uintptr_t address : {uintptr_t(0x17ffffff1ULL), uintptr_t(0x1fffffff1ULL), uintptr_t(0x500000003ULL)}) {
            for (int count : {0, 1, 15, 16, 31, 64}) {
                Check<unsigned char, 32>(reinterpret_cast<unsigned char *>(address), count); ++cases;
            }
        }
    }
    Check<unsigned char, 16>(nullptr, 0);
    std::cout << "PASS aligned external memory cases: " << cases + 1 << '\n';
}
