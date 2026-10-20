/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_EVENT_H
#define PLAYERBOTS_EVENT_H

#include "ObjectGuid.h"
#include "WorldPacket.h"

class Player;

class Event
{
public:
    Event(Event const& other) = default;
    Event& operator=(Event const& other) = default;
    Event() {}
    Event(std::string const source) : source(source) {}
    Event(std::string const source, std::string const param, Player* owner = nullptr);
    Event(std::string const source, WorldPacket& packet, Player* owner = nullptr);
    Event(std::string const source, ObjectGuid object, Player* owner = nullptr);
    virtual ~Event() {}

    std::string const GetSource() { return source; }
    std::string const getParam() { return param; }
    WorldPacket& getPacket() { return packet; }
    ObjectGuid getObject();
    // Looked up again by guid: an action queued for later must not use a player who logged out meanwhile.
    Player* getOwner();
    bool operator!() const { return source.empty(); }

protected:
    std::string source;
    std::string param;
    WorldPacket packet;
    ObjectGuid ownerGuid;
};

#endif
