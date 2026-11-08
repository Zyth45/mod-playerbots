/*
 * CoA World: the three invasions, as data.
 *
 * Creatures, spells, sites and reward factions come from wow-mod-era-events by buildthehomelab
 * (MIT License, see CoaWorldInvasion.h), and were each checked against the CoA data (world DB
 * 2.9.3 and client 2026-10-30): every creature, spell, gameobject, faction and item below exists
 * on CoA. Health and damage modifiers replace the templates' WotLK raid values (Pit Commander x52,
 * Nokhor x150) with open-world ones, at the level of the run.
 *
 * Coordinates: Legion and Ahn'Qiraj sites are era-events' (checked against CoA game_tele). The
 * capital gates come from CoA's own guard spawns (Stormwind City Guard 68, Orgrimmar Grunt 3296).
 *
 * Part of mod-playerbots (GPL-2.0-or-later).
 */

#include "CoaWorldInvasion.h"

#include "Common.h"
#include "SharedDefines.h"

namespace CoaWorld
{
    namespace
    {
        constexpr uint32 MAP_EASTERN_KINGDOMS = 0;
        constexpr uint32 MAP_KALIMDOR = 1;

        enum Creatures : uint32
        {
            // Scourge
            NPC_PALLID_HORROR         = 16394,
            NPC_PATCHWORK_TERROR      = 16382,
            NPC_FLAMESHOCKER          = 16383,
            NPC_LUMBERING_HORROR      = 14697,
            NPC_SPIRIT_OF_THE_DAMNED  = 16379,
            NPC_BONE_WITCH            = 16380,
            NPC_GHOUL_BERSERKER       = 16141,
            NPC_SPECTRAL_SOLDIER      = 16298,
            NPC_SKELETAL_SHOCKTROOPER = 16299,
            // Legion
            NPC_INVADING_FELGUARD     = 19284,
            NPC_INVADING_INFERNAL     = 19285,
            NPC_INVADING_FEL_STALKER  = 19286,
            NPC_INVADING_VOIDWALKER   = 19287,
            NPC_INVADING_ANGUISHER    = 19290,
            NPC_DREADLORD             = 8716,
            NPC_PIT_COMMANDER         = 18945,
            // Ahn'Qiraj
            NPC_KALDOREI_INFANTRY     = 15423,
            NPC_QIRAJI_WASP           = 15414,
            NPC_QIRAJI_TANK           = 15422,
            NPC_ANUBISATH_CONQUEROR   = 15424,
            NPC_NOKHOR                = 15818,
        };

        enum Spells : uint32
        {
            SPELL_MINION_SPAWN_IN       = 28234,
            SPELL_AURA_OF_FEAR          = 28313,
            SPELL_FLAMESHOCKERS_TOUCH   = 28314,
            SPELL_FLAMESHOCKERS_TOUCH2  = 28329,
            SPELL_FLAMESHOCKERS_REVENGE = 28323,
            SPELL_STUNNING_STRIKE       = 16790,
            SPELL_TRAMPLE               = 5568,
            SPELL_RIBBON_OF_SOULS       = 16243,
            SPELL_PSYCHIC_SCREAM        = 22884,
            SPELL_ARCANE_BOLT           = 13748,
            SPELL_INFECTED_BITE         = 7367,
            SPELL_ENRAGE                = 8599,
            SPELL_DEMORALIZING_SHOUT    = 16244,
            SPELL_SUNDER_ARMOR          = 21081,
            SPELL_BONE_SHARDS           = 17014,
            SPELL_CLEAVE                = 15284,
            SPELL_WAR_STOMP             = 16727,
            SPELL_KNOCK_AWAY            = 10101,
            SPELL_IMMOLATION            = 19483,
            SPELL_MANA_BURN             = 11981,
            SPELL_SHADOW_BOLT           = 20825,
            SPELL_SHADOW_VOLLEY         = 17228,
            SPELL_SLEEP                 = 12098,
            SPELL_THUNDERCLAP           = 15548,
            SPELL_REND                  = 16509,
            SPELL_STRIKE                = 11976,
        };

        constexpr uint32 GO_FEL_PORTAL = 183283;  // Infernaling Summoner Visual

        constexpr uint32 FACTION_ARGENT_DAWN = 529;
        constexpr uint32 FACTION_CENARION_CIRCLE = 609;
        constexpr uint32 ITEM_NECROTIC_RUNE = 22484;
    }

    std::vector<InvasionPlan> const& GetInvasionPlans()
    {
        static std::vector<InvasionPlan> const plans = {
            {
                "scourge", "Scourge Invasion", "Scourge", 20, 0,
                {
                    { "Stormwind", "Elwynn Forest", MAP_EASTERN_KINGDOMS, 1519, TEAM_ALLIANCE,
                        { -9200.0f, 330.0f, 84.0f, 0.75f }, { -8958.0f, 509.0f, 96.7f, 0.70f },
                        { -8888.2f, 566.2f, 93.3f, 2.20f } },
                    { "Orgrimmar", "Durotar", MAP_KALIMDOR, 1637, TEAM_HORDE,
                        { 1310.0f, -4385.0f, 26.5f, 0.00f }, { 1480.0f, -4417.0f, 25.6f, 0.00f },
                        { 1629.9f, -4373.6f, 31.6f, 3.70f } },
                },
                { NPC_GHOUL_BERSERKER, NPC_SPECTRAL_SOLDIER, NPC_SKELETAL_SHOCKTROOPER },
                { NPC_LUMBERING_HORROR, NPC_SPIRIT_OF_THE_DAMNED, NPC_BONE_WITCH }, 2,
                NPC_FLAMESHOCKER, 3,
                { NPC_PALLID_HORROR, NPC_PATCHWORK_TERROR }, NPC_FLAMESHOCKER, 4,
                0, 0,
                5, 60 * IN_MILLISECONDS, 15 * IN_MILLISECONDS, 3, 20,
                FACTION_ARGENT_DAWN, ITEM_NECROTIC_RUNE, 5,
                "The dead are gathering on the road to {0}. The Scourge will march on the gates in {2} minutes!",
                "The Scourge is marching on {0}! Defend the gates.",
                "The abomination falls and the Scourge breaks before the gates of {0}.",
                "The Scourge withdraws from {0}, leaving its dead behind.",
                "Burn them! Burn them all!",
                "The Lich King sends his regards. This city will join his army!",
            },
            {
                "legion", "Legion Incursion", "Legion", 45, GO_FEL_PORTAL,
                {
                    { "Gadgetzan", "Tanaris", MAP_KALIMDOR, 440, TEAM_NEUTRAL,
                        { -7108.6f, -3829.62f, 9.62f, 3.6f }, { -7157.02f, -3755.97f, 8.47f, 0.13f },
                        { -7177.15f, -3785.34f, 8.37f, 6.10f } },
                    { "Everlook", "Winterspring", MAP_KALIMDOR, 618, TEAM_NEUTRAL,
                        { 6749.09f, -4713.45f, 721.3f, 4.33f }, { 6726.46f, -4654.4f, 720.99f, 3.56f },
                        { 6725.69f, -4619.44f, 720.91f, 4.67f } },
                    { "Cenarion Hold", "Silithus", MAP_KALIMDOR, 1377, TEAM_NEUTRAL,
                        { -6874.96f, 722.57f, 45.75f, 0.54f }, { -6817.11f, 776.11f, 47.89f, 3.89f },
                        { -6818.09f, 733.81f, 41.57f, 2.31f } },
                    { "Nethergarde Keep", "the Blasted Lands", MAP_EASTERN_KINGDOMS, 4, TEAM_NEUTRAL,
                        { -11003.2f, -3403.04f, 62.07f, 3.85f }, { -11002.2f, -3333.93f, 65.03f, 2.06f },
                        { -10999.8f, -3380.08f, 62.25f, 4.64f } },
                    { "Light's Hope Chapel", "the Eastern Plaguelands", MAP_EASTERN_KINGDOMS, 139, TEAM_NEUTRAL,
                        { 2275.17f, -5394.57f, 86.57f, 0.45f }, { 2288.8f, -5319.14f, 89.0f, 2.2f },
                        { 2279.65f, -5310.01f, 87.08f, 5.08f } },
                },
                { NPC_INVADING_FELGUARD, NPC_INVADING_INFERNAL, NPC_INVADING_FEL_STALKER, NPC_INVADING_VOIDWALKER,
                  NPC_INVADING_ANGUISHER },
                {}, 0,
                NPC_DREADLORD, 3,
                { NPC_PIT_COMMANDER }, NPC_INVADING_FELGUARD, 3,
                0, 0,
                6, 50 * IN_MILLISECONDS, 10 * IN_MILLISECONDS, 4, 24,
                FACTION_ARGENT_DAWN, 0, 0,
                "Fel energy is gathering outside {0} in {1}. The Burning Legion will break through in {2} minutes!",
                "A fel portal has torn open outside {0} in {1}! Demons are marching on the town.",
                "The Pit Commander is slain and the portal outside {0} collapses.",
                "The portal outside {0} fades. The Legion has taken what it came for.",
                "Your world is ripe for conquest. The Legion has come to harvest it!",
                "Kneel before the Burning Legion! Every one of you will burn!",
            },
            {
                "aq", "Gates of Ahn'Qiraj", "AhnQiraj", 55, 0,
                {
                    { "the Scarab Wall", "Silithus", MAP_KALIMDOR, 1377, TEAM_NEUTRAL,
                        { -8112.0f, 1524.0f, 2.70f, 0.0f }, { -8078.0f, 1522.0f, 2.61f, 3.14159f },
                        { -8045.0f, 1532.0f, 2.61f, 3.14159f } },
                },
                { NPC_QIRAJI_WASP, NPC_QIRAJI_TANK },
                { NPC_ANUBISATH_CONQUEROR }, 3,
                0, 0,
                { NPC_NOKHOR }, NPC_ANUBISATH_CONQUEROR, 2,
                NPC_KALDOREI_INFANTRY, 12,
                6, 50 * IN_MILLISECONDS, 15 * IN_MILLISECONDS, 4, 24,
                FACTION_CENARION_CIRCLE, 0, 0,
                "The Scarab Wall trembles in {1}. The Qiraji swarm will pour out in {2} minutes!",
                "The gates of Ahn'Qiraj are open! The swarm is storming the Kaldorei line at {0}.",
                "Lieutenant General Nokhor falls and the swarm retreats behind {0}.",
                "The swarm falls back behind {0}. The Kaldorei count their dead.",
                "",
                "The Qiraji will not be denied. Crush them!",
            },
        };
        return plans;
    }

    void RegisterInvasionMobs()
    {
        // Scourge
        RegisterMob({ .entry = NPC_GHOUL_BERSERKER,
            .spells = { { SPELL_INFECTED_BITE, 13000, 18000, SpellTarget::Victim } },
            .spawnVisual = SPELL_MINION_SPAWN_IN, .enrageSpell = SPELL_ENRAGE, .enragePct = 30, .seekEnemies = true });
        RegisterMob({ .entry = NPC_SPECTRAL_SOLDIER,
            .spells = { { SPELL_DEMORALIZING_SHOUT, 20000, 25000, SpellTarget::Self },
                        { SPELL_SUNDER_ARMOR, 6000, 10000, SpellTarget::Victim } },
            .spawnVisual = SPELL_MINION_SPAWN_IN, .seekEnemies = true });
        RegisterMob({ .entry = NPC_SKELETAL_SHOCKTROOPER,
            .spells = { { SPELL_BONE_SHARDS, 16000, 20000, SpellTarget::Victim } },
            .spawnVisual = SPELL_MINION_SPAWN_IN, .seekEnemies = true });
        RegisterMob({ .entry = NPC_LUMBERING_HORROR, .levelOffset = 1,
            .spells = { { SPELL_STUNNING_STRIKE, 10000, 15000, SpellTarget::Victim },
                        { SPELL_TRAMPLE, 8000, 12000, SpellTarget::Self } },
            .seekEnemies = true });
        RegisterMob({ .entry = NPC_SPIRIT_OF_THE_DAMNED, .levelOffset = 1,
            .spells = { { SPELL_RIBBON_OF_SOULS, 5000, 8000, SpellTarget::RandomEnemy },
                        { SPELL_PSYCHIC_SCREAM, 20000, 30000, SpellTarget::Self } },
            .seekEnemies = true });
        RegisterMob({ .entry = NPC_BONE_WITCH, .levelOffset = 1,
            .spells = { { SPELL_ARCANE_BOLT, 4000, 6000, SpellTarget::Victim } },
            .seekEnemies = true });
        RegisterMob({ .entry = NPC_FLAMESHOCKER, .healthMod = 3.0f, .damageMod = 2.0f,
            .spells = { { SPELL_FLAMESHOCKERS_TOUCH, 30000, 45000, SpellTarget::Victim },
                        { SPELL_FLAMESHOCKERS_TOUCH2, 30000, 45000, SpellTarget::Victim } },
            .deathSpell = SPELL_FLAMESHOCKERS_REVENGE, .seekEnemies = true });
        for (uint32 entry : { NPC_PALLID_HORROR, NPC_PATCHWORK_TERROR })
            RegisterMob({ .entry = entry, .healthMod = 20.0f, .damageMod = 4.0f, .levelOffset = 2,
                .spells = { { SPELL_STUNNING_STRIKE, 15000, 20000, SpellTarget::Victim },
                            { SPELL_TRAMPLE, 10000, 14000, SpellTarget::Self } },
                .aura = SPELL_AURA_OF_FEAR, .seekEnemies = true });

        // Legion
        RegisterMob({ .entry = NPC_INVADING_FELGUARD,
            .spells = { { SPELL_CLEAVE, 6000, 9000, SpellTarget::Victim } }, .seekEnemies = true });
        RegisterMob({ .entry = NPC_INVADING_INFERNAL, .aura = SPELL_IMMOLATION, .seekEnemies = true });
        RegisterMob({ .entry = NPC_INVADING_FEL_STALKER,
            .spells = { { SPELL_MANA_BURN, 10000, 15000, SpellTarget::RandomEnemy } }, .seekEnemies = true });
        RegisterMob({ .entry = NPC_INVADING_VOIDWALKER, .seekEnemies = true });
        RegisterMob({ .entry = NPC_INVADING_ANGUISHER,
            .spells = { { SPELL_SHADOW_BOLT, 3000, 5000, SpellTarget::Victim } }, .seekEnemies = true });
        RegisterMob({ .entry = NPC_DREADLORD, .healthMod = 6.0f, .damageMod = 4.0f, .levelOffset = 1,
            .spells = { { SPELL_SHADOW_VOLLEY, 10000, 14000, SpellTarget::Self },
                        { SPELL_SLEEP, 20000, 25000, SpellTarget::RandomEnemy } },
            .seekEnemies = true });
        RegisterMob({ .entry = NPC_PIT_COMMANDER, .healthMod = 25.0f, .damageMod = 6.0f, .levelOffset = 2,
            .spells = { { SPELL_CLEAVE, 6000, 9000, SpellTarget::Victim },
                        { SPELL_WAR_STOMP, 14000, 18000, SpellTarget::Self },
                        { SPELL_KNOCK_AWAY, 18000, 24000, SpellTarget::Victim } },
            .seekEnemies = true });

        // Ahn'Qiraj
        RegisterMob({ .entry = NPC_KALDOREI_INFANTRY, .healthMod = 3.0f,
            .spells = { { SPELL_STRIKE, 6000, 9000, SpellTarget::Victim } }, .seekEnemies = true });
        RegisterMob({ .entry = NPC_QIRAJI_WASP, .healthMod = 1.5f, .seekEnemies = true });
        RegisterMob({ .entry = NPC_QIRAJI_TANK, .healthMod = 2.0f,
            .spells = { { SPELL_SUNDER_ARMOR, 6000, 10000, SpellTarget::Victim } }, .seekEnemies = true });
        RegisterMob({ .entry = NPC_ANUBISATH_CONQUEROR, .healthMod = 4.0f, .damageMod = 3.0f, .levelOffset = 1,
            .spells = { { SPELL_CLEAVE, 6000, 9000, SpellTarget::Victim },
                        { SPELL_THUNDERCLAP, 12000, 16000, SpellTarget::Self } },
            .seekEnemies = true });
        RegisterMob({ .entry = NPC_NOKHOR, .healthMod = 25.0f, .damageMod = 6.0f, .levelOffset = 2,
            .spells = { { SPELL_CLEAVE, 6000, 9000, SpellTarget::Victim },
                        { SPELL_THUNDERCLAP, 12000, 16000, SpellTarget::Self },
                        { SPELL_KNOCK_AWAY, 18000, 24000, SpellTarget::Victim },
                        { SPELL_REND, 10000, 14000, SpellTarget::RandomEnemy } },
            .seekEnemies = true });
    }
}
