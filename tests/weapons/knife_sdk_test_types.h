#ifndef KNIFE_SDK_TEST_TYPES_H
#define KNIFE_SDK_TEST_TYPES_H
// Real SDK handles/trace layout; entity lookup is a deliberately small double.
#include "basehandle.h"
#include "gametrace.h"
#include "tier0/memdbgoff.h"
class CBaseEntityList {
public:
    IHandleEntity *slots[16] = {};
    IHandleEntity *LookupEntity(const CBaseHandle &h) const {
        if (!h.IsValid() || h.GetEntryIndex() >= 16) return NULL;
        IHandleEntity *p = slots[h.GetEntryIndex()];
        return p && p->GetRefEHandle() == h ? p : NULL;
    }
};
#define ENTITYLIST_BASE_H
#include "ehandle.h"
#include "tier0/memdbgoff.h"
static CBaseEntityList testEntityList;
CBaseEntityList *g_pEntityList = &testEntityList;
typedef CHandle<C_BaseEntity> EHANDLE;
#endif
