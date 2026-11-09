#include "CoaBotPacketRules.h"

#include <cassert>
#include <initializer_list>

using namespace CoaBotPackets;

int main()
{
    // Each packet has exactly one side while the bot is in the world, except in-place ones (either).
    for (bool inWorld : { true, false })
    {
        assert(HandleHere(PLACE_THREADUNSAFE, inWorld, false) && !HandleHere(PLACE_THREADUNSAFE, inWorld, true));
        assert(HandleHere(PLACE_THREADSAFE, inWorld, true) != HandleHere(PLACE_THREADSAFE, inWorld, false));
        assert(HandleHere(PLACE_INPLACE, inWorld, true) && HandleHere(PLACE_INPLACE, inWorld, false));
    }
    // Thread-safe: the map thread while in the world, the world thread otherwise (teleport, login).
    assert(HandleHere(PLACE_THREADSAFE, true, true));
    assert(!HandleHere(PLACE_THREADSAFE, false, true));
    assert(HandleHere(PLACE_THREADSAFE, false, false));
    return 0;
}
