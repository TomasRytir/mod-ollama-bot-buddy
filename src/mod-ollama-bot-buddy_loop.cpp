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
        if (c->IsHostileTo(bot)) type = "ENEMY";
        else if (c->IsFriendlyTo(bot)) type = "FRIENDLY";
        else type = "NEUTRAL";

        std::string questGiver = "";
        if (c->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER)) {
            questGiver = " [QUEST GIVER]";
        }

        float dist = bot->GetDistance(c);
        visible.push_back(fmt::format(
            "{}: {}{} (guid: {}, level: {}, hp: {}/{}, position: {} {} {}, distance: {:.1f})",
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

    // GameObject logic (as in your current function)
    for (auto const& pair : map->GetGameObjectBySpawnIdStore())
    {
        GameObject* go = pair.second;
        if (!go) continue;
        if (!bot->IsWithinDistInMap(go, radius)) continue;
        if (!bot->IsWithinLOS(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ())) continue;
        float dist = bot->GetDistance(go);
        visible.push_back(fmt::format(
            "{} (guid: {}, type: {}, position: {} {} {}, distance: {:.1f})",
            go->GetName(),
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
        // Scan all units in range to see who is targeting the bot as their victim
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

    if (inCombat)
    {
        oss << "IN COMBAT: ";
        if (victim)
        {
            oss << "Target: " << victim->GetName()
                << " (guid: " << victim->GetGUID().GetCounter() << ")"
                << ", level: " << victim->GetLevel()
                << ", hp: " << victim->GetHealth() << "/" << victim->GetMaxHealth();
        }
        else
        {
            oss << "No current target";
        }
        oss << ". ";

        // Show who is attacking the bot, if anyone
        if (attacker)
        {
            oss << "Under attack by: " << attacker->GetName()
                << " (guid: " << attacker->GetGUID().GetCounter() << ")"
                << ", level: " << attacker->GetLevel()
                << ", hp: " << attacker->GetHealth() << "/" << attacker->GetMaxHealth()
                << ". ";
        }

        oss << "Bot HP: " << bot->GetHealth() << "/" << bot->GetMaxHealth();
        oss << ", Mana: " << bot->GetPower(POWER_MANA) << "/" << bot->GetMaxPower(POWER_MANA);
        oss << ", Energy: " << bot->GetPower(POWER_ENERGY) << "/" << bot->GetMaxPower(POWER_ENERGY);
    }
    else
    {
        oss << "NOT IN COMBAT. HP: " << bot->GetHealth() << "/" << bot->GetMaxHealth();
        oss << ", Mana: " << bot->GetPower(POWER_MANA) << "/" << bot->GetMaxPower(POWER_MANA);
        oss << ", Energy: " << bot->GetPower(POWER_ENERGY) << "/" << bot->GetMaxPower(POWER_ENERGY);
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
    oss << "Group status: " << botGroupStatus << "\n";
    oss << "Gold: " << botGold << "\n";
    oss << "Area: " << botAreaName << "\n";
    oss << "Zone: " << botZoneName << "\n";
    oss << "Map: " << botMapName << "\n";
    oss << "Position: " << bot->GetPositionX() << " " << bot->GetPositionY() << " " << bot->GetPositionZ() << "\n";
    oss << GetCombatSummary(bot) << "\n\n";

    oss << "Active quests:\n";
    for (auto const& qs : bot->getQuestStatusMap())
    {
        oss << "Quest " << qs.first << " status " << qs.second.Status << "\n";
    }

    std::vector<std::string> losLocs = GetVisibleLocations(bot);
    std::vector<std::string> wps = GetNearbyWaypoints(bot);

    if (!losLocs.empty()) {
        oss << "Visible locations in line of sight:\n";
        for (const auto& entry : losLocs) oss << " - " << entry << "\n";
    }
    if (!wps.empty()) {
        oss << "Nearby navigation waypoints:\n";
        for (const auto& entry : wps) oss << " - " << entry << "\n";
    }
    if (!losLocs.empty() || !wps.empty()) {
        oss << "You must select one of these locations or waypoints to move to, or choose a new unexplored spot.\n";
    }

    oss << "Primary goal: Level to 80 and equip the best gear. Prioritize combat, questing, and efficient progression. If no quests or grind targets are present, explore for new quests, run dungeons or raids, or improve professions and gold.\n";
    oss << "If you are being attacked, always prioritize defending yourself or escaping immediate danger, using the most effective available action.\n";
    oss << "When selecting a target to attack, always move to the target if it is not in immediate range, and only attack after you are close enough to do so.\n";
    oss << "Select the single most effective action to increase level, complete quests, or obtain gear. Only reply with ONE command, strictly following these formats:\n";
    oss << " - move to <x> <y> <z>\n";
    oss << " - attack <guid>\n";
    oss << " - interact <guid>\n";
    oss << " - loot\n";
    oss << " - say <message>\n";
    oss << " - acceptquest <id>\n";
    oss << " - turninquest <id>\n";
    oss << " - stop\n";
    oss << "\n";
    oss << "You may only use commands that match the GUIDs, waypoints, or options listed in the visible objects and navigation section. Never invent GUIDs or coordinates. For example, to attack a creature with 'guid: 2241', use: attack 2241. To interact with a game object with 'guid: 1772', use: interact 1772.\n";
    oss << "\n";
    oss << "If no valid creatures, objects, or quests are visible, choose a waypoint or unexplored coordinates from the list and use move to <x> <y> <z>.\n";
    oss << "\n";
    oss << "When pursuing any action, prefer targets and objectives that are closest to you and most directly advance the main goal: combat, questing, interaction, or movement toward progress. Move to distant targets before engaging if required. Ignore distractions. Do not idle or repeat commands. Do not choose actions with no available targets.\n";
    oss << "\n";
    oss << "Using the say <message> command to talk to real humans sometimes and let them know what you're doing, your current goals and progress helps advance your main goal as well.\n";
    oss << "\n";
    oss << "IMPORTANT: Your response must be a single command ONLY. Do not return explanations, summaries, or extra text.\n";


    if (g_EnableOllamaBotBuddyDebug)
    {
        //LOG_INFO("server.loading", "[OllamaBotBuddy] Bot prompt for '{}': {}", botName, oss.str());
    }
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
                LOG_INFO("server.loading", "[OllamaBotBuddy] Sending prompt for bot '{}': {}", botName, prompt);
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
