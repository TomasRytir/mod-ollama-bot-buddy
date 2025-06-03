#pragma once
#include "ScriptMgr.h"

class OllamaBotControlLoop : public WorldScript
{
public:
    OllamaBotControlLoop();
    void OnUpdate(uint32 diff) override;
};

void AddBotCommandHistory(Player* bot, const std::string& command);
