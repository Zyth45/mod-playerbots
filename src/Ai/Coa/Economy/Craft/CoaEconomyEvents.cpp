/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaEconomyEvents.h"

#include "CoaEconomyConf.h"
#include "CoaEconomyEventsRules.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr std::size_t RowsPerStatement = 500;

struct Settings
{
    bool enabled = true;
    uint32 purgeDays = 90;
    uint32 formulasPerCharacter = 300;
} settings;

struct FormulaRow
{
    uint32 owner, team, skill, spell, recipe, quality, price, at;
    char const* how;  // one of CoaEconomyEventsRules::KnownHow's literals
};

struct OrderRow
{
    uint32 enchanter, customer, team, item, entry, enchant, price, fee, at;
};

std::mutex lock;
std::vector<FormulaRow> formulas;
std::vector<OrderRow> orders;
bool tableReady = false;

void LoadSettings()
{
    settings.enabled = CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Events.Enable", true, false);
    settings.purgeDays = CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Events.PurgeDays", 90, false);
    settings.formulasPerCharacter =
        std::max<uint32>(1, CoaEconomy::Conf::Get<uint32>("AiPlayerbot.CoaEconomy.Events.FormulasPerCharacter", 300,
                                                          false));
}

void EnsureTables()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `coa_formula_event` ("
        "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `owner` INT UNSIGNED NOT NULL, `team` TINYINT UNSIGNED NOT NULL, "
        "`skill` SMALLINT UNSIGNED NOT NULL, `spell` INT UNSIGNED NOT NULL, `recipe` INT UNSIGNED NOT NULL DEFAULT 0, "
        "`quality` TINYINT UNSIGNED NOT NULL DEFAULT 0, `how` VARCHAR(8) NOT NULL, "
        "`price` INT UNSIGNED NOT NULL DEFAULT 0, `at` INT UNSIGNED NOT NULL, "
        "PRIMARY KEY (`id`), KEY `owner_at` (`owner`, `at`), KEY `skill_team` (`skill`, `team`)) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `coa_enchant_order` ("
        "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `enchanter` INT UNSIGNED NOT NULL, "
        "`customer` INT UNSIGNED NOT NULL, `team` TINYINT UNSIGNED NOT NULL, `item` INT UNSIGNED NOT NULL, "
        "`entry` INT UNSIGNED NOT NULL, `enchant` INT UNSIGNED NOT NULL, `price` INT UNSIGNED NOT NULL, "
        "`fee` INT UNSIGNED NOT NULL, `at` INT UNSIGNED NOT NULL, "
        "PRIMARY KEY (`id`), KEY `enchanter_at` (`enchanter`, `at`), KEY `team_at` (`team`, `at`)) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    tableReady = true;
}

void Flush()
{
    std::vector<FormulaRow> f;
    std::vector<OrderRow> o;
    {
        std::lock_guard<std::mutex> guard(lock);
        f.swap(formulas);
        o.swap(orders);
    }
    if (!tableReady)
        return;
    for (std::size_t i = 0; i < f.size(); i += RowsPerStatement)
    {
        std::string sql = "INSERT INTO `coa_formula_event` (`owner`, `team`, `skill`, `spell`, `recipe`, `quality`, "
                          "`how`, `price`, `at`) VALUES ";
        for (std::size_t j = i; j < f.size() && j < i + RowsPerStatement; ++j)
        {
            FormulaRow const& r = f[j];
            sql += (j == i ? "(" : ",(") + std::to_string(r.owner) + "," + std::to_string(r.team) + "," +
                   std::to_string(r.skill) + "," + std::to_string(r.spell) + "," + std::to_string(r.recipe) + "," +
                   std::to_string(r.quality) + ",'" + r.how + "'," + std::to_string(r.price) + "," +
                   std::to_string(r.at) + ")";
        }
        CharacterDatabase.Execute(sql);
    }
    for (std::size_t i = 0; i < o.size(); i += RowsPerStatement)
    {
        std::string sql = "INSERT INTO `coa_enchant_order` (`enchanter`, `customer`, `team`, `item`, `entry`, "
                          "`enchant`, `price`, `fee`, `at`) VALUES ";
        for (std::size_t j = i; j < o.size() && j < i + RowsPerStatement; ++j)
        {
            OrderRow const& r = o[j];
            sql += (j == i ? "(" : ",(") + std::to_string(r.enchanter) + "," + std::to_string(r.customer) + "," +
                   std::to_string(r.team) + "," + std::to_string(r.item) + "," + std::to_string(r.entry) + "," +
                   std::to_string(r.enchant) + "," + std::to_string(r.price) + "," + std::to_string(r.fee) + "," +
                   std::to_string(r.at) + ")";
        }
        CharacterDatabase.Execute(sql);
    }
}

// Rows of characters deleted; orders older than PurgeDays (a formula learned stays known: its row stays, at most
// FormulasPerCharacter per character, the newest kept).
void Purge()
{
    if (!tableReady)
        return;
    CharacterDatabase.Execute("DELETE e FROM `coa_formula_event` e LEFT JOIN `characters` c ON c.`guid` = e.`owner` "
                              "WHERE c.`guid` IS NULL");
    CharacterDatabase.Execute("DELETE e FROM `coa_enchant_order` e LEFT JOIN `characters` c ON c.`guid` = e.`enchanter` "
                              "WHERE c.`guid` IS NULL");
    if (int64 const before = CoaEconomyEventsRules::PurgeBefore(int64(time(nullptr)), settings.purgeDays))
        CharacterDatabase.Execute("DELETE FROM `coa_enchant_order` WHERE `at` < {}", before);
    CharacterDatabase.Execute("DELETE e FROM `coa_formula_event` e JOIN (SELECT `id`, ROW_NUMBER() OVER "
                              "(PARTITION BY `owner` ORDER BY `id` DESC) AS `n` FROM `coa_formula_event`) r "
                              "ON r.`id` = e.`id` WHERE r.`n` > {}", settings.formulasPerCharacter);
}

class CoaEconomyEventsWorld : public WorldScript
{
public:
    CoaEconomyEventsWorld()
        : WorldScript("CoaEconomyEventsWorld",
                      {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_SHUTDOWN, WORLDHOOK_ON_UPDATE})
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override
    {
        if (!settings.enabled)
            return;
        EnsureTables();
        Purge();
    }

    void OnShutdown() override { Flush(); }

    void OnUpdate(uint32 diff) override
    {
        _flush += diff;
        _purge += diff;
        if (_flush >= MINUTE * IN_MILLISECONDS)
        {
            _flush = 0;
            Flush();
        }
        if (_purge >= DAY * IN_MILLISECONDS)
        {
            _purge = 0;
            if (settings.enabled)
                Purge();
        }
    }

private:
    uint32 _flush = 0;
    uint32 _purge = 0;
};
}  // namespace

void CoaEconomy::Events::NoteFormula(Player* learner, uint32 skill, uint32 spell, ItemTemplate const* recipe,
                                     char const* how, uint64 price)
{
    char const* const known = CoaEconomyEventsRules::KnownHow(how);
    if (!settings.enabled || !learner || !spell || !known)
        return;
    FormulaRow const row{uint32(learner->GetGUID().GetCounter()), uint32(learner->GetTeamId()), skill, spell,
                         recipe ? recipe->ItemId : 0u, recipe ? recipe->Quality : 0u,
                         CoaEconomyEventsRules::Copper(price), uint32(time(nullptr)), known};
    std::lock_guard<std::mutex> guard(lock);
    formulas.push_back(row);
}

void CoaEconomy::Events::NoteOrder(Player* enchanter, Player* customer, Item* item, uint32 enchantId, uint64 price,
                                   uint64 fee)
{
    if (!settings.enabled || !enchanter || !customer || !item)
        return;
    OrderRow const row{uint32(enchanter->GetGUID().GetCounter()), uint32(customer->GetGUID().GetCounter()),
                       uint32(enchanter->GetTeamId()), uint32(item->GetGUID().GetCounter()), item->GetEntry(),
                       enchantId, CoaEconomyEventsRules::Copper(price), CoaEconomyEventsRules::Copper(fee),
                       uint32(time(nullptr))};
    std::lock_guard<std::mutex> guard(lock);
    orders.push_back(row);
}

void AddSC_coa_economy_events()
{
    LoadSettings();
    new CoaEconomyEventsWorld();
}
