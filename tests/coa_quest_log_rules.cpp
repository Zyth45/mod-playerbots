#include "CoaQuestLogRules.h"

#include <cassert>

int main()
{
    CoaQuestKeepInfo fresh;
    CoaQuestKeepInfo halfDone;
    halfDone.progress = 0.5f;
    CoaQuestKeepInfo ready;
    ready.complete = true;
    CoaQuestKeepInfo grey;
    grey.worthDoing = false;
    grey.levelGap = 12;
    CoaQuestKeepInfo noTemplate;
    noTemplate.templateMissing = true;
    CoaQuestKeepInfo failed;
    failed.failed = true;

    // Progress made is kept over a fresh quest; a quest ready to turn in is kept over everything.
    assert(CoaQuestKeepScore(halfDone) > CoaQuestKeepScore(fresh));
    assert(CoaQuestKeepScore(ready) > CoaQuestKeepScore(halfDone));
    assert(CoaQuestKeepScore(grey) < CoaQuestKeepScore(fresh));
    assert(CoaQuestKeepScore(noTemplate) < CoaQuestKeepScore(failed));
    assert(CoaQuestKeepScore(failed) < CoaQuestKeepScore(grey));

    // Out of zone is a preference only: an out-of-zone quest mostly done beats a fresh one in zone.
    CoaQuestKeepInfo inZone;
    inZone.inBotZone = true;
    CoaQuestKeepInfo mostlyDone;
    mostlyDone.progress = 0.75f;
    assert(CoaQuestKeepScore(mostlyDone) > CoaQuestKeepScore(inZone));
    assert(CoaQuestKeepScore(inZone) > CoaQuestKeepScore(fresh));

    // One drop per pass by default, the worst first.
    std::vector<CoaQuestLogEntry> log = {{0, CoaQuestKeepScore(fresh), false},
                                         {1, CoaQuestKeepScore(grey), false},
                                         {2, CoaQuestKeepScore(ready), true},
                                         {3, CoaQuestKeepScore(failed), false}};
    std::vector<std::uint16_t> drop = CoaQuestSlotsToDrop(log, 1);
    assert(drop.size() == 1 && drop[0] == 3);
    // 0 is read as 1: a full log must still free a slot.
    assert(CoaQuestSlotsToDrop(log, 0).size() == 1);
    drop = CoaQuestSlotsToDrop(log, 3);
    assert(drop.size() == 3 && drop[0] == 3 && drop[1] == 1 && drop[2] == 0);
    // Never the quest ready to turn in, even when asked for more drops than there are other quests.
    drop = CoaQuestSlotsToDrop(log, 10);
    assert(drop.size() == 3);
    for (std::uint16_t slot : drop)
        assert(slot != 2);
    // A log full of quests ready to turn in: nothing is dropped.
    std::vector<CoaQuestLogEntry> allReady = {{0, 1000.0f, true}, {1, 1000.0f, true}};
    assert(CoaQuestSlotsToDrop(allReady, 5).empty());
    return 0;
}
