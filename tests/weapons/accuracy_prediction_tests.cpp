#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
enum { FL_ONGROUND=1, FL_DUCKING=2, MOVETYPE_LADDER=2, Primary_Mode=0,
       FIELD_FLOAT=1, FIELD_BOOLEAN=2, FTYPEDESC_OVERRIDE=4, FTYPEDESC_NOERRORCHECK=8 };
bool FBitSet(int flags, int mask) { return (flags & mask) != 0; }
float tickInterval = 0.015f;
#define TICK_INTERVAL tickInterval
float Lerp(float t, float a, float b) { return a + (b-a)*t; }
const float kJumpVelocity = std::sqrt(2.0f*800.0f*57.0f);
struct CCSPlayer {
    int flags = FL_ONGROUND, moveType = 0;
    int GetMoveType() const { return moveType; }
    int GetFlags() const { return flags; }
};
struct CCSWeaponInfo {
    float m_fInaccuracyStand[1] = {0.02f}, m_fInaccuracyLadder[1] = {0.2f};
    float m_fInaccuracyCrouch[1] = {0.01f}, m_fInaccuracyReload = 0.1f;
    float m_fRecoveryTimeStand = 0.5f, m_fRecoveryTimeCrouch = 0.4f;
    float m_fInaccuracyJump[1] = {0.3f}, m_fInaccuracyLand[1] = {0.2f};
};
struct CWeaponCSBase {
    CCSPlayer player;
    CCSWeaponInfo info;
    bool m_bInReload = false, m_bDelayFire = false;
    float m_flTimeWeaponIdle = 0, m_flNextPrimaryAttack = 0, m_flNextSecondaryAttack = 0;
    float m_flAccuracy = 0.9f, m_flDecreaseShotsFired = 0, m_fAccuracyPenalty = 0;
    CCSPlayer *GetPlayerOwner() { return &player; }
    const CCSWeaponInfo &GetCSWpnData() { return info; }
    void UpdateAccuracyPenalty();
    void OnJump(float);
    void OnLand(float);
};
#include "accuracy_methods.inc"
struct Field { std::size_t offset, size; };
#define DEFINE_PRED_FIELD(name, type, flags) {offsetof(CWeaponCSBase,name), sizeof(((CWeaponCSBase*)0)->name)}
const Field fields[] = {
#include "accuracy_fields.inc"
};
#undef DEFINE_PRED_FIELD
struct SavedField { std::size_t offset; std::vector<unsigned char> bytes; };
struct Snapshot {
    std::vector<SavedField> saved;
    explicit Snapshot(const CWeaponCSBase &w) {
        for (const Field &f : fields) {
            const unsigned char *p = reinterpret_cast<const unsigned char *>(&w) + f.offset;
            saved.push_back({f.offset, std::vector<unsigned char>(p,p+f.size)});
        }
    }
    void restore(CWeaponCSBase &w) const {
        for (const SavedField &f : saved)
            std::memcpy(reinterpret_cast<unsigned char *>(&w)+f.offset,f.bytes.data(),f.bytes.size());
    }
};
int main(int argc, char **) {
    unsigned cases=0, divergent=0;
    for (float tick : {0.015f,0.01f,0.0078125f})
    for (int stance=0;stance<4;++stance)
    for (bool reload : {false,true})
    for (float penalty : {0.0f,0.03f,0.5f,1.0f})
    for (int scenario=0;scenario<6;++scenario) {
        tickInterval=tick;
        CWeaponCSBase w;
        w.player.flags=stance==0 ? 0 : FL_ONGROUND | (stance==2 ? FL_DUCKING : 0);
        w.player.moveType=stance==3 ? MOVETYPE_LADDER : 0;
        w.m_bInReload=reload;w.m_fAccuracyPenalty=penalty;
        Snapshot before(w);
        auto run=[&]() {
            if (scenario==1 || scenario==4) w.OnJump(kJumpVelocity);
            if (scenario==2 || scenario==5) w.OnLand(kJumpVelocity*0.8f);
            if (scenario!=3) w.UpdateAccuracyPenalty();
            if (scenario==3) w.OnJump(kJumpVelocity*0.5f);
        };
        run();float expected=w.m_fAccuracyPenalty;
        for (int repeat=0;repeat<8;++repeat) {
            before.restore(w);run();++cases;
            if (w.m_fAccuracyPenalty!=expected) ++divergent;
        }
    }
    if ((argc>1 && !divergent) || (argc==1 && divergent)) {
        std::fprintf(stderr,"Unexpected accuracy replay result: %u/%u\n",divergent,cases);return 1;
    }
    std::printf("%s shared accuracy: replay checks=%u divergent=%u; extracted methods, modeled snapshots\n",
                argc>1 ? "BASELINE DEFECT REPRODUCED" : "PASS",cases,divergent);
}
