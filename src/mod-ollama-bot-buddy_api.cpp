#include "mod-ollama-bot-buddy_api.h"
#include "mod-ollama-bot-buddy_config.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Chat.h"
#include "Log.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Cell.h"
#include "Map.h"
#include <sstream>
#

namespace BotBuddyAI
{
    bool MoveTo(Player* bot, float x, float y, float z)
    {
        if (!bot) return false;
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MovePoint(0, x, y, z);
        return true;
    }

    bool Attack(Player* bot, ObjectGuid guid)
    {
        if (!bot || !guid) return false;

        Unit* target = ObjectAccessor::GetUnit(*bot, guid);
        if (!target || !bot->IsHostileTo(target) || !bot->IsWithinLOSInMap(target)) return false;

        bot->SetSelection(target->GetGUID());
        bot->SetFacingToObject(target);
        bot->Attack(target, true);
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveChase(target);

        return true;
    }

    bool Interact(Player* bot, ObjectGuid guid)
    {
        if (!bot || !guid) return false;

        if (Creature* creature = ObjectAccessor::GetCreature(*bot, guid))
        {
            bot->SetFacingToObject(creature);
            creature->AI()->sGossipHello(bot);
            return true;
        }
        else if (GameObject* go = ObjectAccessor::GetGameObject(*bot, guid))
        {
            bot->SetFacingToObject(go);
            go->Use(bot);
            return true;
        }
        return false;
    }

    bool CastSpell(Player* bot, uint32 spellId, Unit* target)
    {
        if (!bot) return false;
        if (!target) target = bot->GetVictim();
        if (!target) return false;

        bot->SetFacingToObject(target);
        bot->CastSpell(target, spellId, false);
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
        Player* master = ai->GetMaster();
        if (!master) return false;

        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveFollow(master, 1.0f, 0.0f);
        return true;
    }

    bool StopMoving(Player* bot)
    {
        if (!bot) return false;
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();
        return true;
    }

    bool AcceptQuest(Player* bot, uint32 questId)
    {
        if (!bot) return false;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest) return false;
        bot->AddQuest(quest, nullptr);
        return true;
    }

    bool TurnInQuest(Player* bot, uint32 questId)
    {
        if (!bot) return false;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest) return false;
        bot->CompleteQuest(questId);
        bot->RewardQuest(quest, 0, bot);
        return true;
    }

    bool LootNearby(Player* bot)
    {
        if (!bot) return false;

        Map* map = bot->GetMap();
        if (!map) return false;

        for (const auto& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* creature = pair.second;
            if (!creature || !creature->isDead()) continue;

            if (!bot->IsWithinDistInMap(creature, INTERACTION_DISTANCE)) continue;

            bot->SetFacingToObject(creature);
            bot->PrepareGossipMenu(creature, creature->GetCreatureTemplate()->GossipMenuId);
            bot->SendLoot(creature->GetGUID(), LOOT_CORPSE);
            return true;
        }

        return false;
    }

} // namespace BotBuddyAI

bool HandleBotControlCommand(Player* bot, const BotControlCommand& command)
{
    if (g_EnableOllamaBotBuddyDebug && bot)
    {
        LOG_INFO("server.loading", "[OllamaBotBuddy] HandleBotControlCommand for '{}', type {}", bot->GetName(), int(command.type));
        LOG_INFO("server.loading", "[OllamaBotBuddy] ================================================================================================");
    }
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
                uint32 lowGuid = std::stoul(command.args[0]);

                // Try to find the Creature by LowGuid first
                Creature* creatureTarget = nullptr;
                for (auto const& pair : bot->GetMap()->GetCreatureBySpawnIdStore())
                {
                    Creature* c = pair.second;
                    if (!c) continue;
                    if (c->GetGUID().GetCounter() == lowGuid)
                    {
                        creatureTarget = c;
                        break;
                    }
                }

                if (creatureTarget)
                {
                    // Use the actual GUID from the creature, never reconstruct!
                    return BotBuddyAI::Attack(bot, creatureTarget->GetGUID());
                }

                // If not found, try Player
                ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
                Player* playerTarget = ObjectAccessor::FindConnectedPlayer(guid);
                if (playerTarget)
                {
                    return BotBuddyAI::Attack(bot, playerTarget->GetGUID());
                }

                LOG_INFO("server.loading", "[OllamaBotBuddy] Could not find target with lowGuid {}", lowGuid);
                return false;
            }
            break;
        case BotControlCommandType::Interact:
            if (!command.args.empty())
            {
                uint32 lowGuid = std::stoul(command.args[0]);
                Creature* creatureTarget = nullptr;
                GameObject* goTarget = nullptr;

                // Find creature by LowGuid
                for (auto const& pair : bot->GetMap()->GetCreatureBySpawnIdStore())
                {
                    Creature* c = pair.second;
                    if (!c) continue;
                    if (c->GetGUID().GetCounter() == lowGuid)
                    {
                        creatureTarget = c;
                        break;
                    }
                }

                if (creatureTarget)
                {
                    return BotBuddyAI::Interact(bot, creatureTarget->GetGUID());
                }

                // Find gameobject by LowGuid
                for (auto const& pair : bot->GetMap()->GetGameObjectBySpawnIdStore())
                {
                    GameObject* go = pair.second;
                    if (!go) continue;
                    if (go->GetGUID().GetCounter() == lowGuid)
                    {
                        goTarget = go;
                        break;
                    }
                }

                if (goTarget)
                {
                    return BotBuddyAI::Interact(bot, goTarget->GetGUID());
                }

                LOG_INFO("server.loading", "[OllamaBotBuddy] Could not find interact target with lowGuid {}", lowGuid);
                return false;
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
    if (g_EnableOllamaBotBuddyDebug && bot)
    {
        LOG_INFO("server.loading", "[OllamaBotBuddy] ParseBotControlCommand for '{}': {}", bot->GetName(), commandStr);
    }
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
    else if (cmd == "interact")
    {
        std::string guid;
        iss >> guid;
        BotControlCommand command = {BotControlCommandType::Interact, {guid}};
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
