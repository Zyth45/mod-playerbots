/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_MOUNT_RULE_H
#define PLAYERBOTS_COA_MOUNT_RULE_H

// When the mount check (CheckMountStateAction) may run at all, with no game types for the unit test.
//
// CoA lets a player ride before AiPlayerbot.UseGroundMountAtMinLevel, and an alt below that level rides along while
// its master rides. The check used to be allowed below that level only while the master was mounted: once the master
// got off to fight, a mounted alt lost the very check that dismounts it, and stayed in the saddle on top of the enemy
// without attacking until it looted (Discord, RandomGnome). A mounted bot may always run the check, to get off.
namespace CoaMountRule
{
    inline bool CheckAllowed(unsigned botLevel, unsigned minMountLevel, bool botMounted, bool followsMountedMaster)
    {
        return botLevel >= minMountLevel || botMounted || followsMountedMaster;
    }

    // An alt with a master gets off when the master is on foot (dismounted, or fighting on foot).
    inline bool DismountForMaster(bool masterMounted, bool botMounted)
    {
        return botMounted && !masterMounted;
    }
}

#endif
