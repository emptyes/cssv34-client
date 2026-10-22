// SDK descriptors + independent scalar-copy roundtrip, not engine prediction loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include "datamap.h"
#include "tier0/memdbgoff.h"
static unsigned copies;
static void require(bool ok) { if (!ok) { std::fprintf(stderr, "descriptor/roundtrip failure\n"); std::exit(1); } }
template<class T> void testDescriptors(typedescription_t *fields, std::size_t count) {
    std::set<std::string> names;
    for (std::size_t i = 1; i < count; ++i) {
        typedescription_t &d = fields[i];
        require(names.insert(d.fieldName).second);
        const std::size_t bytes = d.fieldType == FIELD_BOOLEAN ? sizeof(bool) :
            d.fieldType == FIELD_INTEGER ? sizeof(int) : d.fieldType == FIELD_FLOAT ? sizeof(float) : 0;
        require(bytes != 0 && bytes == static_cast<std::size_t>(d.fieldSizeInBytes) && d.fieldSize == 1);
        require(d.fieldOffset[0] >= 0 && d.fieldOffset[0] + bytes <= sizeof(T));
        for (int value = 0; value < 8; ++value) {
            T source = {}, target = {};
            unsigned char *s = reinterpret_cast<unsigned char *>(&source) + d.fieldOffset[0];
            unsigned char *t = reinterpret_cast<unsigned char *>(&target) + d.fieldOffset[0];
            if (d.fieldType == FIELD_BOOLEAN) { bool v = value % 2; std::memcpy(s, &v, bytes); }
            else if (d.fieldType == FIELD_INTEGER) { int v = value * 179 - 538; std::memcpy(s, &v, bytes); }
            else { float v = value * 0.25f - 0.5f; std::memcpy(s, &v, bytes); }
            std::memcpy(t, s, bytes);
            require(std::memcmp(t, s, bytes) == 0);
            ++copies;
        }
    }
}
#include "weapon_maps.inc"
int main() {
#include "weapon_calls.inc"
    std::printf("PASS SDK scalar descriptors and roundtrip copies=%u\n", copies);
}
