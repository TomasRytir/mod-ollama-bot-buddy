#include "mod-ollama-bot-buddy_api.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include <sstream>

namespace BotBuddyAI
{
    bool MoveTo(Player* bot, float x, float y, float z)
    {
        if (!bot) return false;
        bot->GetMotionMaster()->MovePoint(0, x, y, z);
        return true;
    }

    bool Attack(Player* bot, ObjectGuid targetGuid)
    {
        if (!bot) return false;
        Unit* target = ObjectAccessor::GetUnit(*bot, targetGuid);
        if (!target) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        // Set the target and issue the attack command (no PushAction, no AI_VALUE)
        ai->GetAiObjectContext()->GetValue<ObjectGuid>("attack target")->Set(targetGuid);
        ai->TellMasterNoFacing("attack");
        return true;
    }

    bool CastSpell(Player* bot, uint32 spellId, Unit* target)
    {
        if (!bot) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        if (!target) target = bot->GetVictim();
        if (!target) return false;
        ai->CastSpell(spellId, target);
        return true;
    }

    bool Say(Player* bot, const std::string& msg)
    {
        if (!bot) return false;
        bot->Say(msg, LANG_UNIVERSAL);
        return true;
    }

    bool FollowMaster(Player* bot)
    {
        if (!bot) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        ai->TellMasterNoFacing("follow");
        return true;
    }

    bool StopMoving(Player* bot)
    {
        if (!bot) return false;
        bot->StopMoving();
        return true;
    }

    bool AcceptQuest(Player* bot, uint32 questId)
    {
        if (!bot) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        std::stringstream ss;
        ss << "accept quest " << questId;
        ai->TellMasterNoFacing(ss.str());
        return true;
    }

    bool TurnInQuest(Player* bot, uint32 questId)
    {
        if (!bot) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        std::stringstream ss;
        ss << "turnin quest " << questId;
        ai->TellMasterNoFacing(ss.str());
        return true;
    }

    bool LootNearby(Player* bot)
    {
        if (!bot) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        ai->TellMasterNoFacing("loot");
        return true;
    }
} // namespace BotBuddyAI

bool HandleBotControlCommand(Player* bot, const BotControlCommand& command)
{
    if (!bot) return false;
    switch (command.type)
    {
        case BotControlCommandType::MoveTo:
            if (command.args.size() >= 3)
            {
                float x = std::stof(command.args[0]);
                float y = std::stof(command.args[1]);
                float z = std::stof(command.args[2]);
                return BotBuddyAI::MoveTo(bot, x, y, z);
            }
            break;
        case BotControlCommandType::Attack:
            if (!command.args.empty())
            {
                ObjectGuid guid(std::stoull(command.args[0]));
                return BotBuddyAI::Attack(bot, guid);
            }
            break;
        case BotControlCommandType::CastSpell:
            if (!command.args.empty())
            {
                uint32 spellId = std::stoi(command.args[0]);
                return BotBuddyAI::CastSpell(bot, spellId);
            }
            break;
        case BotControlCommandType::Say:
            if (!command.args.empty())
            {
                return BotBuddyAI::Say(bot, command.args[0]);
            }
            break;
        case BotControlCommandType::Follow:
            return BotBuddyAI::FollowMaster(bot);
        case BotControlCommandType::Stop:
            return BotBuddyAI::StopMoving(bot);
        case BotControlCommandType::AcceptQuest:
            if (!command.args.empty())
            {
                uint32 questId = std::stoi(command.args[0]);
                return BotBuddyAI::AcceptQuest(bot, questId);
            }
            break;
        case BotControlCommandType::TurnInQuest:
            if (!command.args.empty())
            {
                uint32 questId = std::stoi(command.args[0]);
                return BotBuddyAI::TurnInQuest(bot, questId);
            }
            break;
        case BotControlCommandType::Loot:
            return BotBuddyAI::LootNearby(bot);
        default:
            break;
    }
    return false;
}

bool ParseBotControlCommand(Player* bot, const std::string& commandStr)
{
    std::istringstream iss(commandStr);
    std::string cmd;
    iss >> cmd;
    if (cmd == "move")
    {
        std::string to;
        iss >> to;
        if (to != "to") return false;
        float x, y, z;
        iss >> x >> y >> z;
        BotControlCommand command = {BotControlCommandType::MoveTo, {std::to_string(x), std::to_string(y), std::to_string(z)}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "attack")
    {
        std::string guid;
        iss >> guid;
        BotControlCommand command = {BotControlCommandType::Attack, {guid}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "say")
    {
        std::string msg;
        std::getline(iss, msg);
        BotControlCommand command = {BotControlCommandType::Say, {msg}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "loot")
    {
        BotControlCommand command = {BotControlCommandType::Loot, {}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "follow")
    {
        BotControlCommand command = {BotControlCommandType::Follow, {}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "stop")
    {
        BotControlCommand command = {BotControlCommandType::Stop, {}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "acceptquest")
    {
        uint32 questId;
        iss >> questId;
        BotControlCommand command = {BotControlCommandType::AcceptQuest, {std::to_string(questId)}};
        return HandleBotControlCommand(bot, command);
    }
    else if (cmd == "turninquest")
    {
        uint32 questId;
        iss >> questId;
        BotControlCommand command = {BotControlCommandType::TurnInQuest, {std::to_string(questId)}};
        return HandleBotControlCommand(bot, command);
    }
    return false;
}
