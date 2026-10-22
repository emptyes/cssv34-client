#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "knife_sdk_test_types.h"
#include "datamap.h"
#include "tier0/memdbgoff.h"

static void require(bool ok) { if (!ok) { std::fprintf(stderr, "knife assertion failed\n"); std::exit(1); } }
class C_BaseEntity : public IHandleEntity {
public:
    CBaseHandle handle;
    bool player;
    QAngle angles;
    C_BaseEntity(int slot, int serial, bool p): handle(slot, serial), player(p), angles(2,4,6) {}
    void SetRefEHandle(const CBaseHandle &h) override { handle = h; }
    const CBaseHandle &GetRefEHandle() const override { return handle; }
    bool IsPlayer() const { return player; }
    QAngle GetAbsAngles() const { return angles; }
};
struct Globals { float curtime; } globals;
static Globals *gpGlobals = &globals;
static const int DMG_SLASH = 4;
struct CPASAttenuationFilter { explicit CPASAttenuationFilter(void *) {} void UsePredictionRules() {} };
struct CPASFilter { explicit CPASFilter(Vector) {} };
struct CEffectData {
    Vector m_vOrigin, m_vStart;
    QAngle m_vAngles;
    int m_nSurfaceProp = 0, m_nDamageType = 0, m_nHitBox = 0, m_fFlags = 0;
    CBaseHandle m_hEntity;
};
struct Record {
    std::string sound, effect;
    CEffectData data;
    int sounds = 0, effects = 0;
};
static Record record;
struct TE {
    void DispatchEffect(CPASFilter &, float delay, const Vector &origin, const char *name, const CEffectData &data) {
        require(delay == 0 && origin == data.m_vOrigin);
        ++record.effects; record.effect = name; record.data = data;
    }
} testTE;
static TE *te = &testTE;
struct BaseClass { void ItemPostFrame() {} };
class CKnife : public BaseClass {
public:
    trace_t m_trHit;
    EHANDLE m_pTraceHitEnt;
    float m_flSmackTime = 0;
    bool m_bStab;
    C_BaseEntity *owner = NULL;
    CKnife();
    void Smack();
    void ItemPostFrame();
    C_BaseEntity *GetPlayerOwner() { return owner; }
    int entindex() const { return 5; }
    void EmitSound(CPASAttenuationFilter &, int index, const char *name) {
        require(index == 5); ++record.sounds; record.sound = name;
    }
};
#include "knife_methods.inc"
static typedescription_t *fields(std::size_t &count) {
    typedef CKnife classNameTypedef;
    static typedescription_t f[] = {
        { FIELD_VOID, 0, {0,0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f },
#include "knife_fields.inc"
    };
    count = sizeof(f)/sizeof(f[0]); return f;
}
struct Snapshot { std::vector<std::vector<unsigned char>> values; EHANDLE hitEntity; };
// Independent typed snapshot double, using actual production descriptors/SDK handles.
static Snapshot save(CKnife &k) {
    Snapshot s; s.hitEntity = k.m_pTraceHitEnt; std::size_t count; auto f = fields(count);
    for (std::size_t i = 1; i < count; ++i) {
        auto p = reinterpret_cast<unsigned char *>(&k) + f[i].fieldOffset[0];
        s.values.emplace_back(p, p + f[i].fieldSizeInBytes);
    }
    return s;
}
static void restore(CKnife &k, const Snapshot &s, const char *omit = NULL) {
    std::size_t count; auto f = fields(count);
    for (std::size_t i = 1; i < count; ++i) {
        if (omit && std::strcmp(omit, f[i].fieldName) == 0) continue;
        auto p = reinterpret_cast<unsigned char *>(&k) + f[i].fieldOffset[0];
        if (f[i].fieldType == FIELD_EHANDLE) {
            *reinterpret_cast<EHANDLE *>(p) = s.hitEntity;
        } else std::memcpy(p, s.values[i-1].data(), f[i].fieldSizeInBytes);
    }
}
// Models only the latched result of a successful SwingOrStab, not its traces/damage.
static void latch(CKnife &k, C_BaseEntity *target, bool stab, int seed) {
    k.m_bStab = stab; k.m_pTraceHitEnt = target; k.m_trHit.m_pEnt = target;
    k.m_trHit.startpos.Init(seed + 0.5f, seed + 2.0f, seed - 3.0f);
    k.m_trHit.endpos.Init(seed * 3.0f, seed - 4.5f, seed + 1.5f);
    k.m_trHit.fraction = 0.25f;
    k.m_trHit.surface.flags = 0x8000; // Preserve unsigned high bit; not SKY.
    k.m_trHit.surface.surfaceProps = static_cast<short>(seed + 13);
    k.m_trHit.hitbox = seed + 7;
    k.m_flSmackTime = gpGlobals->curtime + (stab ? 0.2f : 0.1f);
}
static bool equal(const Record &a, const Record &b) {
    if (a.sounds != b.sounds || a.effects != b.effects || a.sound != b.sound || a.effect != b.effect) return false;
    return !a.effects || (a.data.m_vOrigin == b.data.m_vOrigin && a.data.m_vStart == b.data.m_vStart &&
        a.data.m_vAngles == b.data.m_vAngles && a.data.m_hEntity == b.data.m_hEntity &&
        a.data.m_nSurfaceProp == b.data.m_nSurfaceProp && a.data.m_nHitBox == b.data.m_nHitBox &&
        a.data.m_nDamageType == b.data.m_nDamageType && a.data.m_fFlags == b.data.m_fFlags);
}
static unsigned replay(const char *omit, bool ctorCheck) {
    unsigned mismatches = 0;
    C_BaseEntity owner(1,1,true), first(2,11,true), later(3,12,false), reused(2,13,false);
    testEntityList.slots[1] = &owner;
    for (int stab = 0; stab < 2; ++stab) for (int player = 0; player < 2; ++player)
    for (int scenario = 0; scenario < 8; ++scenario) for (int seed = 1; seed <= 6; ++seed) {
        testEntityList.slots[2] = &first; testEntityList.slots[3] = &later; first.player = player;
        CKnife k; k.owner = &owner;
        if (ctorCheck) {
            require(!k.m_bStab && !k.m_pTraceHitEnt && k.m_trHit.fraction == 1 &&
                k.m_trHit.startpos == Vector(0,0,0) && k.m_trHit.endpos == Vector(0,0,0) &&
                k.m_trHit.surface.flags == 0 && k.m_trHit.surface.surfaceProps == 0 && k.m_trHit.hitbox == 0);
        }
        globals.curtime = 10;
        latch(k, &first, stab, seed);
        if (scenario == 1) k.m_trHit.surface.flags |= SURF_SKY;
        if (scenario == 2) k.m_trHit.fraction = 1;
        if (scenario == 3) k.owner = NULL;
        if (scenario == 4) testEntityList.slots[2] = NULL;
        if (scenario == 5) testEntityList.slots[2] = &reused; // serial mismatch
        if (scenario == 6) k.m_pTraceHitEnt = static_cast<C_BaseEntity *>(NULL);
        if (scenario == 7) { k.m_trHit.surface.surfaceProps = -17; k.m_trHit.hitbox = -1; }
        Snapshot pending = save(k);
        float deadline = k.m_flSmackTime;
        globals.curtime = deadline - 0.01f;
        record = Record(); k.ItemPostFrame(); require(!record.effects && !record.sounds);
        globals.curtime = deadline;
        record = Record(); k.ItemPostFrame(); require(!record.effects && !record.sounds);
        globals.curtime = deadline + 0.01f;
        record = Record(); k.ItemPostFrame(); Record expected = record;
        bool visible = scenario == 0 || scenario == 7;
        require(expected.effects == int(visible) && expected.sounds == int(visible));
        if (visible) {
            require(expected.sound == (player ? (stab ? "Weapon_Knife.Stab" : "Weapon_Knife.Hit") : "Weapon_Knife.HitWall"));
            require(expected.effect == "KnifeSlash" && expected.data.m_fFlags == 1 && expected.data.m_nDamageType == DMG_SLASH);
            require(expected.data.m_hEntity == first.GetRefEHandle());
        }
        k.ItemPostFrame(); require(equal(record, expected)); // no second dispatch after timer consumed
        for (int repeat = 0; repeat < 8; ++repeat) {
            globals.curtime = 20; latch(k, &later, !stab, seed + 50);
            // Force all guard fields to differ as well as effect payload/target.
            k.m_trHit.fraction = scenario == 2 ? 0.5f : 1.0f;
            k.m_trHit.surface.flags = scenario == 1 ? 0x8000 : (0x8000 | SURF_SKY);
            restore(k, pending, omit);
            globals.curtime = deadline + 0.01f;
            record = Record(); k.ItemPostFrame();
            if (!equal(record, expected) || k.m_flSmackTime != -1) ++mismatches;
            Record once = record; k.ItemPostFrame(); require(equal(record, once));
        }
    }
    return mismatches;
}
int main(int argc, char **) {
    bool defect = argc > 1;
    unsigned mismatches = replay(NULL, !defect);
    std::printf("knife replay checks=1536 mismatches=%u SDK handle bytes=%zu\n", mismatches, sizeof(EHANDLE));
    if (defect) { require(mismatches > 0); std::puts("BASELINE KNIFE DEFECT REPRODUCED"); return 0; }
    require(mismatches == 0);
    const char *names[] = {"m_bStab", "m_pTraceHitEnt", "m_trHit.startpos", "m_trHit.endpos", "m_trHit.fraction",
                          "m_trHit.surface.flags", "m_trHit.surface.surfaceProps", "m_trHit.hitbox"};
    for (auto name : names) {
        auto failures = replay(name, true);
        std::printf("negative control omit=%s mismatches=%u\n", name, failures); require(failures > 0);
    }
    std::puts("PASS knife delayed impact replay and eight negative controls");
}
