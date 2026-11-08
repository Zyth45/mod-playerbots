/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_ROLL_RULE_H
#define PLAYERBOTS_COA_ROLL_RULE_H

// The bots' manners at a loot roll (the user's rule), with no game types for the unit test.
//
// A bot that wants an item (an upgrade, or a raid token that gives one) only rolls Need when the realm allows it
// (AiPlayerbot.LootNeedRollLevel 2, "Bots need items"); at 1, the default, it rolls Greed; at 0, or on a unique item it
// already has, it passes. And it never rolls Need against a real player who rolled Need on the same item: it rolls
// Greed instead. A Greed is a Pass when greed rolls are off (AiPlayerbot.LootGreedRollLevel 0).
namespace CoaRollRule
{
    enum class Vote
    {
        Pass,
        Need,
        Greed
    };

    // A loot roll lasts 60 s; the wait always leaves the bot time to vote before it closes.
    inline constexpr unsigned ROLL_DURATION_MS = 60000;
    inline constexpr unsigned VOTE_MARGIN_MS = 10000;

    inline unsigned WaitCapMs(unsigned configuredMs)
    {
        return configuredMs < ROLL_DURATION_MS - VOTE_MARGIN_MS ? configuredMs : ROLL_DURATION_MS - VOTE_MARGIN_MS;
    }

    // Whether a bot that wants to roll Need holds its vote for now: only when the realm lets bots roll Need
    // (LootNeedRollLevel 2) and a real player who may roll on the item has not voted yet, for at most the wait. Every
    // other case votes at once, as before.
    inline bool HoldVote(Vote wanted, int needRollLevel, bool realPlayerStillToVote, unsigned waitedMs,
                         unsigned waitMs)
    {
        return wanted == Vote::Need && needRollLevel == 2 && realPlayerStillToVote && waitedMs < WaitCapMs(waitMs);
    }

    inline Vote Polite(Vote wanted, int needRollLevel, bool uniqueAlreadyOwned, bool realPlayerRolledNeed,
                       bool greedAllowed)
    {
        if (wanted == Vote::Need)
        {
            if (needRollLevel == 0 || uniqueAlreadyOwned)
                return Vote::Pass;
            if (needRollLevel == 1 || realPlayerRolledNeed)
                wanted = Vote::Greed;
            else
                return Vote::Need;
        }
        if (wanted == Vote::Greed && !greedAllowed)
            return Vote::Pass;
        return wanted;
    }
}

#endif
