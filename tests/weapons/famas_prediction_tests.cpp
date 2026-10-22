// Extracted production weapon methods; stubbed engine services and snapshot copy.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

struct Globals { float curtime; } globals;
Globals *gpGlobals = &globals;
struct Angle { float x; };
Angle operator+(Angle a, Angle b) { return {a.x + b.x}; }
Angle operator*(float scale, Angle a) { return {scale * a.x}; }
struct Velocity { float value; float Length2D() const { return value; } };
enum { FL_ONGROUND = 1, FL_DUCKING = 2, HUD_PRINTCENTER = 0,
       Primary_Mode = 0, Secondary_Mode = 1, PLAYER_ATTACK1 = 0,
       ACT_VM_PRIMARYATTACK = 0, ACT_VM_SECONDARYATTACK = 1,
       FIELD_BOOLEAN = 1, FIELD_INTEGER = 2, FIELD_FLOAT = 3,
       FTYPEDESC_INSENDTABLE = 0x100 };
bool FBitSet(int flags, int mask) { return (flags & mask) != 0; }
struct CBaseEntity { static int GetPredictionRandomSeed() { return 173; } };
struct CCSPlayer {
    int m_iShotsFired = 0, flags = FL_ONGROUND;
    Velocity velocity = {0};
    int GetFlags() const { return flags; }
    Velocity GetAbsVelocity() const { return velocity; }
    int entindex() const { return 1; }
    Angle Weapon_ShootPosition() const { return {0}; }
    Angle EyeAngles() const { return {0}; }
    Angle GetPunchAngle() const { return {0}; }
    void DoMuzzleFlash() {}
    void SetAnimation(int) {}
    int GetAmmoCount(int) const { return 120; }
    void SetSuitUpdate(const char *, bool, int) {}
};
void ClientPrint(CCSPlayer *, int, const char *) {}
void Error(const char *message) { throw std::runtime_error(message); }
struct AccuracyModel { int value = 1; int GetInt() const { return value; } } weapon_accuracy_model;
struct Shot {
    float inaccuracy, spread;
    int mode, seed;
    bool operator==(const Shot &s) const {
        return inaccuracy == s.inaccuracy && spread == s.spread && mode == s.mode && seed == s.seed;
    }
};
std::vector<Shot> effects;
void FX_FireBullets(int, Angle, Angle, int, int mode, int seed, float inaccuracy, float spread) {
    effects.push_back({inaccuracy, spread, mode, seed});
}
struct CWeaponCSBase {
    float GetInaccuracy() const { return 0.03f; }
    void ItemPostFrame() {} // only the derived-class call is exercised here
};
struct WeaponInfo { float m_flCycleTime = 0.15f; };
class CWeaponFamas : public CWeaponCSBase {
public:
    typedef CWeaponCSBase BaseClass;
    CCSPlayer player;
    bool m_bBurstMode = false, m_bFireOnEmpty = true;
    int m_iFamasShotsFired = 0, m_iClip1 = 20, m_iPrimaryAmmoType = 0;
    float m_flFamasShoot = 0, m_fBurstSpread = 0.03f, m_flAccuracy = 0.2f;
    float m_flNextPrimaryAttack = 0, m_flNextSecondaryAttack = 0, idle = 0;
    CCSPlayer *GetPlayerOwner() const { return const_cast<CCSPlayer *>(&player); }
    WeaponInfo GetCSWpnData() const { return WeaponInfo(); }
    int GetWeaponID() const { return 2; }
    void PlayEmptySound() {}
    void SendWeaponAnim(int) {}
    void SetWeaponIdleTime(float value) { idle = value; }
    void SecondaryAttack();
    void PrimaryAttack();
    float GetInaccuracy() const;
    void FireRemaining(int &, float &);
    void ItemPostFrame();
    void GlockFire(float, bool);
};
#include "famas_methods.inc"
struct Field { std::size_t offset, size; const char *name; int type, flags; };
#define DEFINE_PRED_FIELD(name, type, flags) \
    {offsetof(CWeaponFamas, name), sizeof(((CWeaponFamas *)0)->name), #name, type, flags}
const Field fields[] = {
    {0, 0, "sentinel", 0, 0},
#include "famas_fields.inc"
};
#undef DEFINE_PRED_FIELD
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct SavedField { std::size_t offset; std::vector<unsigned char> bytes; };
struct Snapshot {
    CCSPlayer player;
    float accuracy, nextPrimary, nextSecondary, idle;
    int clip;
    std::vector<SavedField> local;
    explicit Snapshot(const CWeaponFamas &w)
        : player(w.player), accuracy(w.m_flAccuracy), nextPrimary(w.m_flNextPrimaryAttack),
          nextSecondary(w.m_flNextSecondaryAttack), idle(w.idle), clip(w.m_iClip1) {
        for (const Field &f : fields) {
            if (!f.size) continue;
            const unsigned char *p = reinterpret_cast<const unsigned char *>(&w) + f.offset;
            local.push_back({f.offset, std::vector<unsigned char>(p, p + f.size)});
        }
    }
    void restore(CWeaponFamas &w) const {
        // Model inherited predicted state separately from the derived prediction map.
        w.player = player; w.m_flAccuracy = accuracy; w.m_iClip1 = clip;
        w.m_flNextPrimaryAttack = nextPrimary; w.m_flNextSecondaryAttack = nextSecondary; w.idle = idle;
        for (const SavedField &f : local)
            std::memcpy(reinterpret_cast<unsigned char *>(&w) + f.offset, f.bytes.data(), f.bytes.size());
    }
};
bool same(const CWeaponFamas &a, const CWeaponFamas &b) {
    return a.m_bBurstMode == b.m_bBurstMode && a.m_iFamasShotsFired == b.m_iFamasShotsFired
        && a.m_flFamasShoot == b.m_flFamasShoot && a.m_fBurstSpread == b.m_fBurstSpread
        && a.m_flAccuracy == b.m_flAccuracy && a.m_iClip1 == b.m_iClip1
        && a.player.m_iShotsFired == b.player.m_iShotsFired
        && a.m_flNextPrimaryAttack == b.m_flNextPrimaryAttack
        && a.m_flNextSecondaryAttack == b.m_flNextSecondaryAttack && a.idle == b.idle;
}
int main(int argc, char **) {
    try {
        const bool expectDefect = argc > 1;
        unsigned cases = 0, divergent = 0;
        for (int accuracyModel : {0, 1})
        for (int state = 0; state < 4; ++state)
        for (int clip : {0, 1, 2, 3, 25})
        for (int shots : {1, 2, 3})
        for (float timer : {0.0f, 99.9f, 100.05f})
        for (bool mode : {false, true})
        for (int scenario = 0; scenario < 2; ++scenario) {
            weapon_accuracy_model.value = accuracyModel;
            CWeaponFamas w;
            w.player.flags = state == 0 ? 0 : FL_ONGROUND | (state == 2 ? FL_DUCKING : 0);
            w.player.velocity.value = state == 3 ? 180 : 0;
            w.m_iClip1 = clip; w.m_iFamasShotsFired = shots;
            w.m_flFamasShoot = timer; w.m_bBurstMode = mode;
            Snapshot before(w);
            auto run = [&]() {
                globals.curtime = 100.0f;
                if (scenario == 0) w.SecondaryAttack();
                else { w.ItemPostFrame(); globals.curtime += 0.11f; w.ItemPostFrame(); }
            };
            effects.clear(); run();
            const CWeaponFamas expected = w;
            const std::vector<Shot> expectedEffects = effects;
            for (int repeat = 0; repeat < 4; ++repeat) {
                // Model a later burst having changed its cached spread before rollback.
                w.m_fBurstSpread = 0.5f;
                before.restore(w); effects.clear(); run(); ++cases;
                if (!same(w, expected) || effects != expectedEffects) ++divergent;
            }
        }
        if (expectDefect) require(divergent > 0, "negative control failed");
        else {
            require(sizeof(fields)/sizeof(fields[0]) == 5, "four predicted FAMAS fields required");
            require(divergent == 0, "FAMAS replay differs after restore");
        }
        std::printf("%s FAMAS: replay checks=%u divergent=%u; extracted methods, modeled snapshot copy\n",
                    expectDefect ? "BASELINE DEFECT REPRODUCED" : "PASS", cases, divergent);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
