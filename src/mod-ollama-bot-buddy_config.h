#pragma once
#include "ScriptMgr.h"
#include <string>

extern bool g_EnableOllamaBotControl;
extern std::string g_OllamaBotControlUrl;
extern std::string g_OllamaBotControlModel;

class OllamaBotControlConfigWorldScript : public WorldScript
{
public:
    OllamaBotControlConfigWorldScript();
    void OnStartup() override;
};
