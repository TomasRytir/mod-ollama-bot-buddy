<p align="center">
  <img src="./icon.png" alt="Ollama bot-buddy Module" title="Ollama bot-buddy Module Icon">
</p>

# AzerothCore + Playerbots Module: mod-ollama-bot-buddy

> [!CAUTION]
> NOT FOR GENERAL USE YET -
> This module is experimental and may not work, break your server or cause significant CPU load on your server due to LLM-driven bot automation. Use with caution.

## Overview

***mod-ollama-bot-buddy*** is an AzerothCore module that leverages Player Bots and integrates advanced AI control via the Ollama LLM API. This module enables true autonomous gameplay for bots—driving questing, grinding, exploration, dungeon runs, gold farming, and progression—by letting a language model interpret game state and issue real in-game actions. The ultimate goal is to have bots dynamically reach level 80, gear up, and participate in all facets of endgame content under LLM guidance.

## Features

- **Full LLM-Driven Bot Control:**  
  Playerbots are controlled by external LLM-generated commands using a robust API. Bots can be guided to level up, complete quests, explore, grind, improve professions, and raid, all according to high-level AI goals.

- **Chat-Driven Bot Control:**  
  Bots respond in real-time to player chat messages that mention the bot's name. Chat commands like "come here," "go to <NPC/object>," "interact with <NPC/object>," or "attack <target>" will immediately override all other bot logic and are prioritized above any background task or AI reasoning.

- **Comprehensive Command Set:**  
  Supports direct control via these commands (from chat, LLM, or API):  
  - `move to <x> <y> <z>`  
  - `attack <guid>`  
  - `spell <spellid> [guid]`  
  - `interact <guid>`  
  - `loot`  
  - `follow`  
  - `stop`  
  - `say <message>`  
  - `acceptquest <questId>`  
  - `turninquest <questId>`

- **PlayerbotAI Integration:**  
  Full interoperability with PlayerbotAI for bot movement, combat, questing, looting, and interaction with creatures and game objects.

- **Combat and Threat Handling:**  
  - Automatically defends self or party members when attacked.
  - Prioritizes survival and support actions if enemy level or threat is high.
  - Tracks attackers and provides context-aware responses (attack, escape, or support).

- **Quest Automation:**  
  - Accepts and turns in quests by quest ID.
  - Tracks active quest states.
  - Will interact with quest givers and objects as directed by LLM or player chat.

- **Spell Casting:**  
  - Casts spells by ID, targeting self or specific GUIDs if provided.
  - Gathers and summarizes available spells for LLM context.

- **Contextual Awareness and State Summaries:**  
  - Maintains and exposes current group status, nearby players (with details), current combat state, available spells, nearby visible creatures/objects, and navigation waypoints.
  - Tracks and reports last five executed commands for debugging and context.

- **Smart Navigation:**  
  - Lists all visible objects, game objects, and navigation waypoints within line of sight and range.
  - Selects valid GUIDs or coordinates from in-game data, never invents IDs.

- **Profession Detection:**  
  - Tags visible chests and game objects with profession requirements (Herbalism, Mining, Alchemy Lab, etc.) for more intelligent navigation and interaction.

- **Debug Logging and LLM Prompt Transparency:**  
  - Full prompt and response logging in debug mode, including real-time state snapshot for the bot.

- **API & Configurable LLM Model:**  
  - Can be configured to use a specific LLM endpoint, model, and debug state via config.

- **Thread-Safe Messaging and Command History:**  
  - Thread-safe storage of recent player messages and bot command history for consistent operation in a concurrent environment.

- **Strict Command Formatting Enforcement:**  
  - All bot actions triggered by the LLM must be a single valid command (never summaries, never multiple actions, no extra commentary).

---

## Installation

> [!IMPORTANT]
> Dependencies are verified on macOS Monterey 12.7.6 and Ubuntu 22.04 LTS. Please open an issue with your OS and steps if you hit any compatibility issues.

1. **Prerequisites:**
   - A working AzerothCore (https://github.com/liyunfan1223/azerothcore-wotlk/tree/Playerbot) with the Player Bots module (https://github.com/liyunfan1223/mod-playerbots).
   - Requires:
     - cURL (https://curl.se/libcurl/)
     - fmtlib (https://github.com/fmtlib/fmt)
     - nlohmann/json (https://github.com/nlohmann/json)
     - Ollama LLM API server (https://ollama.com), running locally or accessible over your network.

2. **Clone the Module:**
   cd /path/to/azerothcore/modules
   git clone https://github.com/DustinHendrickson/mod-ollama-bot-buddy.git

3. **Recompile AzerothCore:**
   cd /path/to/azerothcore
   mkdir build && cd build
   cmake ..
   make -j$(nproc)

4. **Configuration:**
   Copy the sample config and adjust as needed:
   cp /path/to/azerothcore/modules/mod-ollama-bot-buddy/mod-ollama-bot-buddy.conf.dist /path/to/azerothcore/etc/config/mod-ollama-bot-buddy.conf

5. **Restart the Server:**
   ./worldserver

## Configuration Options

All configuration is in `mod-ollama-bot-buddy.conf`. Key settings:

- **Ollamabot-buddy.Enable:**  
  Enable/disable the module (default: `1`)

- **Ollamabot-buddy.Url:**  
  Endpoint for Ollama API (`http://localhost:11434/api/generate` by default)

- **Ollamabot-buddy.Model:**  
  LLM model used for decision making (default: `llama3.2:1b`)

Other options may be added as the project evolves.

## How It Works

1. **Bot Selection:**  
   Only bots with a configured name (e.g., "Ollamatest") will be LLM-controlled. Change the name in the source to target other bots.

2. **State Prompt Generation:**  
   Every few seconds, the module summarizes the bot's current state, inventory, quests, and surroundings and sends this to the LLM.

3. **LLM Action Decision:**  
   The LLM responds with a single action (such as move to X Y Z, attack GUID, acceptquest ID, etc.).

4. **Command Parsing & Execution:**  
   The returned command string is parsed and mapped to the Playerbot's API. The bot then performs the corresponding action.

5. **Iterative Goal Progression:**  
   This loop continues, enabling bots to progress toward their high-level objectives.

## Debugging

Enable verbose logging in your worldserver for detailed insight into LLM requests, responses, and parsed actions.

## Troubleshooting

- If your bots do not respond, check that their names match the control string in the loop.
- Ensure the Ollama server is running and reachable from your server.
- Check your build includes all dependencies (curl, fmt, nlohmann/json).

## License

This module is released under the GNU GPL v3 license, consistent with AzerothCore's licensing.

## Contribution

Developed by Dustin Hendrickson

Pull requests and feedback are welcome. Please follow AzerothCore's coding and contribution standards.
