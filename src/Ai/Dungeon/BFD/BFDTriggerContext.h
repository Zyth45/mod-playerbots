/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BFDTRIGGERCONTEXT_H
#define PLAYERBOTS_BFDTRIGGERCONTEXT_H

#include "BFDTriggers.h"
#include "NamedObjectContext.h"

class VanillaDungeonBFDTriggerContext : public NamedObjectContext<Trigger>
{
public:
    VanillaDungeonBFDTriggerContext()
    {
        creators["bfd ghamoo-ra torrent"] = &VanillaDungeonBFDTriggerContext::ghamoora_torrent;
        creators["bfd frontal cone"] = &VanillaDungeonBFDTriggerContext::frontal_cone;
    }

private:
    static Trigger* ghamoora_torrent(PlayerbotAI* ai) { return new BfdGhamooraTorrentTrigger(ai); }
    static Trigger* frontal_cone(PlayerbotAI* ai) { return new BfdFrontalConeTrigger(ai); }
};

#endif
