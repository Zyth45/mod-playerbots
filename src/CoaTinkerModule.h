/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_TINKER_MODULE_H
#define COA_TINKER_MODULE_H

#include <cstdint>

// "Only one Module per Tinker can be active" (Mana Module 803658-803664, Greater Mana Module 803665, Power Module
// 706742, 707346-707350, 707688, Greater Power Module 680315; jealous-sound #11164, as on the live realm): a Tinker bot
// that put Mana Module on one ally and Power Module on another took the first off with the second, saw it missing and
// put it back, for ever (Yeh, Discord, 07/11). Each Tinker bot now keeps one kind for the group, apart from the game
// objects so that tests/coa_tinker_module.cpp can check it.
enum class CoaModule : uint8_t
{
    None,
    Mana,
    Power
};

// The kind this Tinker keeps: Mana Module when the group has mana casters or healers besides the Tinkers, else Power
// Module. With several Tinkers, the second (in group order) takes the other kind, the third the first again, and so
// on. A Tinker that knows only one kind keeps that one.
inline CoaModule CoaModuleChoice(bool groupHasManaUsers, uint32_t tinkerIndex, bool knowsMana, bool knowsPower)
{
    if (!knowsMana && !knowsPower)
        return CoaModule::None;
    CoaModule const preferred = groupHasManaUsers ? CoaModule::Mana : CoaModule::Power;
    CoaModule const other = preferred == CoaModule::Mana ? CoaModule::Power : CoaModule::Mana;
    CoaModule const pick = tinkerIndex % 2 ? other : preferred;
    bool const knowsPick = pick == CoaModule::Mana ? knowsMana : knowsPower;
    return knowsPick ? pick : (knowsMana ? CoaModule::Mana : CoaModule::Power);
}

// Whether an ally gains from a single target Module of this kind: mana regeneration to a mana caster or healer,
// attack power to anyone else.
inline bool CoaModuleProfits(CoaModule kind, bool manaCasterOrHealer)
{
    return kind == CoaModule::Mana ? manaCasterOrHealer : kind == CoaModule::Power && !manaCasterOrHealer;
}

#endif
