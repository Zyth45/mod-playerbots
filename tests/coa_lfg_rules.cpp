#include "CoaLfgRules.h"

#include <cassert>

int main()
{
    std::uint32_t const proposal = 40000;  // LFG_TIME_PROPOSAL in the core, 40 s

    // Free bot: accepts at once.
    assert(CoaLfgProposalDecision(false, false, false, false, 0, proposal) == CoaLfgProposalAnswer::Accept);
    // In combat or dead: waits, never declines.
    assert(CoaLfgProposalDecision(false, false, true, false, 0, proposal) == CoaLfgProposalAnswer::Wait);
    assert(CoaLfgProposalDecision(false, false, false, true, 10000, proposal) == CoaLfgProposalAnswer::Wait);
    // Out of combat again: accepts.
    assert(CoaLfgProposalDecision(false, false, false, false, 15000, proposal) == CoaLfgProposalAnswer::Accept);
    // Edge: still in combat just before the deadline waits, at the deadline accepts, before the proposal expires.
    std::uint32_t const deadline = proposal - COA_LFG_PROPOSAL_MARGIN_MS;
    assert(CoaLfgProposalDecision(false, false, true, false, deadline - 1, proposal) == CoaLfgProposalAnswer::Wait);
    assert(CoaLfgProposalDecision(false, false, true, false, deadline, proposal) == CoaLfgProposalAnswer::Accept);
    assert(CoaLfgProposalDecision(false, false, true, true, proposal - 1, proposal) == CoaLfgProposalAnswer::Accept);
    assert(!CoaLfgProposalExpired(proposal - 1, proposal));
    assert(!CoaLfgProposalExpired(proposal, proposal));
    assert(CoaLfgProposalExpired(proposal + 1, proposal));
    // A margin longer than the proposal: accepts at once rather than never.
    assert(CoaLfgProposalDecision(false, false, true, false, 0, 5000) == CoaLfgProposalAnswer::Accept);
    // Never for a bot in a battleground or arena, nor for a real player, whatever the time.
    assert(CoaLfgProposalDecision(false, true, false, false, 0, proposal) == CoaLfgProposalAnswer::DoNotAnswer);
    assert(CoaLfgProposalDecision(false, true, true, false, proposal - 1, proposal) == CoaLfgProposalAnswer::DoNotAnswer);
    assert(CoaLfgProposalDecision(true, false, false, false, 0, proposal) == CoaLfgProposalAnswer::DoNotAnswer);
    // 3.2.1: the role a queued party misses comes first.
    assert(CoaLfgRoleWanted(true, false, true, false));    // a tank, a tank missing
    assert(CoaLfgRoleWanted(false, true, false, true));    // a healer, a healer missing
    assert(!CoaLfgRoleWanted(false, true, true, false));   // a healer while only a tank is missing
    assert(!CoaLfgRoleWanted(false, false, true, true));   // a damage dealer: its random roll as before
    // 3.2.2 (issue 62): the addclass cache holds the CoA classes 12-32 (MAX_CLASSES 33), never 0, 10 or past the last.
    for (std::uint8_t claz = 12; claz <= 32; ++claz)
        assert(CoaAddclassCacheClass(claz, 33));
    assert(CoaAddclassCacheClass(1, 33) && CoaAddclassCacheClass(11, 33));
    assert(!CoaAddclassCacheClass(0, 33) && !CoaAddclassCacheClass(10, 33) && !CoaAddclassCacheClass(33, 33));
    assert(!CoaAddclassCacheClass(12, 12));  // a core without the CoA classes (MAX_CLASSES 12)
    return 0;
}
