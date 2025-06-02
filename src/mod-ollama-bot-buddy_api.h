#pragma once
#include "Player.h"
#include <string>
#include <vector>

enum class BotControlCommandType
{
    MoveTo,
    Attack,
    Interact,
    CastSpell,
    Loot,
    Follow,
    Say,
    AcceptQuest,
    TurnInQuest,
    Stop
};

struct BotControlCommand
{
    BotControlCommandType type;
    std::vector<std::string> args;
};

bool HandleBotControlCommand(Player* bot, const BotControlCommand& command);
bool ParseBotControlCommand(Player* bot, const std::string& commandStr);

// BotBuddyAI namespace with wrappers for bot actions
namespace BotBuddyAI
{
    bool MoveTo(Player* bot, float x, float y, float z);
    bool Attack(Player* bot, ObjectGuid guid);
    bool CastSpell(Player* bot, uint32 spellId, Unit* target = nullptr);
    bool Say(Player* bot, const std::string& msg);
    bool FollowMaster(Player* bot);
    bool StopMoving(Player* bot);
    bool AcceptQuest(Player* bot, uint32 questId);
    bool TurnInQuest(Player* bot, uint32 questId);
    bool LootNearby(Player* bot);
    bool Interact(Player* bot, ObjectGuid guid);
}
