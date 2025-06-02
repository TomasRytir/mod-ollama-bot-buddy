#include "mod-ollama-bot-buddy_api.h"
#include "mod-ollama-bot-buddy_config.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "Log.h"
#include <sstream>

namespace BotBuddyAI
{
    bool MoveTo(Player* bot, float x, float y, float z)
    {
        if (!bot) return false;
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' moving to ({}, {}, {})", bot->GetName(), x, y, z);
        }
        bot->GetMotionMaster()->MovePoint(0, x, y, z);
        return true;
    }

    bool Attack(Player* bot, ObjectGuid guid)
    {
        if (!bot || !guid)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot or GUID invalid for attack.");
            return false;
        }

        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] No PlayerbotAI for '{}'", bot->GetName());
            return false;
        }

        // Find valid target (Creature or Player)
        Unit* target = nullptr;

        if (Creature* creature = ObjectAccessor::GetCreature(*bot, guid))
        {
            target = creature;
        }
        else if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        {
            target = player;
        }

        if (!target)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Target with GUID {} not found or not attackable.", guid.GetCounter());
            return false;
        }

        if (!bot->IsHostileTo(target) || !bot->IsWithinLOSInMap(target))
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Target with GUID {} not attackable.", guid.GetCounter());
            return false;
        }

        // Reset AI (clear queued actions and strategies)
        ai->Reset();

        // Select the target
        bot->SetSelection(target->GetGUID());

        // PlayerbotAI: force the attack action
        bool result = ai->DoSpecificAction("attack", Event(), false, std::to_string(target->GetGUID().GetRawValue()));

        LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' attacking GUID {} result={}", bot->GetName(), guid.GetCounter(), result);

        return result;
    }


    bool Interact(Player* bot, ObjectGuid guid)
    {
        if (!bot || !guid)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot or GUID invalid for interact.");
            return false;
        }

        // Try Creature first
        if (Creature* creature = ObjectAccessor::GetCreature(*bot, guid))
        {
            if (g_EnableOllamaBotBuddyDebug)
            {
                LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' interacting with creature '{}'", bot->GetName(), creature->GetName());
            }
            // Try gossip (questgiver, vendor, etc.)
            creature->AI()->sGossipHello(bot);
            // perform additional interaction steps (quest/loot/vendor/etc.)
            return true;
        }
        // Try GameObject
        else if (GameObject* go = ObjectAccessor::GetGameObject(*bot, guid))
        {
            if (g_EnableOllamaBotBuddyDebug)
            {
                LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' interacting with gameobject '{}'", bot->GetName(), go->GetName());
            }
            // Try using/opening/interacting with the game object
            go->Use(bot);
            return true;
        }
        else
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Interact: No creature or gameobject found with GUID {}", guid.GetCounter());
            return false;
        }
    }

    bool CastSpell(Player* bot, uint32 spellId, Unit* target)
    {
        if (!bot) return false;
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        if (!target) target = bot->GetVictim();
        if (!target) return false;
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' casting spell {} on target '{}'", bot->GetName(), spellId, target->GetName());
        }
        ai->CastSpell(spellId, target);
        return true;
    }

    bool Say(Player* bot, const std::string& msg)
    {
        if (!bot) return false;
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' says: {}", bot->GetName(), msg);
        }
        bot->Say(msg, LANG_UNIVERSAL);
        return true;
    }

    bool FollowMaster(Player* bot)
    {
        if (!bot) return false;
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' following master.", bot->GetName());
        }
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        ai->TellMasterNoFacing("follow");
        return true;
    }

    bool StopMoving(Player* bot)
    {
        if (!bot) return false;
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' stopping movement.", bot->GetName());
        }
        bot->StopMoving();
        return true;
    }

    bool AcceptQuest(Player* bot, uint32 questId)
    {
        if (!bot) return false;
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' accepting quest {}", bot->GetName(), questId);
        }
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
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' turning in quest {}", bot->GetName(), questId);
        }
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
        if (g_EnableOllamaBotBuddyDebug)
        {
            LOG_INFO("server.loading", "[OllamaBotBuddy] Bot '{}' looting nearby.", bot->GetName());
        }
        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!ai) return false;
        ai->TellMasterNoFacing("loot");
        return true;
    }
} // namespace BotBuddyAI

bool HandleBotControlCommand(Player* bot, const BotControlCommand& command)
{
    if (g_EnableOllamaBotBuddyDebug && bot)
    {
        LOG_INFO("server.loading", "[OllamaBotBuddy] HandleBotControlCommand for '{}', type {}", bot->GetName(), int(command.type));
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
