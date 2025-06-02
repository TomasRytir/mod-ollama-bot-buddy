#include "mod-ollama-bot-buddy_loop.h"
#include "mod-ollama-bot-buddy_config.h"
#include "mod-ollama-bot-buddy_api.h"
#include "PlayerbotMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Log.h"
#include <thread>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <ctime>
#include "Creature.h"
#include "GameObject.h"
#include "TravelMgr.h"
#include "TravelNode.h"
#include <atomic>
#include <unordered_map>
#include <iomanip>
#include "GameObjectData.h"
#include "GameObject.h"

std::vector<std::string> GetGroupStatus(Player* bot)
{
    std::vector<std::string> info;
    if (!bot || !bot->GetGroup()) return info;

    Group* group = bot->GetGroup();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->GetMap()) continue;

        if(bot == member)
        {
            continue; // Skip the bot itself
        }

        float dist = bot->GetDistance(member);
        std::string beingAttacked = "";

        if (Unit* attacker = member->GetVictim())
        {
            beingAttacked = fmt::format(
                " [Under Attack by {} (guid: {}, Level: {}, HP: {}/{})]",
                attacker->GetName(),
                attacker->GetGUID().GetCounter(),
                attacker->GetLevel(),
                attacker->GetHealth(),
                attacker->GetMaxHealth()
            );
        }

        info.push_back(fmt::format(
            "{} (guid: {}, Level: {}, HP: {}/{}, Pos: {} {} {}, Dist: {:.1f}){}",
            member->GetName(),
            member->GetGUID().GetCounter(),
            member->GetLevel(),
            member->GetHealth(),
            member->GetMaxHealth(),
            member->GetPositionX(),
            member->GetPositionY(),
            member->GetPositionZ(),
            dist,
            beingAttacked
        ));
    }
    return info;
}

static std::string GetProfessionTagFromChest(uint32 entry)
{
    switch (entry)
    {
        case 1617: return " [Herbalism: Peacebloom]";
        case 1618: return " [Herbalism: Silverleaf]";
        case 1620: return " [Herbalism: Briarthorn]";
        case 1621: return " [Herbalism: Bruiseweed]";
        case 1731: return " [Mining: Copper Vein]";
        case 1732: return " [Mining: Tin Vein]";
        case 1733: return " [Mining: Silver Vein]";
        case 1735: return " [Mining: Iron Deposit]";
        case 2040: return " [Mining: Mithril Deposit]";
        case 2047: return " [Mining: Truesilver Deposit]";
        case 324:  return " [Mining: Small Thorium Vein]";
        case 175404: return " [Alchemy Lab]";
        default: return "";
    }
}



// Gather visible objects (creatures/gameobjects) around the bot with LOS check
std::vector<std::string> GetVisibleLocations(Player* bot, float radius = 100.0f)
{
    std::vector<std::string> visible;
    if (!bot || !bot->GetMap()) return visible;
    Map* map = bot->GetMap();

    for (auto const& pair : map->GetCreatureBySpawnIdStore())
    {
        Creature* c = pair.second;
        if (!c) continue;
        if (c->GetGUID() == bot->GetGUID()) continue;
        if (!bot->IsWithinDistInMap(c, radius)) continue;
        if (!bot->IsWithinLOS(c->GetPositionX(), c->GetPositionY(), c->GetPositionZ())) continue;
        if (c->IsPet() || c->IsTotem()) continue;

        std::string type;
        if (c->isDead())
        {
            type = "DEAD";
            if (c->hasLootRecipient() && (c->GetLootRecipient() == bot || (c->GetLootRecipientGroup() && bot->GetGroup() == c->GetLootRecipientGroup())))
            {
                type = "DEAD (LOOTABLE)";
            }
            else
            {
                continue;
            }
            if(!c->hasLootRecipient())
            {
                if (c->GetCreatureTemplate() && c->GetCreatureTemplate()->SkinLootId)
                {
                    type += " [SKINNABLE]";
                }
            }
        }
        else if (c->IsHostileTo(bot)) type = "ENEMY";
        else if (c->IsFriendlyTo(bot)) type = "FRIENDLY";
        else type = "NEUTRAL";

        std::string questGiver = "";
        if (c->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER)) {
            questGiver = " [QUEST GIVER]";
        }

        float dist = bot->GetDistance(c);
        visible.push_back(fmt::format(
            "{}: {}{} (guid: {}, Level: {}, HP: {}/{}, Position: {} {} {}, Distance: {:.1f})",
            type,
            c->GetName(),
            questGiver,
            c->GetGUID().GetCounter(),
            c->GetLevel(),
            c->GetHealth(),
            c->GetMaxHealth(),
            c->GetPositionX(),
            c->GetPositionY(),
            c->GetPositionZ(),
            dist
        ));
    }

    for (auto const& pair : map->GetGameObjectBySpawnIdStore())
    {
        GameObject* go = pair.second;
        if (!go) continue;
        if (!bot->IsWithinDistInMap(go, radius)) continue;
        if (!bot->IsWithinLOS(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ())) continue;

        std::string tag = "";

        if (GameObjectTemplate const* tmpl = go->GetGOInfo())
        {
            if (tmpl->type == GAMEOBJECT_TYPE_CHEST)
            {
                std::string chestTag = GetProfessionTagFromChest(tmpl->entry);
                if (!chestTag.empty())
                    tag = chestTag;
            }
        }
        
        float dist = bot->GetDistance(go);
        visible.push_back(fmt::format(
            "{}{} (guid: {}, Type: {}, Position: {} {} {}, Distance: {:.1f})",
            go->GetName(),
            tag,
            go->GetGUID().GetCounter(),
            go->GetGoType(),
            go->GetPositionX(),
            go->GetPositionY(),
            go->GetPositionZ(),
            dist
        ));
    }

    return visible;
}

std::string GetCombatSummary(Player* bot)
{
    std::ostringstream oss;
    bool inCombat = bot->IsInCombat();
    Unit* victim = bot->GetVictim();

    // Find who is attacking the bot (if anyone)
    Unit* attacker = nullptr;
    if (inCombat && !victim)
    {
        Map* map = bot->GetMap();
        if (map)
        {
            for (auto const& pair : map->GetCreatureBySpawnIdStore())
            {
                Creature* c = pair.second;
                if (!c) continue;
                if (c->GetVictim() == bot)
                {
                    attacker = c;
                    break;
                }
            }
        }
    }

    auto safe_name = [](Unit* unit) -> std::string { return unit ? unit->GetName() : "?"; };
    auto safe_guid = [](Unit* unit) -> std::string { return unit ? std::to_string(unit->GetGUID().GetCounter()) : "?"; };
    auto safe_level = [](Unit* unit) -> std::string { return unit ? std::to_string(unit->GetLevel()) : "?"; };
    auto safe_hp = [](Unit* unit) -> std::string { return unit ? std::to_string(unit->GetHealth()) : "?"; };
    auto safe_maxhp = [](Unit* unit) -> std::string { return unit ? std::to_string(unit->GetMaxHealth()) : "?"; };

    if (inCombat)
    {
        oss << "IN COMBAT: ";
        if (victim)
        {
            oss << "Target: " << safe_name(victim)
                << " (guid: " << safe_guid(victim) << ")"
                << ", Level: " << safe_level(victim)
                << ", HP: " << safe_hp(victim) << "/" << safe_maxhp(victim);
        }
        else
        {
            oss << "No current target";
        }
        oss << ". ";

        if (attacker)
        {
            float dist = bot && attacker ? bot->GetDistance(attacker) : -1.0f;

            Creature* c = dynamic_cast<Creature*>(attacker);
            Player* p = dynamic_cast<Player*>(attacker);

            oss << "DEFEND YOURSELF, YOU ARE UNDER ATTACK BY: ";
            if (c)
            {
                // Creature-specific info
                oss << "Creature '" << safe_name(c)
                    << "' (guid: " << safe_guid(c) << ")"
                    << ", Level: " << safe_level(c)
                    << ", HP: " << safe_hp(c) << "/" << safe_maxhp(c)
                    << ", Distance: " << (dist >= 0 ? (std::ostringstream() << std::fixed << std::setprecision(1) << dist).str() : "?")
                    << ", Elite: " << (c->isElite() ? "Yes" : "No");

                // Show auras/buffs/debuffs
                oss << ", Auras:";
                bool anyAura = false;
                for (auto& auraPair : c->GetOwnedAuras())
                {
                    if (!anyAura) anyAura = true;
                    oss << " " << auraPair.second->GetSpellInfo()->SpellName[0];
                }
                if (!anyAura) oss << " None";
            }
            else if (p)
            {
                // Player-specific info
                std::string pFaction = (p->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
                oss << "Player '" << safe_name(p)
                    << "' (guid: " << safe_guid(p) << ")"
                    << ", Level: " << safe_level(p)
                    << ", HP: " << safe_hp(p) << "/" << safe_maxhp(p)
                    << ", Distance: " << (dist >= 0 ? (std::ostringstream() << std::fixed << std::setprecision(1) << dist).str() : "?")
                    << ", Faction: " << pFaction
                    << ", Class: " << std::to_string(p->getClass())
                    << ", Race: " << std::to_string(p->getRace());

                // Show auras/buffs/debuffs
                oss << ", Auras:";
                bool anyAura = false;
                for (auto& auraPair : p->GetOwnedAuras())
                {
                    if (!anyAura) anyAura = true;
                    oss << " " << auraPair.second->GetSpellInfo()->SpellName[0];
                }
                if (!anyAura) oss << " None";
            }
            else
            {
                // Unknown Unit type
                oss << safe_name(attacker)
                    << " (guid: " << safe_guid(attacker) << ")"
                    << ", Level: " << safe_level(attacker)
                    << ", HP: " << safe_hp(attacker) << "/" << safe_maxhp(attacker)
                    << ", Distance: " << (dist >= 0 ? (std::ostringstream() << std::fixed << std::setprecision(1) << dist).str() : "?");
            }

            oss << ". ";
        }

        oss << "Your HP: " << (bot ? std::to_string(bot->GetHealth()) : "?") << "/" << (bot ? std::to_string(bot->GetMaxHealth()) : "?");
        oss << ", Mana: " << (bot ? std::to_string(bot->GetPower(POWER_MANA)) : "?") << "/" << (bot ? std::to_string(bot->GetMaxPower(POWER_MANA)) : "?");
        oss << ", Energy: " << (bot ? std::to_string(bot->GetPower(POWER_ENERGY)) : "?") << "/" << (bot ? std::to_string(bot->GetMaxPower(POWER_ENERGY)) : "?");
    }
    else
    {
        oss << "NOT IN COMBAT. Your HP: " << (bot ? std::to_string(bot->GetHealth()) : "?") << "/" << (bot ? std::to_string(bot->GetMaxHealth()) : "?");
        oss << ", Mana: " << (bot ? std::to_string(bot->GetPower(POWER_MANA)) : "?") << "/" << (bot ? std::to_string(bot->GetMaxPower(POWER_MANA)) : "?");
        oss << ", Energy: " << (bot ? std::to_string(bot->GetPower(POWER_ENERGY)) : "?") << "/" << (bot ? std::to_string(bot->GetMaxPower(POWER_ENERGY)) : "?");
    }
    return oss.str();
}


std::vector<std::string> GetNearbyWaypoints(Player* bot, float radius = 200.0f)
{
    std::vector<std::string> wps;
    if (!bot) return wps;
    uint32 bot_map = bot->GetMapId();
    float bot_x = bot->GetPositionX();
    float bot_y = bot->GetPositionY();
    float bot_z = bot->GetPositionZ();

    auto nodes = sTravelNodeMap->getNodes();
    int idx = 0;
    for (TravelNode* node : nodes)
    {
        if (!node) continue;
        WorldPosition* pos = node->getPosition();
        if (!pos) continue;
        if (pos->getMapId() != bot_map) continue;
        float dx = pos->getX() - bot_x;
        float dy = pos->getY() - bot_y;
        float dz = pos->getZ() - bot_z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist > radius) continue;
        wps.push_back(fmt::format("Node #{} '{}' ({:.1f}, {:.1f}, {:.1f}), distance: {:.1f}", idx, node->getName(), pos->getX(), pos->getY(), pos->getZ(), dist));        
        ++idx;
    }
    return wps;
}

OllamaBotControlLoop::OllamaBotControlLoop() : WorldScript("OllamaBotControlLoop") {}

static std::unordered_map<uint64_t, time_t> nextTick;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    std::string* responseBuffer = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    responseBuffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

static std::string QueryOllamaLLM(const std::string& prompt)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        LOG_INFO("server.loading", "[OllamaBotBuddy] Failed to initialize cURL.");
        return "";
    }

    nlohmann::json requestData = {
        {"model",  g_OllamaBotControlModel},
        {"prompt", prompt}
    };
    std::string requestDataStr = requestData.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string responseBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, g_OllamaBotControlUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestDataStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(requestDataStr.length()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        LOG_INFO("server.loading", "[OllamaBotBuddy] Failed to reach Ollama AI. cURL error: {}", curl_easy_strerror(res));
        return "";
    }

    std::stringstream ss(responseBuffer);
    std::string line, extracted;
    while (std::getline(ss, line))
    {
        try
        {
            nlohmann::json jsonResponse = nlohmann::json::parse(line);
            if (jsonResponse.contains("response"))
                extracted += jsonResponse["response"].get<std::string>();
        }
        catch (...) {}
    }
    return extracted;
}

static std::string BuildBotPrompt(Player* bot)
{
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);
    if (!botAI) return "";

    AreaTableEntry const* botCurrentArea = botAI->GetCurrentArea();
    AreaTableEntry const* botCurrentZone = botAI->GetCurrentZone();

    std::vector<std::string> groupInfo = GetGroupStatus(bot);

    std::string botName             = bot->GetName();
    uint32_t botLevel               = bot->GetLevel();
    uint8_t botGenderByte           = bot->getGender();
    std::string botAreaName         = botCurrentArea ? botAI->GetLocalizedAreaName(botCurrentArea): "UnknownArea";
    std::string botZoneName         = botCurrentZone ? botAI->GetLocalizedAreaName(botCurrentZone): "UnknownZone";
    std::string botMapName          = bot->GetMap() ? bot->GetMap()->GetMapName() : "UnknownMap";
    std::string botClass            = botAI->GetChatHelper()->FormatClass(bot->getClass());
    std::string botRace             = botAI->GetChatHelper()->FormatRace(bot->getRace());
    std::string botGender           = (botGenderByte == 0 ? "Male" : "Female");
    std::string botFaction          = (bot->GetTeamId() == TEAM_ALLIANCE ? "Alliance" : "Horde");
    std::string botGroupStatus      = (bot->GetGroup() ? "In a group" : "Solo");
    uint32_t botGold                = bot->GetMoney() / 10000;
    

    std::ostringstream oss;
    oss << "Bot state summary:\n";
    oss << "Name: " << botName << "\n";
    oss << "Level: " << botLevel << "\n";
    oss << "Class: " << botClass << "\n";
    oss << "Race: " << botRace << "\n";
    oss << "Gender: " << botGender << "\n";
    oss << "Faction: " << botFaction << "\n";
    oss << "Gold: " << botGold << "\n";
    oss << "Area: " << botAreaName << "\n";
    oss << "Zone: " << botZoneName << "\n";
    oss << "Map: " << botMapName << "\n";
    oss << "Position: " << bot->GetPositionX() << " " << bot->GetPositionY() << " " << bot->GetPositionZ() << "\n";

    oss << GetCombatSummary(bot) << "\n\n";

    oss << "Group status: " << botGroupStatus << "\n";
    if (!groupInfo.empty()) {
        oss << "Group members:\n";
        for (const auto& entry : groupInfo) oss << " - " << entry << "\n";
    }

    oss << "Active quests:\n";
    for (auto const& qs : bot->getQuestStatusMap())
    {
        oss << "Quest " << qs.first << " status " << qs.second.Status << "\n";
    }

    std::vector<std::string> losLocs = GetVisibleLocations(bot);
    std::vector<std::string> wps = GetNearbyWaypoints(bot);

    if (!losLocs.empty()) {
        oss << "Visible locations/objects in line of sight:\n";
        for (const auto& entry : losLocs) oss << " - " << entry << "\n";
    }
    if (!wps.empty()) {
        oss << "Nearby navigation waypoints:\n";
        for (const auto& entry : wps) oss << " - " << entry << "\n";
    }
    if (!losLocs.empty() || !wps.empty()) {
        oss << "You must select one of these locations or waypoints to move to, interact with, accept or turn in quests, attack, loot, or any other action or choose a new unexplored spot.\n";
    }

    if (g_EnableOllamaBotBuddyDebug)
    {
        LOG_INFO("server.loading", "[OllamaBotBuddy] Bot Snapshot for '{}': {}", botName, oss.str());
    }

    oss << "You are an AI-controlled bot in World of Warcraft. Your task is to follow these strict rules and reply only with the listed acceptable commands:\n";
    oss << "Primary goal: Level to 80 and equip the best gear. Prioritize combat, questing, and efficient progression. If no quests or viable enemies are nearby, explore for new quests, dungeons, raids, professions, or gold opportunities.\n";
    oss << "\n";
    oss << "COMBAT RULES:\n";
    oss << "- If you or a player in your group are under attack, IMMEDIATELY prioritize defense. Attack the enemy targeting you or your group, or escape if the enemy is much higher level.\n";
    oss << "- During combat, do NOT disengage or move away unless your HP is low or the enemy is significantly stronger.\n";
    oss << "- When choosing a target, move toward them if not in range. Use 'attack' only once you're within melee or casting distance (distance < 2).\n";
    oss << "- If you're too close to your target (distance <= 0.15) then move away before attacking again.\n";
    oss << "\n";
    oss << "DECISION RULE:\n";
    oss << "- Always choose the most effective single action to level up, complete quests, gain gear, or respond to threats.\n";
    oss << "- Only use ONE of the following commands:\n";
    oss << "   move to <x> <y> <z>\n";
    oss << "   attack <guid>\n";
    oss << "   interact <guid>\n";
    oss << "   loot\n";
    oss << "   say <message>\n";
    oss << "   acceptquest <id>\n";
    oss << "   turninquest <id>\n";
    oss << "   stop\n";
    oss << "- ANY other format or additional text reply is INVALID.\n";
    oss << "\n";
    oss << "NAVIGATION:\n";
    oss << "- Use ONLY GUIDs or coordinates listed in visible objects or navigation options.\n";
    oss << "- NEVER make up IDs, GUIDs, or coordinates.\n";
    oss << "- If nothing useful is visible, choose a waypoint or unexplored coordinate and move there.\n";
    oss << "\n";
    oss << "COMMUNICATION:\n";
    oss << "- Use 'say <message>' to announce goals, movement, or status when moving, finishing quests, entering combat, idling, or seeing nearby real players.\n";
    oss << "\n";
    oss << "IMPORTANT FORMATTING RULE:\n";
    oss << "- Respond with EXACTLY ONE command. No summaries. No commentary. No dialogue. No bullet points.\n";
    oss << "- NEVER EVER REPLY WITH ANYTHING THAT IS NOT A SINGLE COMMAND FROM THE ABOVE LIST.\n";
    oss << "\n";
    oss << "Correct examples:\n";
    oss << "   attack 2241\n";
    oss << "   move to -9347.02 256.48 65.10\n";
    oss << "   say Moving to explore new quest area\n";
    oss << "\n";
    oss << "Incorrect examples (DO NOT DO THIS):\n";
    oss << "   I will attack the enemy now\n";
    oss << "   Let's go! move to -9347.02 256.48 65.10\n";
    oss << "   Command: attack 2241\n";


    return oss.str();
}

namespace
{
    struct OllamaBotState
    {
        std::atomic<bool> busy { false };
        time_t lastRequest { 0 };
    };
    std::unordered_map<uint64_t, OllamaBotState> ollamaBotStates;
}

void OllamaBotControlLoop::OnUpdate(uint32 diff)
{
    if (!g_EnableOllamaBotControl) return;

    for (auto const& itr : ObjectAccessor::GetPlayers())
    {
        Player* bot = itr.second;
        if (!bot->IsInWorld()) continue;
        std::string botName = bot->GetName();
        if (botName != "OllamaTest") continue;

        PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (ai)
        {
            ai->ClearStrategies(BOT_STATE_COMBAT);
            ai->ClearStrategies(BOT_STATE_NON_COMBAT);
            ai->ClearStrategies(BOT_STATE_DEAD);
        }

        uint64_t guid = bot->GetGUID().GetRawValue();
        OllamaBotState& state = ollamaBotStates[guid];

        // Only process if not already waiting for LLM
        if (!state.busy)
        {
            state.busy = true;
            state.lastRequest = time(nullptr);

            std::string prompt = BuildBotPrompt(bot);

            if (g_EnableOllamaBotBuddyDebug)
            {
                //LOG_INFO("server.loading", "[OllamaBotBuddy] Sending prompt for bot '{}': {}", botName, prompt);
            }

            std::thread([bot, guid, prompt]() {
                std::string llmReply = QueryOllamaLLM(prompt);

                if (g_EnableOllamaBotBuddyDebug)
                {
                    LOG_INFO("server.loading", "[OllamaBotBuddy] LLM reply for '{}': {}", bot->GetName(), llmReply);
                }

                if (!llmReply.empty())
                {
                    ParseBotControlCommand(bot, llmReply);
                }

                // Mark ready for the next request
                ollamaBotStates[guid].busy = false;
            }).detach();
        }
    }
}
