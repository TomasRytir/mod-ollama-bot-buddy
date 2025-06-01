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
        LOG_INFO("server.loading", "Failed to initialize cURL.");
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
        LOG_INFO("server.loading", "Failed to reach Ollama AI. cURL error: {}", curl_easy_strerror(res));
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
    PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
    if (!ai) return "";

    std::ostringstream oss;
    oss << "Bot state summary:\n";
    oss << "Name: " << bot->GetName() << "\n";
    oss << "Level: " << bot->GetLevel() << "\n";
    oss << "Class: " << uint32(bot->getClass()) << "\n";
    oss << "Race: " << uint32(bot->getRace()) << "\n";
    oss << "Zone: " << bot->GetZoneId() << "\n";
    oss << "Location: " << bot->GetPositionX() << " " << bot->GetPositionY() << " " << bot->GetPositionZ() << "\n";

    oss << "Active quests:\n";
    for (auto const& qs : bot->getQuestStatusMap())
    {
        oss << "Quest " << qs.first << " status " << qs.second.Status << "\n";
    }

    oss << "Current focus: Level to 80 and gear up. If not questing or grinding, explore, find new quests, run dungeons or raid if possible. Increase professions or farm gold if needed.\n";
    oss << "Decide the most optimal action to further the bot's progress to level 80 and epic gear. Only respond with a single step action in the following command format: move to x y z, attack guid, interact guid, acceptquest id, turninquest id, loot, say message, stop, etc.\n";
    return oss.str();
}

void OllamaBotControlLoop::OnUpdate(uint32 diff)
{
    if (!g_EnableOllamaBotControl) return;
    static uint32_t timer = 0;
    if (timer <= diff)
    {
        timer = 3000; // 3 seconds
        for (auto const& itr : ObjectAccessor::GetPlayers())
        {
            Player* bot = itr.second;
            if (!bot->IsInWorld()) continue;
            PlayerbotAI* ai = sPlayerbotsMgr->GetPlayerbotAI(bot);
            if (!ai) continue;
            std::string botName = bot->GetName();
            if (botName != "OllamaTest")
            {
                continue;
            }

            uint64_t guid = bot->GetGUID().GetRawValue();
            time_t now = time(nullptr);

            if (nextTick.find(guid) == nextTick.end())
            {
                nextTick[guid] = now;
                continue;
            }
            if (now < nextTick[guid]) continue;
            nextTick[guid] = now + 5; // 5 second cycle per bot

            std::string prompt = BuildBotPrompt(bot);
            std::thread([bot, prompt]() {
                std::string llmReply = QueryOllamaLLM(prompt);
                if (llmReply.empty()) return;
                ParseBotControlCommand(bot, llmReply);
            }).detach();
        }
    }
    else
    {
        timer -= diff;
    }
}
