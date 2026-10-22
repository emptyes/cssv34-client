// SDK descriptors + independent typed-copy roundtrip, not engine prediction loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include "knife_sdk_test_types.h"
#include "datamap.h"
#include "tier0/memdbgoff.h"
static unsigned copies;
static void require(bool ok) { if (!ok) { std::fprintf(stderr, "descriptor/roundtrip failure\n"); std::exit(1); } }
// Kept outside template wrappers so small scalar-only layouts are not diagnosed
// as handles by speculative inlining. These are called only for FIELD_EHANDLE.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void setHandle(void *p, int value) {
    *static_cast<EHANDLE *>(p) = EHANDLE(value + 1, value + 17);
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void copyHandle(void *to, const void *from) {
    *static_cast<EHANDLE *>(to) = *static_cast<const EHANDLE *>(from);
}
template<class T> void testDescriptors(typedescription_t *fields, std::size_t count) {
    std::set<std::string> names;
    for (std::size_t i = 1; i < count; ++i) {
        typedescription_t &d = fields[i];
        require(names.insert(d.fieldName).second);
        const std::size_t bytes = d.fieldType == FIELD_BOOLEAN ? sizeof(bool) :
            d.fieldType == FIELD_INTEGER ? sizeof(int) : d.fieldType == FIELD_FLOAT ? sizeof(float) : d.fieldType == FIELD_VECTOR ? sizeof(Vector) :
            d.fieldType == FIELD_SHORT ? sizeof(short) : d.fieldType == FIELD_EHANDLE ? sizeof(EHANDLE) : 0;
        require(bytes != 0 && bytes == static_cast<std::size_t>(d.fieldSizeInBytes) && d.fieldSize == 1);
        require(d.fieldOffset[0] >= 0 && d.fieldOffset[0] + bytes <= sizeof(T));
        for (int value = 0; value < 8; ++value) {
            T source = {}, target = {};
            unsigned char *s = reinterpret_cast<unsigned char *>(&source) + d.fieldOffset[0];
            unsigned char *t = reinterpret_cast<unsigned char *>(&target) + d.fieldOffset[0];
            if (d.fieldType == FIELD_BOOLEAN) { bool v = value % 2; std::memcpy(s, &v, bytes); }
            else if (d.fieldType == FIELD_INTEGER) { int v = value * 179 - 538; std::memcpy(s, &v, bytes); }
            else if (d.fieldType == FIELD_EHANDLE) {
                setHandle(s, value);
            }
            else if (d.fieldType == FIELD_VECTOR) { Vector v(value, value * -0.5f, value * 2.5f); std::memcpy(s, &v, bytes); }
            else if (d.fieldType == FIELD_SHORT) { unsigned short v = 0x8000u + value; std::memcpy(s, &v, bytes); }
            else { float v = value * 0.25f - 0.5f; std::memcpy(s, &v, bytes); }
            if (d.fieldType == FIELD_EHANDLE)
                copyHandle(t, s);
            else std::memcpy(t, s, bytes);
            require(std::memcmp(t, s, bytes) == 0);
            ++copies;
        }
    }
}
#include "weapon_maps.inc"
int main() {
#include "weapon_calls.inc"
    std::printf("PASS SDK typed descriptors and roundtrip copies=%u\n", copies);
}
