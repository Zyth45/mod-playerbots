/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ20ACTIONCONTEXT_H
#define PLAYERBOTS_AQ20ACTIONCONTEXT_H

#include "Action.h"
#include "Aq20Actions.h"
#include "NamedObjectContext.h"

class RaidAq20ActionContext : public NamedObjectContext<Action>
{
public:
    RaidAq20ActionContext()
    {
        creators["aq20 use crystal"] = &RaidAq20ActionContext::use_crystal;
        creators["aq20 buru avoid devastate"] = &RaidAq20ActionContext::buru_avoid_devastate;
        creators["aq20 ossirian shelter"] = &RaidAq20ActionContext::ossirian_shelter;
    }

private:
    static Action* use_crystal(PlayerbotAI* ai) { return new Aq20UseCrystalAction(ai); }
    static Action* buru_avoid_devastate(PlayerbotAI* ai) { return new Aq20BuruAvoidDevastateAction(ai); }
    static Action* ossirian_shelter(PlayerbotAI* ai) { return new Aq20OssirianShelterAction(ai); }
};

#endif
