#include "mod-ollama-bot-buddy_handler.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "ObjectAccessor.h"
#include <mutex>
#include <algorithm>
#include <chrono>
#include <tuple>
#include <unordered_map>

std::unordered_map<uint64_t, std::deque<std::pair<std::string, std::string>>> botPlayerMessages;
std::mutex botPlayerMessagesMutex;

// (botGUID, playerName) -> (lastMsg, lastTime)
std::unordered_map<std::tuple<uint64_t, std::string>, std::pair<std::string, std::chrono::steady_clock::time_point>> botPlayerLastMsg;

void BotBuddyChatHandler::OnPlayerChat(Player* player, uint32_t type, uint32_t lang, std::string& msg)
{
    ProcessChat(player, type, lang, msg, nullptr);
}
void BotBuddyChatHandler::OnPlayerChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Group* /*group*/)
{
    ProcessChat(player, type, lang, msg, nullptr);
}
void BotBuddyChatHandler::OnPlayerChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Channel* channel)
{
    ProcessChat(player, type, lang, msg, channel);
}

void BotBuddyChatHandler::ProcessChat(Player* player, uint32_t type, uint32_t lang, std::string& msg, Channel* channel)
{
    LOG_INFO("server.loading", "ProcessChat: sender={} type={} lang={} msg='{}' channel={}", 
    player ? player->GetName() : "NULL", type, lang, msg, channel ? channel->GetName() : "nullptr");

    if (!player || msg.empty()) return;
    PlayerbotAI* senderAI = sPlayerbotsMgr->GetPlayerbotAI(player);
    if (senderAI && senderAI->IsBotAI()) return;

    std::lock_guard<std::mutex> lock(botPlayerMessagesMutex);

    auto const& allPlayers = ObjectAccessor::GetPlayers();
    for (auto const& itr : allPlayers)
    {
        Player* bot = itr.second;
        if (!bot || !bot->IsAlive()) continue;

        PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);
        if (!botAI || !botAI->IsBotAI()) continue;

        std::string messageLower = msg;
        std::string botNameLower = bot->GetName();
        std::transform(messageLower.begin(), messageLower.end(), messageLower.begin(), ::tolower);
        std::transform(botNameLower.begin(), botNameLower.end(), botNameLower.begin(), ::tolower);

        if (messageLower.find(botNameLower) != std::string::npos)
        {
            auto botKey = std::make_tuple(bot->GetGUID().GetRawValue(), player->GetName());
            auto now = std::chrono::steady_clock::now();
            bool isSpam = false;
            {
                // Anti-spam: Ignore same message from same player to same bot within 30 seconds
                auto it = botPlayerLastMsg.find(botKey);
                if (it != botPlayerLastMsg.end()) {
                    auto& [lastMsg, lastTime] = it->second;
                    if (lastMsg == msg && std::chrono::duration_cast<std::chrono::seconds>(now - lastTime).count() < 30) {
                        isSpam = true;
                    }
                }
                // Always update last message/time
                botPlayerLastMsg[botKey] = {msg, now};
            }
            if (!isSpam) {
                botPlayerMessages[bot->GetGUID().GetRawValue()].emplace_back(player->GetName(), msg);
            }
        }
    }
}
