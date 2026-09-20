/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChangeTalentsAction.h"
#include "AiFactory.h"
#include "mod-ascension-compat/src/AscensionSpecialization.h"
#include "AiObjectContext.h"
#include "ChatHelper.h"
#include "CoaSpecLookup.h"
#include "CoaSpecialization.h"
#include "Event.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"

#include <algorithm>
#include "RandomPlayerbotMgr.h"

bool ChangeTalentsAction::Execute(Event event)
{
    auto* flag = botAI->GetAiObjectContext()->GetValue<bool>("custom_glyphs"); // Added for custom Glyphs

    if (flag->Get()) // Added for custom Glyphs
    {
        flag->Set(false);
        LOG_INFO("playerbots", "Custom Glyph Flag set to OFF");
    }
    std::string param = event.getParam();

    std::ostringstream out;

    if (!param.empty())
    {
        if (param.find("help") != std::string::npos)
        {
            out << TalentsHelp();
        }
        else if (param.find("switch") != std::string::npos)
        {
            if (param.find("switch 1") != std::string::npos)
            {
                bot->ActivateSpec(0);
                out << "Active first talent";
                botAI->ResetStrategies();
            }
            else if (param.find("switch 2") != std::string::npos)
            {
                if (bot->GetSpecsCount() == 1 && bot->GetLevel() >= sWorld->getIntConfig(CONFIG_MIN_DUALSPEC_LEVEL))
                {
                    bot->CastSpell(bot, 63680, true, nullptr, nullptr, bot->GetGUID());
                    bot->CastSpell(bot, 63624, true, nullptr, nullptr, bot->GetGUID());
                }
                bot->ActivateSpec(1);
                out << "Active second talent";
                botAI->ResetStrategies();
            }
        }
        else if (param.find("autopick") != std::string::npos)
        {
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.InitTalentsTree(true);
            out << "Auto pick talents";
            botAI->ResetStrategies();
        }
        else if (param.find("spec list") != std::string::npos)
        {
            out << (IsCoaClass(bot) ? CoaSpecList() : SpecList());
        }
        else if (param.find("spec ") != std::string::npos)
        {
            param = param.substr(5);
            // CoA classes have no Blizzard talent tabs, so the premade spec
            // lists below are empty for them. Their specialization lives in
            // mod-ascension-compat and is what decides role, position and
            // rotation - see CoaSpecStrategies.h.
            out << (IsCoaClass(bot) ? CoaSpecPick(param) : SpecPick(param));
            botAI->ResetStrategies();
        }
        else if (param.find("apply ") != std::string::npos)
        {
            param = param.substr(6);
            out << SpecApply(param);
            botAI->ResetStrategies();
        }
        else
        {
            out << "Unknown command.";
        }
    }
    else
    {
        out << "My current talent spec is: "
            << "|h|cffffffff";

        // CoA classes have no Blizzard talent tabs: FormatClass counts those and always reports
        // "(0/0/0)". For them, the specialization mod-ascension-compat holds.
        if (CoaSpecStrategy const* coaSpec = GetCoaSpecStrategyFor(bot))
        {
            out << coaSpec->specName;
            if (!GetAscensionActiveSpecialization(bot))
                out << " (no specialization set, class default)";
            out << "\n";
        }
        else
        {
            uint32 tab = AiFactory::GetPlayerSpecTab(bot);
            out << chat->FormatClass(bot, tab) << "\n";
        }

        out << TalentsHelp();
    }

    botAI->TellMaster(out);

    return true;
}

std::string ChangeTalentsAction::TalentsHelp()
{
    std::ostringstream out;
    out << "Talents usage: talents switch <1/2>, talents autopick, talents spec list, "
           "talents spec <specName>, talents apply <link>.";
    return out.str();
}

namespace
{
// "vanguard" and "Vanguard" mean the same thing to a player typing a command.
std::string Lowered(std::string const& text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

char const* RoleWord(CoaSpecRole role)
{
    switch (role)
    {
        case CoaSpecRole::Tank: return "tank";
        case CoaSpecRole::Heal: return "healer";
        default:                return "damage";
    }
}
} // namespace

std::string ChangeTalentsAction::CoaSpecList()
{
    std::ostringstream out;
    uint32 const active = GetAscensionActiveSpecialization(bot);

    for (CoaSpecStrategy const& spec : CoaSpecStrategies)
    {
        if (spec.classId != bot->getClass())
            continue;

        out << (spec.specId == active ? "\n> " : "\n  ") << spec.specName
            << " - " << RoleWord(spec.role) << ", " << spec.position;
        if (spec.support)
            out << ", support";
    }

    return out.str();
}

// `talents spec <name>` for a CoA class, and `talents spec tank|heal|dps` for
// any spec of that role.
//
// The specialization is what decides role, position, talent plan and rotation,
// so switching it has to do all four: hand the new id to the compat module,
// which removes the spells of the old spec and grants the new ones, spend the
// talents of the new spec, and let the caller reset the strategies so
// AiFactory reads the new row.
std::string ChangeTalentsAction::CoaSpecPick(std::string const& wanted)
{
    std::string const asked = Lowered(wanted);
    std::vector<CoaSpecStrategy const*> matches;

    for (CoaSpecStrategy const& spec : CoaSpecStrategies)
    {
        if (spec.classId != bot->getClass())
            continue;

        bool const byName = Lowered(spec.specName) == asked;
        bool const byRole =
            (asked == "tank" && spec.role == CoaSpecRole::Tank) ||
            (asked == "heal" && spec.role == CoaSpecRole::Heal) ||
            (asked == "dps" && spec.role == CoaSpecRole::Dps) ||
            (asked == "support" && spec.support) ||
            (asked == "random");

        if (byName)
        {
            matches.assign(1, &spec);
            break;
        }
        if (byRole)
            matches.push_back(&spec);
    }

    if (matches.empty())
        return "I have no specialization called '" + wanted + "'. Try 'talents spec list'.";

    CoaSpecStrategy const* pick =
        matches.size() == 1 ? matches[0] : matches[urand(0, matches.size() - 1)];

if (!SwitchAscensionSpecialization(bot, pick->specId))
    return std::string("I cannot switch to ") + pick->specName + ".";

// A specialization explicitly chosen through "talents spec" must not be
// overwritten later by EnsureCoaSpecialization's deterministic random-bot role.
//
// Store the chosen spec in the random-bot event store so it survives
// logout/restart and can be reapplied if required.
if (sRandomPlayerbotMgr.IsRandomBot(bot))
    sRandomPlayerbotMgr.SetValue(bot, "coa_manual_spec", pick->specId);

// Talents follow the CoA talent path.
ApplyCoaTalents(bot);

    // Talents follow the coa talent path: the new specialization's level build, recorded through
    // SetAscensionTalentRank like a player's purchase (random bots only, as for every other pick).
    ApplyCoaTalents(bot);

    std::ostringstream out;
    out << "Now " << pick->specName << " - " << RoleWord(pick->role) << ", "
        << pick->position << ", running " << pick->combat << ".";
    return out.str();
}

std::string ChangeTalentsAction::SpecList()
{
    int cls = bot->getClass();
    int specFound = 0;
    std::ostringstream out;
    for (int specNo = 0; specNo < MAX_SPECNO; ++specNo)
    {
        if (sPlayerbotAIConfig.premadeSpecName[cls][specNo].size() == 0)
        {
            break;
        }
        specFound++;
        std::ostringstream out;
        std::vector<std::vector<uint32>> parsed = sPlayerbotAIConfig.parsedSpecLinkOrder[cls][specNo][80];
        std::unordered_map<int, int> tabCount;
        tabCount[0] = tabCount[1] = tabCount[2] = 0;
        for (auto& item : parsed)
        {
            tabCount[item[0]] += item[3];
        }
        out << specFound << ". " << sPlayerbotAIConfig.premadeSpecName[cls][specNo] << " (";
        out << tabCount[0] << "-" << tabCount[1] << "-" << tabCount[2] << ")";
        botAI->TellMasterNoFacing(out.str());
    }
    out << "Total " << specFound << " specs found";
    return out.str();
}

std::string ChangeTalentsAction::SpecPick(std::string param)
{
    int cls = bot->getClass();
    // int specFound = 0; //not used, line marked for removal.
    for (int specNo = 0; specNo < MAX_SPECNO; ++specNo)
    {
        if (sPlayerbotAIConfig.premadeSpecName[cls][specNo].size() == 0)
        {
            break;
        }
        if (sPlayerbotAIConfig.premadeSpecName[cls][specNo] == param)
        {
            PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true);

            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.InitGlyphs(false);

            std::ostringstream out;
            out << "Picking " << sPlayerbotAIConfig.premadeSpecName[cls][specNo];
            return out.str();
        }
    }
    std::ostringstream out;
    out << "Spec " << param << " not found";
    return out.str();
}

std::string ChangeTalentsAction::SpecApply(std::string param)
{
    int cls = bot->getClass();
    std::ostringstream out;
    std::vector<std::vector<uint32>> parsedSpecLink = PlayerbotAIConfig::ParseTempTalentsOrder(cls, param);
    if (parsedSpecLink.size() == 0)
    {
        out << "Invalid link " << param;
        return out.str();
    }
    PlayerbotFactory::InitTalentsByParsedSpecLink(bot, parsedSpecLink, true);
    out << "Applying " << param;
    return out.str();
}

// std::vector<TalentPath*> ChangeTalentsAction::getPremadePaths(std::string const findName)
// {
//     std::vector<TalentPath*> ret;
//     // for (auto& path : sPlayerbotAIConfig.classSpecs[bot->getClass()].talentPath)
//     // {
//     //     if (findName.empty() || path.name.find(findName) != std::string::npos)
//     //     {
//     //         ret.push_back(&path);
//     //     }
//     // }

//     return ret;
// }

// std::vector<TalentPath*> ChangeTalentsAction::getPremadePaths(TalentSpec* oldSpec)
// {
//     std::vector<TalentPath*> ret;

//     // for (auto& path : sPlayerbotAIConfig.classSpecs[bot->getClass()].talentPath)
//     // {
//     //     TalentSpec newSpec = *GetBestPremadeSpec(path.id);
//     //     newSpec.CropTalents(bot->GetLevel());
//     //     if (oldSpec->isEarlierVersionOf(newSpec))
//     //     {
//     //         ret.push_back(&path);
//     //     }
//     // }

//     return ret;
// }

// TalentPath* ChangeTalentsAction::getPremadePath(uint32 id)
// {
//     // for (auto& path : sPlayerbotAIConfig.classSpecs[bot->getClass()].talentPath)
//     // {
//     //     if (id == path.id)
//     //     {
//     //         return &path;
//     //     }
//     // }

//     // return &sPlayerbotAIConfig.classSpecs[bot->getClass()].talentPath[0];
//     return nullptr;
// }

// void ChangeTalentsAction::listPremadePaths(std::vector<TalentPath*> paths, std::ostringstream* out)
// {
//     if (paths.size() == 0)
//     {
//         *out << "No predefined talents found..";
//     }

//     *out << "|h|cffffffff";

//     for (auto path : paths)
//     {
//         *out << path->name << " (" << path->talentSpec.back().FormatSpec(bot) << "), ";
//     }

//     out->seekp(-2, out->cur);
//     *out << ".";
// }

// TalentPath* ChangeTalentsAction::PickPremadePath(std::vector<TalentPath*> paths, bool useProbability)
// {
//     uint32 totProbability = 0;
//     uint32 curProbability = 0;

//     if (paths.size() == 1)
//         return paths[0];

//     for (auto path : paths)
//     {
//         totProbability += useProbability ? path->probability : 1;
//     }

//     totProbability = urand(0, totProbability);

//     for (auto path : paths)
//     {
//         curProbability += (useProbability ? path->probability : 1);
//         if (curProbability >= totProbability)
//             return path;
//     }

//     return paths[0];
// }

// bool ChangeTalentsAction::AutoSelectTalents(std::ostringstream* out)
// {
//     // Does the bot have talentpoints?
//     if (bot->GetLevel() < 10)
//     {
//         *out << "No free talent points.";
//         return false;
//     }

//     uint32 specNo = sRandomPlayerbotMgr.GetValue(bot->GetGUID().GetCounter(), "specNo");
//     uint32 specId = specNo - 1;
//     std::string specLink = sRandomPlayerbotMgr.GetData(bot->GetGUID().GetCounter(), "specLink");

//     //Continue the current spec
//     if (specNo > 0)
//     {
//         TalentSpec newSpec = *GetBestPremadeSpec(specId);
//         newSpec.CropTalents(bot->GetLevel());
//         newSpec.ApplyTalents(bot, out);
//         if (newSpec.GetTalentPoints() > 0)
//         {
//             *out << "Upgrading spec " << "|h|cffffffff" << getPremadePath(specId)->name << "" <<
//             newSpec.FormatSpec(bot);
//         }
//     }
//     else if (!specLink.empty())
//     {
//         TalentSpec newSpec(bot, specLink);
//         newSpec.CropTalents(bot->GetLevel());
//         newSpec.ApplyTalents(bot, out);
//         if (newSpec.GetTalentPoints() > 0)
//         {
//             *out << "Upgrading saved spec "
//                  << "|h|cffffffff" << chat->FormatClass(bot, newSpec.highestTree()) << " (" <<
//                  newSpec.FormatSpec(bot) << ")";
//         }
//     }

//     //Spec was not found or not sufficient
//     if (bot->GetFreeTalentPoints() > 0 || (!specNo && specLink.empty()))
//     {
//         TalentSpec oldSpec(bot);
//         std::vector<TalentPath*> paths = getPremadePaths(&oldSpec);

//         if (paths.size() == 0) //No spec like the old one found. Pick any.
//         {
//             if (bot->CalculateTalentsPoints() > 0)
//                 *out << "No specs like the current spec found. ";

//             paths = getPremadePaths("");
//         }

//         if (paths.size() == 0)
//         {
//             *out << "No predefined talents found for this class.";
//             specId = -1;
//             // specLink = "";
//         }
//         else if (paths.size() > 1 && false/*!sPlayerbotAIConfig.autoPickTalents*/ &&
//         !sRandomPlayerbotMgr.IsRandomBot(bot))
//         {
//             *out << "Found multiple specs: ";
//             listPremadePaths(paths, out);
//         }
//         else
//         {
//             specId = PickPremadePath(paths, sRandomPlayerbotMgr.IsRandomBot(bot))->id;
//             TalentSpec newSpec = *GetBestPremadeSpec(specId);
//             specLink = newSpec.GetTalentLink();
//             newSpec.CropTalents(bot->GetLevel());
//             newSpec.ApplyTalents(bot, out);

//             if (paths.size() > 1)
//                 *out << "Found " << paths.size() << " possible specs to choose from. ";

//             *out << "Apply spec " << "|h|cffffffff" << getPremadePath(specId)->name << " " <<
//             newSpec.FormatSpec(bot);
//         }
//     }

//     sRandomPlayerbotMgr.SetValue(bot->GetGUID().GetCounter(), "specNo", specId + 1);

//     if (!specLink.empty() && specId == -1)
//         sRandomPlayerbotMgr.SetValue(bot->GetGUID().GetCounter(), "specLink", 1, specLink);
//     else
//         sRandomPlayerbotMgr.SetValue(bot->GetGUID().GetCounter(), "specLink", 0);

//     return (specNo == 0) ? false : true;
// }

// //Returns a pre-made talentspec that best suits the bots current talents.
// TalentSpec* ChangeTalentsAction::GetBestPremadeSpec(uint32 specId)
// {
//     TalentPath* path = getPremadePath(specId);
//     for (auto& spec : path->talentSpec)
//     {
//         if (spec.points >= bot->CalculateTalentsPoints())
//             return &spec;
//     }

//     if (path->talentSpec.size())
//         return &path->talentSpec.back();

//     // return &sPlayerbotAIConfig.classSpecs[bot->getClassMask()].baseSpec;
//     return nullptr;
// }

bool AutoSetTalentsAction::Execute(Event /*event*/)
{
    std::ostringstream out;

    if (!PlayerbotAIConfig::instance().autoPickTalents || !RandomPlayerbotMgr::instance().IsRandomBot(bot))
        return false;

    if (bot->GetFreeTalentPoints() <= 0)
        return false;

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitTalentsTree(true, true, true);
    factory.InitPetTalents();
    botAI->TellMaster(out);

    return true;
}
