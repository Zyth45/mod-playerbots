/*
 * CoA: generic, parameterised triggers and actions.
 *
 * The problem: mod-playerbots registers actions such as "mortal strike" in
 * C++ - one class per spell, nine classes hard-wired. For Conquest of Azeroth
 * with its 21 additional classes and ~70 specs that does not scale, all the
 * more because the content itself is still moving.
 *
 * The solution: NamedObjectFactory::create() splits every name at "::" and
 * passes the trailing part to the object it creates, as long as that object
 * derives from Qualified. A handful of generic building blocks is then enough,
 * and the whole combat behaviour becomes DATA - rows in
 * playerbots_custom_strategy that can be changed without recompiling.
 *
 * What that expresses:
 *   spell ready::Rune Strike   > cast::Rune Strike         (keep it on cooldown)
 *   debuff missing::Rend       > cast debuff::Rend         (keep a debuff up)
 *   has aura::Eclipse (Lunar)  > cast::Starfire            (use a buff window)
 *   aura stacks::Fury,6        > cast::Consuming Strike    (spend at full stacks)
 *   coa resource::Static,75    > cast::Arm of Thorim       (from three quarters of the resource)
 *   coa summon missing         > cast::Tentacle of C'Thun  (only while no summon of our own stands)
 *
 * And for a healer, with the base triggers that rank the group by damage
 * taken:
 *
 *   party member critical health > cast heal party::Flash Heal
 *   medium group heal setting    > cast heal aoe::Prayer of Healing
 *   party member dead            > cast rez::Resurrection
 *
 * The spell name is resolved at runtime through SpellIdValue against the bot's
 * spellbook, so no spell id tables and no rank bookkeeping are needed.
 *
 * Deliberately NOT changed: the existing class contexts. These building blocks
 * are added to the shared context and are therefore open to every bot.
 */
#ifndef PLAYERBOTS_COAGENERICCONTEXT_H
#define PLAYERBOTS_COAGENERICCONTEXT_H

#include "Timer.h"
#include "GenericSpellActions.h"
#include "CureTriggers.h"
#include "GenericTriggers.h"
#include "NamedObjectContext.h"

// ---------------------------------------------------------------------------
// Actions - the qualifier is the spell name
// ---------------------------------------------------------------------------

#define COA_QUALIFIED_CAST(ClassName, Base, Label)                              \
    class ClassName : public Base, public Qualified                             \
    {                                                                           \
    public:                                                                     \
        ClassName(PlayerbotAI* botAI) : Base(botAI, "") {}                      \
        void Qualify(std::string const qual) override                           \
        {                                                                       \
            Qualified::Qualify(qual);                                           \
            spell = qual;                                                       \
        }                                                                       \
        std::string const getName() override { return Label "::" + qualifier; } \
    };

COA_QUALIFIED_CAST(CoaCastAction, CastSpellAction, "cast")
COA_QUALIFIED_CAST(CoaCastMeleeAction, CastMeleeSpellAction, "cast melee")
COA_QUALIFIED_CAST(CoaCastBuffAction, CastBuffSpellAction, "cast buff")
COA_QUALIFIED_CAST(CoaCastDebuffAction, CastDebuffSpellAction, "cast debuff")
// Resurrection: aims at the dead party member, not at the current target.
// Together with the base trigger "party member dead" that is enough.
COA_QUALIFIED_CAST(CoaCastRezAction, ResurrectPartyMemberAction, "cast rez")

// Healing needs its own handling: the constructor takes further parameters.
//
// This one heals the CASTER. It is what a damage dealer uses to keep itself
// standing; a healer needs the three below, which act on the group.
class CoaCastHealAction : public CastHealingSpellAction, public Qualified
{
public:
    CoaCastHealAction(PlayerbotAI* botAI) : CastHealingSpellAction(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "cast heal::" + qualifier; }
};

/* Healing the group, the way the nine base classes already do it.
 *
 * The difference to `cast heal` is the target, not the spell:
 * HealPartyMemberAction asks the value "party member to heal", which ranks the
 * group by how badly it is hurt and by range. Paired with the base triggers
 * `party member critical health`, `party member low health` and
 * `party member medium health`, that is the same structure HealPriestStrategy
 * uses - only as data rather than as C++ per spell.
 *
 * PartyMemberActionNameSupport builds its name in the constructor, when the
 * qualifier is not known yet, so getName() is overridden here instead. The
 * target value reads `spell` at call time and therefore works unchanged.
 */
class CoaHealPartyAction : public HealPartyMemberAction, public Qualified
{
public:
    CoaHealPartyAction(PlayerbotAI* botAI) : HealPartyMemberAction(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "cast heal party::" + qualifier; }
};

// Group heal. Same target value, but isUseful() additionally asks whether
// enough of the group is hurt for an area heal to be worth its mana.
class CoaAoeHealAction : public CastAoeHealSpellAction, public Qualified
{
public:
    CoaAoeHealAction(PlayerbotAI* botAI) : CastAoeHealSpellAction(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "cast heal aoe::" + qualifier; }
};

// A buff or a shield on a group member. Targets the first member who does not
// carry the aura yet, so a rule fires once per member instead of once per tick.
class CoaBuffPartyAction : public BuffOnPartyAction, public Qualified
{
public:
    CoaBuffPartyAction(PlayerbotAI* botAI) : BuffOnPartyAction(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "cast buff party::" + qualifier; }
};

/* Removing a harmful effect from a group member.
 *
 * Both the trigger and the action ask the value "party member to dispel",
 * qualified with a DISPEL TYPE - magic, curse, disease, poison. The nine base
 * classes hand that number in from C++, one class per cure spell.
 *
 * We do not want the number in the strategy line: whoever writes
 * `cure party::Continuum Restoration` should not have to look up that magic
 * is 1. The type is in the spell itself - the effect SPELL_EFFECT_DISPEL
 * carries it as its MiscValue - so it is resolved from the spell name on
 * first use and remembered.
 *
 * A spell that dispels nothing yields 0, and both the trigger and the action
 * then stay quiet.
 */
uint32 CoaDispelTypeOf(PlayerbotAI* botAI, std::string const& spellName);

class CoaCurePartyAction : public CurePartyMemberAction, public Qualified
{
public:
    CoaCurePartyAction(PlayerbotAI* botAI) : CurePartyMemberAction(botAI, "", 0) {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
        dispelType = 0;
    }
    Value<Unit*>* GetTargetValue() override
    {
        if (!dispelType)
            dispelType = CoaDispelTypeOf(botAI, spell);

        return CurePartyMemberAction::GetTargetValue();
    }
    std::string const getName() override { return "cast cure party::" + qualifier; }
};

// ---------------------------------------------------------------------------
// Triggers - the qualifier is the spell name (for "aura stacks" the stack
// count follows after a comma)
// ---------------------------------------------------------------------------

/* These three mirror HasAura/HasNoAura/HasAuraStack. The originals keep the
 * spell name in the const field AiNamedObject::name, which cannot be qualified
 * afterwards - hence our own implementations with identical logic (see
 * GenericTriggers.cpp). */
class CoaHasAuraTrigger : public Trigger, public Qualified
{
public:
    CoaHasAuraTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa has aura") {}
    std::string const GetTargetName() override { return "self target"; }
    std::string const getName() override { return "has aura::" + qualifier; }
    bool IsActive() override;
};

class CoaTargetHasAuraTrigger : public Trigger, public Qualified
{
public:
    CoaTargetHasAuraTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa target has aura") {}
    std::string const GetTargetName() override { return "current target"; }
    std::string const getName() override { return "target has aura::" + qualifier; }
    bool IsActive() override;
};

class CoaHasNoAuraTrigger : public Trigger, public Qualified
{
public:
    CoaHasNoAuraTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa no aura") {}
    std::string const GetTargetName() override { return "self target"; }
    std::string const getName() override { return "no aura::" + qualifier; }
    bool IsActive() override;
};

/* "aura stacks::<spell>,<count>" - carries the builder/spender pattern that
 * several CoA specs use (Felsworn Fury up to 6, for instance). It assumes the
 * resource is implemented as a stacking aura. */
class CoaAuraStacksTrigger : public Trigger, public Qualified
{
public:
    CoaAuraStacksTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa aura stacks") {}
    std::string const GetTargetName() override { return "self target"; }
    std::string const getName() override { return "aura stacks::" + qualifier; }
    bool IsActive() override;

private:
    std::string SpellPart() const;
    int StackPart() const;
};

/* "coa resource::<name>,<percent>" - resource level as a share of its maximum.
 *
 * WHY NOT "aura stacks": that compares absolute stacks. The maxima of the CoA
 * resources are far apart - Static 100, Brood Mark 5, Reaped Soul 3,
 * Earthshaping 15. A rule like "Arm of Thorim only from three quarters" would
 * otherwise have to be written differently for every class, and not at all for
 * resources without a fixed maximum in the table.
 *
 * Only mod-ascension-compat knows those maxima, and its public API does not
 * expose them, so this trigger currently reaches the CORE powers alone: mana,
 * rage, energy, runic power, focus, happiness. A name it does not know leaves
 * the trigger off - a misspelled line therefore makes the bot quiet, not wild.
 *
 * Example:
 *   coa resource::Static,75 > cast::Arm of Thorim
 */
class CoaResourceTrigger : public Trigger, public Qualified
{
public:
    CoaResourceTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa resource") {}
    std::string const getName() override { return "coa resource::" + qualifier; }
    bool IsActive() override;

private:
    std::string NamePart() const;
    int PercentPart() const;
};

/* "coa summon missing" - true while the bot has no living summon of its own
 * nearby.
 *
 * WHAT FOR: summons with a cap. Tentacle of C'Thun says in its spell text "You
 * may only have 1 Old God Tentacle active at a time", so a second cast is
 * wasted. With "can cast" the bot casts it at every opportunity; observed in
 * game on 14 Sep 2026.
 *
 * WHY TWO ROUTES: guardians and minions hang off the bot directly and appear in
 * m_Controlled. Time-limited summons such as the tentacles do not necessarily
 * end up there - they are created through SummonProperties with the player as
 * their summoner. So the surroundings are searched as well, for creatures the
 * bot owns.
 */
class CoaSummonMissingTrigger : public Trigger
{
public:
    CoaSummonMissingTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa summon missing") {}
    std::string const getName() override { return "coa summon missing"; }
    bool IsActive() override;
};

/* "cure party::<spell>" - a group member carries something this spell removes.
 * See CoaDispelTypeOf above for where the dispel type comes from. */
class CoaCurePartyTrigger : public PartyMemberNeedCureTrigger, public Qualified
{
public:
    CoaCurePartyTrigger(PlayerbotAI* botAI) : PartyMemberNeedCureTrigger(botAI, "", 0) {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
        dispelType = 0;
    }
    Value<Unit*>* GetTargetValue() override
    {
        if (!dispelType)
            dispelType = CoaDispelTypeOf(botAI, spell);

        return PartyMemberNeedCureTrigger::GetTargetValue();
    }
    bool IsActive() override
    {
        return CoaDispelTypeOf(botAI, spell) && PartyMemberNeedCureTrigger::IsActive();
    }
    std::string const getName() override { return "cure party::" + qualifier; }
};

/* Cooldown ready - the "keep it on cooldown" pattern. The original reads
 * AiNamedObject::name, hence our own implementation here. */
class CoaSpellReadyTrigger : public Trigger, public Qualified
{
public:
    CoaSpellReadyTrigger(PlayerbotAI* botAI) : Trigger(botAI, "coa spell ready") {}
    std::string const getName() override { return "spell ready::" + qualifier; }
    bool IsActive() override;
};

/* These three derive from SpellTrigger, whose `spell` field is settable - here
 * qualifying is enough and the logic stays that of the original. */
#define COA_QUALIFIED_SPELL_TRIGGER(ClassName, Base, Label)                     \
    class ClassName : public Base, public Qualified                             \
    {                                                                           \
    public:                                                                     \
        ClassName(PlayerbotAI* botAI) : Base(botAI, "") {}                      \
        void Qualify(std::string const qual) override                           \
        {                                                                       \
            Qualified::Qualify(qual);                                           \
            spell = qual;                                                       \
        }                                                                       \
        std::string const getName() override { return Label "::" + qualifier; } \
    };

/* "can cast::<spell>" - the spell can be cast now. Unlike the original, not when the bot already
 * carries the lasting aura it gives (an Ascension resistance aura was recast 23 times in one fight,
 * 20% of base mana each), nor for a healer keeping its mana for heals. */
class CoaCanCastTrigger : public SpellCanBeCastTrigger, public Qualified
{
public:
    CoaCanCastTrigger(PlayerbotAI* botAI) : SpellCanBeCastTrigger(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "can cast::" + qualifier; }
    bool IsActive() override;

private:
    // The last search for a summon of this spell still standing: a grid search, not every tick.
    uint32 summonCheckedAt = 0;
    bool summonStanding = false;
};
/* A rotation line that stays true while the bot acts on it gets nowhere: the aura never comes. It is
 * set aside for a while, so the lines below it get their turn. A Chronomancer's "buff missing::
 * Incarnation of Chaos" named a spell with no effect: at priority 87 it won every tick, and the bot
 * never even took a target (test arena, 22/09). A buff that works lands well within 5 seconds. */
struct CoaLineBackoff
{
    uint32 activeSince = 0;
    uint32 asideUntil = 0;

    bool Allow(bool active)
    {
        uint32 const now = getMSTime();
        if (asideUntil && getMSTimeDiff(now, asideUntil) > 0 && getMSTimeDiff(now, asideUntil) < 60 * IN_MILLISECONDS)
            return false;  // still set aside
        asideUntil = 0;
        if (!active)
        {
            activeSince = 0;
            return false;
        }
        if (!activeSince)
            activeSince = now;
        else if (getMSTimeDiff(activeSince, now) > 5 * IN_MILLISECONDS)
        {
            activeSince = 0;
            asideUntil = now + 30 * IN_MILLISECONDS;
            return false;
        }
        return true;
    }
};

/* "buff missing::<spell>" - as the original, except for a form a healer's heals cannot be cast in. */
class CoaBuffMissingTrigger : public BuffTrigger, public Qualified
{
public:
    CoaBuffMissingTrigger(PlayerbotAI* botAI) : BuffTrigger(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "buff missing::" + qualifier; }
    bool IsActive() override;

private:
    CoaLineBackoff backoff;
};
/* "debuff missing::<spell>" - as the original, except for a healer keeping its mana for heals
 * (a Chronomancer healer put Unmake back 12 times in one fight and ran dry for 20 s). */
class CoaDebuffMissingTrigger : public DebuffTrigger, public Qualified
{
public:
    CoaDebuffMissingTrigger(PlayerbotAI* botAI) : DebuffTrigger(botAI, "") {}
    void Qualify(std::string const qual) override
    {
        Qualified::Qualify(qual);
        spell = qual;
    }
    std::string const getName() override { return "debuff missing::" + qualifier; }
    bool IsActive() override;

private:
    CoaLineBackoff backoff;
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

/* The plain melee swing.
 *
 * Every one of the nine class strategies schedules NextAction("melee",
 * ACTION_DEFAULT) from its own getDefaultActions(). The generic `close`
 * strategy does not - it only carries the movement triggers. A CoA spec has
 * no class strategy, so a melee CoA bot never swings: between two abilities
 * it simply stands there. Reported on 16 Sep 2026 for Venomancer/Stalking.
 *
 * Ranged specs get their equivalent from the rotation itself (Auto Shot,
 * Shoot, Wand), because which of the three applies depends on the weapon.
 */
class CoaBasicAttackStrategy : public Strategy
{
public:
    CoaBasicAttackStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "coa basic attack"; }
    std::vector<NextAction> getDefaultActions() override
    {
        return { NextAction("melee", ACTION_DEFAULT) };
    }
    void InitTriggers(std::vector<TriggerNode*>& /*triggers*/) override {}
};

class CoaGenericStrategyContext : public NamedObjectContext<Strategy>
{
public:
    CoaGenericStrategyContext() : NamedObjectContext<Strategy>(false, true)
    {
        creators["coa basic attack"] = &CoaGenericStrategyContext::basic_attack;
    }

private:
    static Strategy* basic_attack(PlayerbotAI* botAI) { return new CoaBasicAttackStrategy(botAI); }
};

class CoaGenericActionContext : public NamedObjectContext<Action>
{
public:
    CoaGenericActionContext()
    {
        creators["cast"] = &CoaGenericActionContext::cast;
        creators["cast melee"] = &CoaGenericActionContext::cast_melee;
        creators["cast buff"] = &CoaGenericActionContext::cast_buff;
        creators["cast debuff"] = &CoaGenericActionContext::cast_debuff;
        creators["cast heal"] = &CoaGenericActionContext::cast_heal;
        creators["cast heal party"] = &CoaGenericActionContext::cast_heal_party;
        creators["cast heal aoe"] = &CoaGenericActionContext::cast_heal_aoe;
        creators["cast buff party"] = &CoaGenericActionContext::cast_buff_party;
        creators["cast cure party"] = &CoaGenericActionContext::cast_cure_party;
        creators["cast rez"] = &CoaGenericActionContext::cast_rez;
    }

private:
    static Action* cast(PlayerbotAI* botAI) { return new CoaCastAction(botAI); }
    static Action* cast_melee(PlayerbotAI* botAI) { return new CoaCastMeleeAction(botAI); }
    static Action* cast_buff(PlayerbotAI* botAI) { return new CoaCastBuffAction(botAI); }
    static Action* cast_debuff(PlayerbotAI* botAI) { return new CoaCastDebuffAction(botAI); }
    static Action* cast_heal(PlayerbotAI* botAI) { return new CoaCastHealAction(botAI); }
    static Action* cast_heal_party(PlayerbotAI* botAI) { return new CoaHealPartyAction(botAI); }
    static Action* cast_heal_aoe(PlayerbotAI* botAI) { return new CoaAoeHealAction(botAI); }
    static Action* cast_buff_party(PlayerbotAI* botAI) { return new CoaBuffPartyAction(botAI); }
    static Action* cast_cure_party(PlayerbotAI* botAI) { return new CoaCurePartyAction(botAI); }
    static Action* cast_rez(PlayerbotAI* botAI) { return new CoaCastRezAction(botAI); }
};

class CoaGenericTriggerContext : public NamedObjectContext<Trigger>
{
public:
    CoaGenericTriggerContext()
    {
        creators["has aura"] = &CoaGenericTriggerContext::has_aura;
        creators["target has aura"] = &CoaGenericTriggerContext::target_has_aura;
        creators["no aura"] = &CoaGenericTriggerContext::no_aura;
        creators["aura stacks"] = &CoaGenericTriggerContext::aura_stacks;
        creators["coa resource"] = &CoaGenericTriggerContext::coa_resource;
        creators["coa summon missing"] = &CoaGenericTriggerContext::coa_summon_missing;
        creators["cure party"] = &CoaGenericTriggerContext::cure_party;
        creators["spell ready"] = &CoaGenericTriggerContext::spell_ready;
        creators["can cast"] = &CoaGenericTriggerContext::can_cast;
        creators["buff missing"] = &CoaGenericTriggerContext::buff_missing;
        creators["debuff missing"] = &CoaGenericTriggerContext::debuff_missing;
    }

private:
    static Trigger* has_aura(PlayerbotAI* botAI) { return new CoaHasAuraTrigger(botAI); }
    static Trigger* target_has_aura(PlayerbotAI* botAI) { return new CoaTargetHasAuraTrigger(botAI); }
    static Trigger* no_aura(PlayerbotAI* botAI) { return new CoaHasNoAuraTrigger(botAI); }
    static Trigger* aura_stacks(PlayerbotAI* botAI) { return new CoaAuraStacksTrigger(botAI); }
    static Trigger* coa_resource(PlayerbotAI* botAI) { return new CoaResourceTrigger(botAI); }
    static Trigger* coa_summon_missing(PlayerbotAI* botAI)
    { return new CoaSummonMissingTrigger(botAI); }
    static Trigger* cure_party(PlayerbotAI* botAI) { return new CoaCurePartyTrigger(botAI); }
    static Trigger* spell_ready(PlayerbotAI* botAI) { return new CoaSpellReadyTrigger(botAI); }
    static Trigger* can_cast(PlayerbotAI* botAI) { return new CoaCanCastTrigger(botAI); }
    static Trigger* buff_missing(PlayerbotAI* botAI) { return new CoaBuffMissingTrigger(botAI); }
    static Trigger* debuff_missing(PlayerbotAI* botAI) { return new CoaDebuffMissingTrigger(botAI); }
};

#endif
