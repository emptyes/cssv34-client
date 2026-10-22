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
typedef Angle QAngle;
Angle operator+(Angle a, Angle b) { return {a.x + b.x}; }
Angle operator*(float scale, Angle a) { return {scale * a.x}; }
struct Velocity { float value; float Length2D() const { return value; } };
enum { FL_ONGROUND = 1, FL_DUCKING = 2, HUD_PRINTCENTER = 0,
       Primary_Mode = 0, Secondary_Mode = 1, PLAYER_ATTACK1 = 0,
       ACT_VM_PRIMARYATTACK = 0, ACT_VM_SECONDARYATTACK = 1,
       ACT_VM_DETACH_SILENCER = 3, ACT_VM_ATTACH_SILENCER = 4,
       FIELD_BOOLEAN = 1, FIELD_INTEGER = 2, FIELD_FLOAT = 3,
       FTYPEDESC_INSENDTABLE = 0x100 };
bool FBitSet(int flags, int mask) { return (flags & mask) != 0; }
struct CBaseEntity { static int GetPredictionRandomSeed() { return 173; } };
struct CCSPlayer {
    int m_iShotsFired = 0, flags = FL_ONGROUND;
    Angle punch = {0};
    Velocity velocity = {0};
    int GetFlags() const { return flags; }
    Velocity GetAbsVelocity() const { return velocity; }
    int entindex() const { return 1; }
    Angle Weapon_ShootPosition() const { return {0}; }
    Angle EyeAngles() const { return {0}; }
    Angle GetPunchAngle() const { return punch; }
    void SetPunchAngle(Angle value) { punch = value; }
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
struct CBaseCombatWeapon {};
struct CWeaponCSBase {
    float GetInaccuracy() const { return 0.03f; }
    bool Holster(CBaseCombatWeapon *) { return true; }
};
struct WeaponInfo { float m_flCycleTime = 0.15f; };
class CWeaponUSP : public CWeaponCSBase {
public:
    typedef CWeaponCSBase BaseClass;
    CCSPlayer player;
    bool m_bSilencerOn = false, m_bFireOnEmpty = true;
    int m_iClip1 = 12, model = 0;
    float m_flDoneSwitchingSilencer = 0, m_flLastFire = 99.5f, m_flAccuracy = 0.92f;
    float m_flNextPrimaryAttack = 0, m_flNextSecondaryAttack = 0, idle = 0;
    CCSPlayer *GetPlayerOwner() const { return const_cast<CCSPlayer *>(&player); }
    WeaponInfo GetCSWpnData() const { return WeaponInfo(); }
    int GetWeaponID() const { return 3; }
    int GetPrimaryAmmoType() const { return 0; }
    void PlayEmptySound() {}
    void SendWeaponAnim(int) {}
    void SetWeaponIdleTime(float value) { idle = value; }
    const char *GetWorldModel() const { return m_bSilencerOn ? "silenced" : "normal"; }
    void SetWeaponModelIndex(const char *name) { model = std::strcmp(name, "silenced") == 0; }
    void SecondaryAttack();
    void PrimaryAttack();
    float GetInaccuracy() const;
    bool Holster(CBaseCombatWeapon *);
    void USPFire(float);
};
#include "usp_methods.inc"
struct Field { std::size_t offset, size; const char *name; int type, flags; };
#define DEFINE_PRED_FIELD(name, type, flags) \
    {offsetof(CWeaponUSP, name), sizeof(((CWeaponUSP *)0)->name), #name, type, flags}
#define DEFINE_PRED_FIELD_TOL(name, type, flags, tolerance) DEFINE_PRED_FIELD(name, type, flags)
const Field fields[] = {
    {0, 0, "sentinel", 0, 0},
#include "usp_fields.inc"
};
#undef DEFINE_PRED_FIELD
#undef DEFINE_PRED_FIELD_TOL
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct SavedField { std::size_t offset; std::vector<unsigned char> bytes; };
struct Snapshot {
    CCSPlayer player;
    float accuracy, nextPrimary, nextSecondary, idle;
    int clip, model;
    std::vector<SavedField> local;
    explicit Snapshot(const CWeaponUSP &w)
        : player(w.player), accuracy(w.m_flAccuracy), nextPrimary(w.m_flNextPrimaryAttack),
          nextSecondary(w.m_flNextSecondaryAttack), idle(w.idle), clip(w.m_iClip1), model(w.model) {
        for (const Field &f : fields) {
            if (!f.size) continue;
            const unsigned char *p = reinterpret_cast<const unsigned char *>(&w) + f.offset;
            local.push_back({f.offset, std::vector<unsigned char>(p, p + f.size)});
        }
    }
    void restore(CWeaponUSP &w) const {
        w.player = player; w.m_flAccuracy = accuracy; w.m_iClip1 = clip; w.model = model;
        w.m_flNextPrimaryAttack = nextPrimary; w.m_flNextSecondaryAttack = nextSecondary; w.idle = idle;
        for (const SavedField &f : local)
            std::memcpy(reinterpret_cast<unsigned char *>(&w) + f.offset, f.bytes.data(), f.bytes.size());
    }
};
bool same(const CWeaponUSP &a, const CWeaponUSP &b) {
    return a.m_bSilencerOn == b.m_bSilencerOn && a.m_flDoneSwitchingSilencer == b.m_flDoneSwitchingSilencer
        && a.m_flLastFire == b.m_flLastFire && a.m_flAccuracy == b.m_flAccuracy && a.m_iClip1 == b.m_iClip1
        && a.player.m_iShotsFired == b.player.m_iShotsFired && a.player.punch.x == b.player.punch.x && a.model == b.model
        && a.m_flNextPrimaryAttack == b.m_flNextPrimaryAttack
        && a.m_flNextSecondaryAttack == b.m_flNextSecondaryAttack && a.idle == b.idle;
}
int main(int argc, char **) {
    try {
        const bool expectDefect = argc > 1;
        unsigned cases = 0, divergent = 0;
        for (int accuracyModel : {0, 1})
        for (int state = 0; state < 4; ++state)
        for (float interval : {0.1f, 0.2f, 0.3f, 0.5f, 2.0f})
        for (int clip : {0, 1, 2, 3, 12})
        for (bool silenced : {false, true})
        for (int scenario = 0; scenario < 4; ++scenario) {
            weapon_accuracy_model.value = accuracyModel;
            CWeaponUSP w;
            w.player.flags = state == 0 ? 0 : FL_ONGROUND | (state == 2 ? FL_DUCKING : 0);
            w.player.velocity.value = state == 3 ? 50 : 0;
            w.m_flLastFire = 100.0f - interval; w.m_iClip1 = clip;
            w.m_bSilencerOn = silenced;
            w.m_flDoneSwitchingSilencer = scenario == 3 ? 101.0f : 99.5f;
            Snapshot before(w);
            auto run = [&]() {
                globals.curtime = 100.0f;
                if (scenario == 0) w.PrimaryAttack();
                else if (scenario == 1) w.SecondaryAttack();
                else w.Holster(nullptr);
            };
            effects.clear(); run();
            const CWeaponUSP expected = w;
            const std::vector<Shot> expectedEffects = effects;
            for (int repeat = 0; repeat < 8; ++repeat) {
                // A later command may have changed the switch timer before rollback.
                globals.curtime = 110.0f; w.SecondaryAttack();
                before.restore(w); effects.clear(); run(); ++cases;
                if (!same(w, expected) || effects != expectedEffects) ++divergent;
            }
        }
        if (expectDefect) require(divergent > 0, "negative control failed");
        else {
            require(sizeof(fields)/sizeof(fields[0]) == 4, "three predicted USP fields required");
            require(divergent == 0, "USP replay differs after restore");
        }
        std::printf("%s USP: replay checks=%u divergent=%u; extracted methods, modeled snapshot copy\n",
                    expectDefect ? "BASELINE DEFECT REPRODUCED" : "PASS", cases, divergent);
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
