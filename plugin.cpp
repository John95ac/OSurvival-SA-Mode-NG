#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <shlobj.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
namespace logger = SKSE::log;

struct SKSELogsPaths {
    fs::path primary;
    fs::path secondary;
};

struct PluginConfig {
    struct {
        bool enabled = true;
        int amount = 300;
        int intervalMinutes = 7;
        bool showNotification = true;
    } gold;

    struct {
        bool enabled = true;
        int reductionAmountHunger = 100;
        int reductionAmountCold = 100;
        int reductionAmountExhaustion = 100;
        int intervalSeconds = 60;
        int activationThreshold = 100;
        bool showNotification = true;
    } survival;

    struct {
        bool enabled = true;
        int restorationAmount = 50;
        int intervalSeconds = 120;
        bool showNotification = true;
    } attributes;

    struct {
        bool enabled = false;
        std::string itemName = "none";
        std::string id = "xxxxxx";
        std::string plugin = "none";
        int amount = 1;
        int intervalMinutes = 1;
        bool showNotification = true;
    } item1;

    struct {
        bool enabled = false;
        std::string itemName = "none";
        std::string id = "xxxxxx";
        std::string plugin = "none";
        int amount = 1;
        int intervalMinutes = 1;
        bool showNotification = true;
    } item2;

    struct {
        bool enabled = true;
        std::string id = "003534";
        std::string plugin = "Dawnguard.esm";
        int amount = 3;
        int intervalMinutes = 3;
        bool showNotification = true;
    } milk;

    struct {
        bool enabled = true;
        std::string id = "000D73";
        std::string plugin = "YurianaWench.esp";
        int amount = 3;
        int intervalMinutes = 3;
        bool showNotification = true;
    } milkWench;

    struct {
        bool enabled = true;
        std::string id = "65FEC3";
        std::string pluginItem = "YurianaWench.esp";
        std::string npc = "576A03";
        std::string pluginNPC = "YurianaWench.esp";
        int amount = 1;
        int intervalMinutes = 3;
        bool showNotification = true;
    } milkEthel;

    struct {
        bool enabled = true;
    } notification;
};

struct PluginConfigClimax {
    struct {
        bool enabled = true;
        int amount = 300;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } gold;

    struct {
        bool enabled = true;
        int reductionAmountHunger = 100;
        int reductionAmountCold = 100;
        int reductionAmountExhaustion = 100;
        int activationThreshold = 100;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } survival;

    struct {
        bool enabled = true;
        int restorationAmount = 50;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } attributes;

    struct {
        bool enabled = false;
        std::string itemName = "none";
        std::string id = "xxxxxx";
        std::string plugin = "none";
        int amount = 1;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } item1;

    struct {
        bool enabled = false;
        std::string itemName = "none";
        std::string id = "xxxxxx";
        std::string plugin = "none";
        int amount = 1;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } item2;

    struct {
        bool enabled = false;
        std::string id = "003534";
        std::string plugin = "HearthFires.esm";
        int amount = 1;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } milk;

    struct {
        bool enabled = false;
        std::string id = "000D73";
        std::string plugin = "YurianaWench.esp";
        int amount = 1;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } milkWench;

    struct {
        bool enabled = false;
        std::string id = "65FEC3";
        std::string pluginItem = "YurianaWench.esp";
        std::string npc = "576A03";
        std::string pluginNPC = "YurianaWench.esp";
        int amount = 1;
        std::string event = "ostim_actor_orgasm";
        bool male = true;
        bool female = true;
        bool showNotification = true;
    } milkEthel;
};

struct CapturedNPCData {
    RE::FormID formID = 0;
    std::string pluginName;
    bool captured = false;
    std::chrono::steady_clock::time_point lastSeen;
};

struct CachedFormIDs {
    RE::FormID item1 = 0;
    RE::FormID item2 = 0;
    RE::FormID milkDawnguard = 0;
    RE::FormID milkWench = 0;
    RE::FormID milkEthel = 0;
    bool resolved = false;
};

struct ActorInfo {
    std::string name;
    RE::FormID refID = 0;
    RE::FormID baseID = 0;
    std::string race;
    std::string gender;
    bool isVampire = false;
    bool isWerewolf = false;
    bool captured = false;
};

struct OStimEventData {
    std::string eventType;
    std::string sceneID;
    std::string actorName;
    int threadID = 0;
    int speed = -1;
    std::chrono::steady_clock::time_point timestamp;
};

static std::deque<std::string> g_actionLines;
static std::deque<std::string> g_animationLines;
static std::deque<std::string> g_ostimEventLines;
static std::string g_documentsPath;
static std::string g_gamePath;
static bool g_isInitialized = false;
static std::mutex g_logMutex;
static std::mutex g_sceneMutex;
static std::mutex g_configMutex;
static std::mutex g_cacheMutex;
static std::streampos g_lastOStimLogPosition = 0;
static bool g_monitoringActive = false;
static std::thread g_monitorThread;
static int g_monitorCycles = 0;
static std::unordered_set<std::string> g_processedLines;
static size_t g_lastFileSize = 0;
static std::string g_lastAnimation = "";
static std::chrono::steady_clock::time_point g_monitoringStartTime;
static bool g_initialDelayComplete = false;
static std::atomic<bool> g_isShuttingDown(false);
static SKSELogsPaths g_ostimLogPaths;
static PluginConfig g_config;
static PluginConfigClimax g_configClimax;

static bool g_inOStimScene = false;
static std::chrono::steady_clock::time_point g_lastGoldRewardTime;
static std::atomic<bool> g_goldRewardActive(false);

static float g_lastHungerValue = 0.0f;
static float g_lastColdValue = 0.0f;
static float g_lastExhaustionValue = 0.0f;
static std::chrono::steady_clock::time_point g_lastSurvivalReductionTime;
static std::atomic<bool> g_survivalRestorationActive(false);
static bool g_allStatsAtZero = false;

static std::chrono::steady_clock::time_point g_lastAttributesRestorationTime;
static std::atomic<bool> g_attributesRestorationActive(false);

static std::chrono::steady_clock::time_point g_lastItem1RewardTime;
static std::atomic<bool> g_item1RewardActive(false);

static std::chrono::steady_clock::time_point g_lastItem2RewardTime;
static std::atomic<bool> g_item2RewardActive(false);

static std::chrono::steady_clock::time_point g_lastMilkRewardTime;
static std::atomic<bool> g_milkRewardActive(false);

static std::chrono::steady_clock::time_point g_lastMilkWenchRewardTime;
static std::atomic<bool> g_milkWenchRewardActive(false);
static std::chrono::steady_clock::time_point g_lastMilkEthelRewardTime;
static std::atomic<bool> g_milkEthelRewardActive(false);

static bool g_wenchMilkNPCDetected = false;
static bool g_ethelNPCDetected = false;
static std::chrono::steady_clock::time_point g_lastNPCDetectionCheck;

static CapturedNPCData g_capturedYurianaWenchNPC;
static CapturedNPCData g_capturedEthelNPC;

static CachedFormIDs g_cachedItemFormIDs;

static HANDLE g_directoryHandle = INVALID_HANDLE_VALUE;
static std::thread g_fileWatchThread;
static std::atomic<bool> g_fileWatchActive(false);

static std::vector<std::string> g_detectedNPCNames;
static std::map<std::string, RE::FormID> g_npcNameToRefID;
static std::map<std::string, ActorInfo> g_nearbyNPCsCache;
static std::vector<ActorInfo> g_sceneActors;

static std::map<int, OStimEventData> g_currentOStimEvents;
static int g_currentOStimSpeed = 0;
static std::chrono::steady_clock::time_point g_lastOStimEventCheck;

void StartMonitoringThread();
void StopMonitoringThread();
void WriteToActionsLog(const std::string& message, int lineNumber = 0);
void WriteToAnimationsLog(const std::string& message, int lineNumber = 0);
void WriteToOStimEventsLog(const std::string& message, int lineNumber = 0);
void CheckAndRewardGold();
void CheckAndRestoreSurvivalStats();
void CheckAndRestoreAttributes();
void CheckAndRewardItem1();
void CheckAndRewardItem2();
void CheckAndRewardMilk();
void CheckAndRewardMilkWench();
void CheckAndRewardMilkEthel();
void CheckForNearbyNPCs();
void TryCaptureNPCFormIDs();
void ResolveItemFormIDs();
void ValidateAndUpdatePluginsInINI();
bool LoadConfiguration();
void SaveDefaultConfiguration();
std::string GetLastAnimation();
void SetLastAnimation(const std::string& animation);
bool IsInOStimScene();
void SetInOStimScene(bool inScene);
fs::path GetPluginINIPath();
RE::FormID GetFormIDFromPlugin(const std::string& pluginName, const std::string& localFormID);
bool IsAnyNPCFromPluginNearPlayer(const std::string& pluginName, float maxDistance);
bool IsSpecificNPCNearPlayer(RE::FormID npcFormID, float maxDistance);
void DetectNPCNamesFromLine(const std::string& line);
void FindAndCacheNPCRefIDs();
void BuildNPCsCacheForScene();
void ClearNPCsCache();
ActorInfo CapturePlayerInfo();
ActorInfo CaptureNPCInfo(const std::string& npcName);
void LogActorInfo(const ActorInfo& info, bool isPlayer);
bool IsActorFromPlugin(RE::FormID actorFormID, const std::string& pluginName);
bool IsDLCInstalled(const std::string& dlcName);
bool IsActorVampire(RE::Actor* actor);
bool IsActorWerewolf(RE::Actor* actor);
std::string NormalizeName(const std::string& name);
void ParseOStimEventFromLine(const std::string& line);
void ProcessOStimEventData();
void ProcessOrgasmEvent(const std::string& actorName, const std::string& gender, bool isPlayer);
void ProcessClimaxGoldReward(const std::string& actorName, bool isPlayer);
void ProcessClimaxSurvivalRestore(const std::string& actorName, bool isPlayer);
void ProcessClimaxAttributesRestore(const std::string& actorName, bool isPlayer);
void ProcessClimaxItem1Reward(const std::string& actorName, bool isPlayer);
void ProcessClimaxItem2Reward(const std::string& actorName, bool isPlayer);
void ProcessClimaxMilkReward(const std::string& actorName, bool isPlayer);
void ProcessClimaxMilkWenchReward(const std::string& actorName, bool isPlayer);
void ProcessClimaxMilkEthelReward(const std::string& actorName, bool isPlayer);
bool LoadClimaxConfiguration();

std::string SafeWideStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    try {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed <= 0) {
            size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
            if (size_needed <= 0) return std::string();
            std::string result(size_needed, 0);
            int converted = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
            if (converted <= 0) return std::string();
            return result;
        }
        std::string result(size_needed, 0);
        int converted = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
        if (converted <= 0) return std::string();
        return result;
    } catch (...) {
        std::string result;
        result.reserve(wstr.size());
        for (wchar_t wc : wstr) {
            if (wc <= 127) {
                result.push_back(static_cast<char>(wc));
            } else {
                result.push_back('?');
            }
        }
        return result;
    }
}

std::string GetEnvVar(const std::string& key) {
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, key.c_str()) == 0 && buf != nullptr) {
        std::string value(buf);
        free(buf);
        return value;
    }
    return "";
}

std::string NormalizeName(const std::string& name) {
    std::string normalized = name;
    normalized.erase(0, normalized.find_first_not_of(" \t\r\n"));
    normalized.erase(normalized.find_last_not_of(" \t\r\n") + 1);
    return normalized;
}

std::string GetCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string GetCurrentTimeStringWithMillis() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string GetLastAnimation() {
    std::lock_guard<std::mutex> lock(g_sceneMutex);
    return g_lastAnimation;
}

void SetLastAnimation(const std::string& animation) {
    std::lock_guard<std::mutex> lock(g_sceneMutex);
    g_lastAnimation = animation;
}

bool IsInOStimScene() {
    std::lock_guard<std::mutex> lock(g_sceneMutex);
    return g_inOStimScene;
}

void SetInOStimScene(bool inScene) {
    std::lock_guard<std::mutex> lock(g_sceneMutex);
    g_inOStimScene = inScene;
    
    if (!inScene) {
        ClearNPCsCache();
    }
}

void WriteToAnimationsLog(const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return;

    auto logPath = *logsFolder / "OSurvival-Mode-NG-Animations.log";

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);

    std::stringstream ss;
    ss << "[" << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << "[log] [info] ";
    ss << "[plugin.cpp:" << lineNumber << "] ";
    ss << message;

    std::string newLine = ss.str();
    g_animationLines.push_back(newLine);

    if (g_animationLines.size() > 4000) {
        for (int i = 0; i < 500; i++) {
            if (!g_animationLines.empty()) {
                g_animationLines.pop_front();
            }
        }
        
        std::ofstream animationsFile(logPath, std::ios::trunc);
        if (animationsFile.is_open()) {
            for (const auto& line : g_animationLines) {
                animationsFile << line << std::endl;
            }
            animationsFile.close();
        }
    } else {
        std::ofstream animationsFile(logPath, std::ios::app);
        if (animationsFile.is_open()) {
            animationsFile << newLine << std::endl;
            animationsFile.close();
        }
    }
}

void WriteToActionsLog(const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return;

    auto logPath = *logsFolder / "OSurvival-Mode-NG-Actions.log";

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);

    std::stringstream ss;
    ss << "[" << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << "[log] [info] ";
    ss << "[plugin.cpp:" << lineNumber << "] ";
    ss << message;

    std::string newLine = ss.str();
    g_actionLines.push_back(newLine);

    if (g_actionLines.size() > 4000) {
        for (int i = 0; i < 500; i++) {
            if (!g_actionLines.empty()) {
                g_actionLines.pop_front();
            }
        }
        
        std::ofstream actionsFile(logPath, std::ios::trunc);
        if (actionsFile.is_open()) {
            for (const auto& line : g_actionLines) {
                actionsFile << line << std::endl;
            }
            actionsFile.close();
        }
    } else {
        std::ofstream actionsFile(logPath, std::ios::app);
        if (actionsFile.is_open()) {
            actionsFile << newLine << std::endl;
            actionsFile.close();
        }
    }
}

void WriteToOStimEventsLog(const std::string& message, int lineNumber) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) return;

    auto logPath = *logsFolder / "OSurvival-Mode-NG-OStimEvents.log";

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &time_t);

    std::stringstream ss;
    ss << "[" << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << "[ostim_events] [info] ";
    ss << "[plugin.cpp:" << lineNumber << "] ";
    ss << message;

    std::string newLine = ss.str();
    g_ostimEventLines.push_back(newLine);

    if (g_ostimEventLines.size() > 4000) {
        for (int i = 0; i < 500; i++) {
            if (!g_ostimEventLines.empty()) {
                g_ostimEventLines.pop_front();
            }
        }
        
        std::ofstream eventsFile(logPath, std::ios::trunc);
        if (eventsFile.is_open()) {
            for (const auto& line : g_ostimEventLines) {
                eventsFile << line << std::endl;
            }
            eventsFile.close();
        }
    } else {
        std::ofstream eventsFile(logPath, std::ios::app);
        if (eventsFile.is_open()) {
            eventsFile << newLine << std::endl;
            eventsFile.close();
        }
    }
}

fs::path GetPluginINIPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path gamePath = fs::path(exePath).parent_path();
    fs::path pluginConfigDir = gamePath / "Data" / "SKSE" / "Plugins";
    
    if (!fs::exists(pluginConfigDir)) {
        fs::create_directories(pluginConfigDir);
    }
    
    return pluginConfigDir / "OSurvival-Mode-NG.ini";
}

bool IsDLCInstalled(const std::string& dlcName) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return false;
    
    auto* file = dataHandler->LookupModByName(dlcName);
    return (file != nullptr);
}

bool IsActorVampire(RE::Actor* actor) {
    if (!actor) return false;
    
    auto* actorBase = actor->GetActorBase();
    if (!actorBase) return false;
    
    auto* actorClass = actorBase->npcClass;
    if (actorClass) {
        RE::FormID classID = actorClass->GetFormID();
        if (classID == 0x0002E00F) {
            return true;
        }
    }
    
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler && IsDLCInstalled("Dawnguard.esm")) {
        auto* vampireFaction = dataHandler->LookupForm<RE::TESFaction>(0x020142E6, "Dawnguard.esm");
        if (vampireFaction && actor->IsInFaction(vampireFaction)) {
            return true;
        }
    }
    
    return false;
}

bool IsActorWerewolf(RE::Actor* actor) {
    if (!actor) return false;
    
    auto* actorBase = actor->GetActorBase();
    if (!actorBase) return false;
    
    auto* actorClass = actorBase->npcClass;
    if (actorClass) {
        RE::FormID classID = actorClass->GetFormID();
        if (classID == 0x000A1993 || classID == 0x000A1994 || classID == 0x000A1995) {
            return true;
        }
    }
    
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        auto* werewolfFaction = dataHandler->LookupForm<RE::TESFaction>(0x0009A741, "Skyrim.esm");
        if (werewolfFaction && actor->IsInFaction(werewolfFaction)) {
            return true;
        }
    }
    
    return false;
}

RE::FormID GetFormIDFromPlugin(const std::string& pluginName, const std::string& localFormID) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        logger::error("Failed to get TESDataHandler");
        return 0;
    }

    auto* file = dataHandler->LookupModByName(pluginName);
    if (!file) {
        logger::error("Plugin not found: {}", pluginName);
        return 0;
    }

    std::string cleanID = localFormID;
    if (cleanID.length() >= 2 && cleanID.substr(0, 2) == "XX") {
        cleanID = cleanID.substr(2);
    }

    RE::FormID localID = 0;
    try {
        localID = std::stoul(cleanID, nullptr, 16);
    } catch (...) {
        logger::error("Failed to parse FormID: {}", cleanID);
        return 0;
    }

    uint8_t modIndex = file->compileIndex;
    if (modIndex == 0xFF) {
        modIndex = file->smallFileCompileIndex;
    }

    RE::FormID fullFormID = (static_cast<RE::FormID>(modIndex) << 24) | (localID & 0x00FFFFFF);
    
    return fullFormID;
}

bool IsAnyNPCFromPluginNearPlayer(const std::string& pluginName, float maxDistance) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }
    
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return false;
    }
    
    auto* file = dataHandler->LookupModByName(pluginName);
    if (!file) {
        return false;
    }
    
    uint8_t modIndex = file->compileIndex;
    if (modIndex == 0xFF) {
        modIndex = file->smallFileCompileIndex;
    }
    
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return false;
    }
    
    RE::NiPoint3 playerPos = player->GetPosition();
    
    for (auto& actorHandle : processLists->highActorHandles) {
        auto actor = actorHandle.get();
        if (!actor) continue;
        
        auto* actorBase = actor->GetActorBase();
        if (!actorBase) continue;
        
        uint8_t npcModIndex = (actorBase->formID >> 24) & 0xFF;
        if (npcModIndex == modIndex) {
            RE::NiPoint3 npcPos = actor->GetPosition();
            float distance = playerPos.GetDistance(npcPos);
            
            if (distance <= maxDistance) {
                return true;
            }
        }
    }
    
    return false;
}

bool IsSpecificNPCNearPlayer(RE::FormID npcFormID, float maxDistance) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }
    
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return false;
    }
    
    RE::NiPoint3 playerPos = player->GetPosition();
    
    for (auto& actorHandle : processLists->highActorHandles) {
        auto actor = actorHandle.get();
        if (!actor) continue;
        
        auto* actorBase = actor->GetActorBase();
        if (!actorBase) continue;
        
        if (actorBase->formID == npcFormID) {
            RE::NiPoint3 npcPos = actor->GetPosition();
            float distance = playerPos.GetDistance(npcPos);
            
            if (distance <= maxDistance) {
                return true;
            }
        }
    }
    
    return false;
}

bool IsActorFromPlugin(RE::FormID actorFormID, const std::string& pluginName) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return false;
    
    auto* file = dataHandler->LookupModByName(pluginName);
    if (!file) return false;
    
    uint8_t modIndex = file->compileIndex;
    if (modIndex == 0xFF) {
        modIndex = file->smallFileCompileIndex;
    }
    
    uint8_t actorModIndex = (actorFormID >> 24) & 0xFF;
    
    return (actorModIndex == modIndex);
}

std::string GetDocumentsPath() {
    try {
        wchar_t path[MAX_PATH] = {0};
        HRESULT result = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, path);
        if (SUCCEEDED(result)) {
            std::wstring ws(path);
            std::string converted = SafeWideStringToString(ws);
            if (!converted.empty()) {
                return converted;
            }
        }
        std::string userProfile = GetEnvVar("USERPROFILE");
        if (!userProfile.empty()) {
            return userProfile + "\\Documents";
        }
        return "C:\\Users\\Default\\Documents";
    } catch (...) {
        return "C:\\Users\\Default\\Documents";
    }
}

bool IsValidPluginPath(const fs::path& pluginPath) {
    const std::vector<std::string> dllNames = {
        "OSurvival-Mode-NG.dll"
    };
    
    for (const auto& dllName : dllNames) {
        fs::path dllPath = pluginPath / dllName;
        try {
            if (fs::exists(dllPath)) {
                return true;
            }
        } catch (...) {
            continue;
        }
    }
    return false;
}

fs::path BuildPathCaseInsensitive(const fs::path& basePath, const std::vector<std::string>& components) {
    try {
        fs::path currentPath = basePath;
        
        for (const auto& component : components) {
            fs::path testPath = currentPath / component;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            std::string lowerComponent = component;
            std::transform(lowerComponent.begin(), lowerComponent.end(), 
                         lowerComponent.begin(), ::tolower);
            testPath = currentPath / lowerComponent;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            std::string upperComponent = component;
            std::transform(upperComponent.begin(), upperComponent.end(), 
                         upperComponent.begin(), ::toupper);
            testPath = currentPath / upperComponent;
            if (fs::exists(testPath)) {
                currentPath = testPath;
                continue;
            }
            
            bool found = false;
            if (fs::exists(currentPath) && fs::is_directory(currentPath)) {
                for (const auto& entry : fs::directory_iterator(currentPath)) {
                    try {
                        std::string entryName = entry.path().filename().string();
                        std::string lowerEntryName = entryName;
                        std::transform(lowerEntryName.begin(), lowerEntryName.end(), 
                                     lowerEntryName.begin(), ::tolower);
                        
                        if (lowerEntryName == lowerComponent) {
                            currentPath = entry.path();
                            found = true;
                            break;
                        }
                    } catch (...) {
                        continue;
                    }
                }
            }
            
            if (!found) {
                currentPath = currentPath / component;
            }
        }
        
        return currentPath;
        
    } catch (...) {
        return basePath;
    }
}

fs::path GetDllDirectory() {
    try {
        HMODULE hModule = nullptr;
        static int dummyVariable = 0;

        if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&dummyVariable), 
            &hModule) && hModule != nullptr) {
            
            wchar_t dllPath[MAX_PATH] = {0};
            DWORD size = GetModuleFileNameW(hModule, dllPath, MAX_PATH);

            if (size > 0) {
                std::wstring wsDllPath(dllPath);
                std::string dllPathStr = SafeWideStringToString(wsDllPath);

                if (!dllPathStr.empty()) {
                    fs::path dllDir = fs::path(dllPathStr).parent_path();
                    return dllDir;
                }
            }
        }

        return fs::path();

    } catch (...) {
        return fs::path();
    }
}

std::string GetGamePath() {
    try {
        std::string mo2Path = GetEnvVar("MO2_MODS_PATH");
        if (!mo2Path.empty()) {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(mo2Path), {"Data", "SKSE", "Plugins"}
            );
            if (IsValidPluginPath(testPath)) {
                WriteToAnimationsLog("Game path detected: MO2 Environment Variable", __LINE__);
                return mo2Path;
            }
        }

        std::string mo2Overwrite = GetEnvVar("MO_OVERWRITE_PATH");
        if (!mo2Overwrite.empty()) {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(mo2Overwrite), {"SKSE", "Plugins"}
            );
            if (IsValidPluginPath(testPath)) {
                WriteToAnimationsLog("Game path detected: MO2 Overwrite Path", __LINE__);
                return mo2Overwrite;
            }
        }

        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty()) {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(vortexPath), {"Data", "SKSE", "Plugins"}
            );
            if (IsValidPluginPath(testPath)) {
                WriteToAnimationsLog("Game path detected: Vortex Environment Variable", __LINE__);
                return vortexPath;
            }
        }

        std::string skyrimMods = GetEnvVar("SKYRIM_MODS_FOLDER");
        if (!skyrimMods.empty()) {
            fs::path testPath = BuildPathCaseInsensitive(
                fs::path(skyrimMods), {"Data", "SKSE", "Plugins"}
            );
            if (IsValidPluginPath(testPath)) {
                WriteToAnimationsLog("Game path detected: SKYRIM_MODS_FOLDER Variable", __LINE__);
                return skyrimMods;
            }
        }

        std::vector<std::string> registryKeys = {
            "SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition",
            "SOFTWARE\\Bethesda Softworks\\Skyrim Special Edition",
            "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1457087920",
            "SOFTWARE\\GOG.com\\Games\\1457087920",
            "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\489830",
            "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\611670"
        };

        HKEY hKey;
        char pathBuffer[MAX_PATH] = {0};
        DWORD pathSize = sizeof(pathBuffer);

        for (const auto& key : registryKeys) {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                if (RegQueryValueExA(hKey, "Installed Path", NULL, NULL, (LPBYTE)pathBuffer, &pathSize) == ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    std::string result(pathBuffer);
                    if (!result.empty()) {
                        fs::path testPath = BuildPathCaseInsensitive(
                            fs::path(result), {"Data", "SKSE", "Plugins"}
                        );
                        if (IsValidPluginPath(testPath)) {
                            WriteToAnimationsLog("Game path detected: Windows Registry", __LINE__);
                            return result;
                        }
                    }
                }
                RegCloseKey(hKey);
            }
            pathSize = sizeof(pathBuffer);
        }

        std::vector<std::string> commonPaths = {
            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\Program Files\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "F:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "F:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition",
            "C:\\GOG Games\\Skyrim Special Edition",
            "D:\\GOG Games\\Skyrim Special Edition",
            "E:\\GOG Games\\Skyrim Special Edition",
            "C:\\Games\\Skyrim Special Edition",
            "D:\\Games\\Skyrim Special Edition"
        };

        for (const auto& pathCandidate : commonPaths) {
            try {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate)) {
                    fs::path testPath = BuildPathCaseInsensitive(
                        fs::path(pathCandidate), {"Data", "SKSE", "Plugins"}
                    );
                    if (IsValidPluginPath(testPath)) {
                        WriteToAnimationsLog("Game path detected: Common Installation Path", __LINE__);
                        return pathCandidate;
                    }
                }
            } catch (...) {
                continue;
            }
        }

        WriteToAnimationsLog("Attempting DLL Directory Detection (Wabbajack/MO2/Portable fallback)...", __LINE__);
        fs::path dllDir = GetDllDirectory();
        
        if (!dllDir.empty()) {
            if (IsValidPluginPath(dllDir)) {
                fs::path calculatedGamePath = dllDir.parent_path().parent_path().parent_path();
                WriteToAnimationsLog("Game path detected: DLL Directory Method (Wabbajack/Portable)", __LINE__);
                WriteToAnimationsLog("Calculated game path: " + calculatedGamePath.string(), __LINE__);
                return calculatedGamePath.string();
            }
        }

        WriteToAnimationsLog("WARNING: No valid game path detected, using default fallback", __LINE__);
        return "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition";
        
    } catch (...) {
        return "";
    }
}

SKSELogsPaths GetAllSKSELogsPaths() {
    SKSELogsPaths paths;

    try {
        std::string docs = GetDocumentsPath();

        fs::path primaryBase = fs::path(docs) / "My Games" / "Skyrim Special Edition" / "SKSE";
        paths.primary = primaryBase;

        fs::path secondaryBase = fs::path(docs) / "My Games" / "Skyrim.INI" / "SKSE";
        paths.secondary = secondaryBase;

        if (fs::exists(paths.primary)) {
            logger::info("Primary path exists and is accessible");
        }

        if (fs::exists(paths.secondary)) {
            logger::info("Secondary path exists and is accessible");
        }

    } catch (const std::exception& e) {
        logger::error("Error detecting SKSE paths: {}", e.what());
    }

    return paths;
}

void BuildNPCsCacheForScene() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    
    g_nearbyNPCsCache.clear();
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return;
    
    RE::NiPoint3 playerPos = player->GetPosition();
    float maxDistance = 3000.0f;
    
    auto processActorList = [&](auto& actorHandles) {
        for (auto& actorHandle : actorHandles) {
            auto actor = actorHandle.get();
            if (!actor) continue;
            
            auto* actorBase = actor->GetActorBase();
            if (!actorBase) continue;
            
            RE::NiPoint3 npcPos = actor->GetPosition();
            float distance = playerPos.GetDistance(npcPos);
            
            if (distance <= maxDistance) {
                ActorInfo info;
                info.name = actorBase->GetName();
                info.refID = actor->GetFormID();
                info.baseID = actorBase->GetFormID();
                
                auto* race = actorBase->GetRace();
                if (race) {
                    info.race = race->GetName();
                } else {
                    info.race = "Unknown";
                }
                
                info.gender = actorBase->IsFemale() ? "Female" : "Male";
                
                info.isVampire = IsActorVampire(actor.get());
                info.isWerewolf = IsActorWerewolf(actor.get());
                
                info.captured = true;
                
                std::string normalizedName = NormalizeName(info.name);
                
                g_nearbyNPCsCache[normalizedName] = info;
            }
        }
    };
    
    processActorList(processLists->highActorHandles);
    processActorList(processLists->middleHighActorHandles);
    processActorList(processLists->lowActorHandles);
    
    WriteToAnimationsLog("NPC cache built: " + std::to_string(g_nearbyNPCsCache.size()) + " NPCs within 3000 units", __LINE__);
}

void ClearNPCsCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_nearbyNPCsCache.clear();
    WriteToAnimationsLog("NPC cache cleared", __LINE__);
}

ActorInfo CapturePlayerInfo() {
    ActorInfo info;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return info;
    }
    
    auto* playerBase = player->GetActorBase();
    if (!playerBase) {
        return info;
    }
    
    info.name = playerBase->GetName();
    info.refID = player->GetFormID();
    info.baseID = playerBase->GetFormID();
    
    auto* race = playerBase->GetRace();
    if (race) {
        info.race = race->GetName();
    } else {
        info.race = "Unknown";
    }
    
    info.gender = playerBase->IsFemale() ? "Female" : "Male";
    
    info.isVampire = IsActorVampire(player);
    info.isWerewolf = IsActorWerewolf(player);
    
    info.captured = true;
    
    return info;
}

ActorInfo CaptureNPCInfo(const std::string& npcName) {
    ActorInfo info;
    info.name = npcName;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return info;
    }
    
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return info;
    }
    
    RE::NiPoint3 playerPos = player->GetPosition();
    float maxDistance = 3000.0f;
    
    std::string normalizedSearchName = NormalizeName(npcName);
    
    auto searchInList = [&](auto& actorHandles) -> bool {
        for (auto& actorHandle : actorHandles) {
            auto actor = actorHandle.get();
            if (!actor) continue;
            
            auto* actorBase = actor->GetActorBase();
            if (!actorBase) continue;
            
            std::string actorName = actorBase->GetName();
            std::string normalizedActorName = NormalizeName(actorName);
            
            if (normalizedActorName == normalizedSearchName) {
                RE::NiPoint3 npcPos = actor->GetPosition();
                float distance = playerPos.GetDistance(npcPos);
                
                if (distance <= maxDistance) {
                    info.refID = actor->GetFormID();
                    info.baseID = actorBase->GetFormID();
                    
                    auto* race = actorBase->GetRace();
                    if (race) {
                        info.race = race->GetName();
                    } else {
                        info.race = "Unknown";
                    }
                    
                    info.gender = actorBase->IsFemale() ? "Female" : "Male";
                    
                    info.isVampire = IsActorVampire(actor.get());
                    info.isWerewolf = IsActorWerewolf(actor.get());
                    
                    info.captured = true;
                    
                    return true;
                }
            }
        }
        return false;
    };
    
    if (searchInList(processLists->highActorHandles)) return info;
    if (searchInList(processLists->middleHighActorHandles)) return info;
    if (searchInList(processLists->lowActorHandles)) return info;
    
    return info;
}

void LogActorInfo(const ActorInfo& info, bool isPlayer) {
    if (!info.captured) {
        WriteToAnimationsLog("Failed to capture info for: " + info.name, __LINE__);
        return;
    }
    
    std::string actorType = isPlayer ? "PLAYER" : "NPC";
    
    WriteToAnimationsLog("========================================", __LINE__);
    WriteToAnimationsLog(actorType + " DETECTED IN OSTIM SCENE", __LINE__);
    WriteToAnimationsLog("Name: " + info.name, __LINE__);
    
    std::stringstream refIDStr;
    refIDStr << "Reference ID: 0x" << std::hex << std::uppercase << info.refID;
    WriteToAnimationsLog(refIDStr.str(), __LINE__);
    
    std::stringstream baseIDStr;
    baseIDStr << "Base ID: 0x" << std::hex << std::uppercase << info.baseID;
    WriteToAnimationsLog(baseIDStr.str(), __LINE__);
    
    WriteToAnimationsLog("Race: " + info.race, __LINE__);
    WriteToAnimationsLog("Gender: " + info.gender, __LINE__);
    WriteToAnimationsLog("Is Vampire: " + std::string(info.isVampire ? "Yes" : "No"), __LINE__);
    WriteToAnimationsLog("Is Werewolf: " + std::string(info.isWerewolf ? "Yes" : "No"), __LINE__);
    WriteToAnimationsLog("========================================", __LINE__);
}

void DetectNPCNamesFromLine(const std::string& line) {
    bool hasVoiceSetFound = (line.find("voice set") != std::string::npos && 
                             line.find("found for actor") != std::string::npos);
    bool hasNoVoiceSet = (line.find("no voice set found for actor") != std::string::npos);
    
    if (!hasVoiceSetFound && !hasNoVoiceSet) {
        return;
    }
    
    if (g_nearbyNPCsCache.empty()) {
        WriteToAnimationsLog("Cache empty when detecting NPC - building now", __LINE__);
        BuildNPCsCacheForScene();
    }

    size_t actorPos = line.find("found for actor ");
    if (actorPos == std::string::npos) {
        return;
    }

    size_t nameStart = actorPos + 16;
    
    size_t nameEndBy = line.find(" by", nameStart);
    size_t nameEndComma = line.find(", using", nameStart);
    
    size_t nameEnd = std::string::npos;
    if (nameEndBy != std::string::npos && nameEndComma != std::string::npos) {
        nameEnd = std::min(nameEndBy, nameEndComma);
    } else if (nameEndBy != std::string::npos) {
        nameEnd = nameEndBy;
    } else if (nameEndComma != std::string::npos) {
        nameEnd = nameEndComma;
    }
    
    if (nameEnd == std::string::npos || nameEnd <= nameStart) {
        return;
    }

    std::string npcName = line.substr(nameStart, nameEnd - nameStart);
    
    npcName.erase(0, npcName.find_first_not_of(" \t\r\n"));
    npcName.erase(npcName.find_last_not_of(" \t\r\n") + 1);
    
    if (npcName.empty() || npcName == ",") {
        return;
    }
    
    bool alreadyDetected = false;
    for (const auto& detectedName : g_detectedNPCNames) {
        if (detectedName == npcName) {
            alreadyDetected = true;
            break;
        }
    }
    
    if (!alreadyDetected) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* playerBase = player->GetActorBase();
            if (playerBase) {
                std::string playerName = playerBase->GetName();
                std::string normalizedPlayerName = NormalizeName(playerName);
                std::string normalizedNPCName = NormalizeName(npcName);
                
                if (normalizedPlayerName == normalizedNPCName) {
                    WriteToAnimationsLog("Detected player name in OStim log, skipping: " + npcName, __LINE__);
                    return;
                }
            }
        }
        
        g_detectedNPCNames.push_back(npcName);
        
        std::string normalizedNPCName = NormalizeName(npcName);
        
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_nearbyNPCsCache.find(normalizedNPCName);
        
        if (it != g_nearbyNPCsCache.end()) {
            ActorInfo npcInfo = it->second;
            g_sceneActors.push_back(npcInfo);
            LogActorInfo(npcInfo, false);
        } else {
            WriteToAnimationsLog("========================================", __LINE__);
            WriteToAnimationsLog("NPC NOT FOUND IN CACHE", __LINE__);
            WriteToAnimationsLog("Name from OStim log: " + npcName, __LINE__);
            WriteToAnimationsLog("Normalized name: " + normalizedNPCName, __LINE__);
            WriteToAnimationsLog("Cache size: " + std::to_string(g_nearbyNPCsCache.size()) + " NPCs", __LINE__);
            WriteToAnimationsLog("========================================", __LINE__);
        }
    }
}

void FindAndCacheNPCRefIDs() {
    if (g_detectedNPCNames.empty()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return;
    }

    RE::NiPoint3 playerPos = player->GetPosition();

    for (const auto& npcName : g_detectedNPCNames) {
        if (g_npcNameToRefID.find(npcName) != g_npcNameToRefID.end()) {
            continue;
        }

        bool found = false;
        std::string normalizedSearchName = NormalizeName(npcName);

        auto searchInList = [&](auto& actorHandles) -> bool {
            for (auto& actorHandle : actorHandles) {
                auto actor = actorHandle.get();
                if (!actor) continue;

                auto* actorBase = actor->GetActorBase();
                if (!actorBase) continue;

                std::string actorName = actorBase->GetName();
                std::string normalizedActorName = NormalizeName(actorName);
                
                if (normalizedActorName == normalizedSearchName) {
                    RE::NiPoint3 npcPos = actor->GetPosition();
                    float distance = playerPos.GetDistance(npcPos);

                    if (distance <= 1000.0f) {
                        g_npcNameToRefID[npcName] = actor->GetFormID();
                        
                        WriteToActionsLog("NPC RefID cached: " + npcName + " = 0x" + 
                            std::to_string(actor->GetFormID()), __LINE__);
                        return true;
                    }
                }
            }
            return false;
        };

        if (searchInList(processLists->highActorHandles)) {
            found = true;
        } else if (searchInList(processLists->middleHighActorHandles)) {
            found = true;
        } else if (searchInList(processLists->lowActorHandles)) {
            found = true;
        }

        if (!found) {
            if (g_config.notification.enabled) {
                std::string msg = "OSurvival - " + npcName + " apparently it's like a ghost";
                RE::DebugNotification(msg.c_str());
            }
        }
    }
}

void ParseOStimEventFromLine(const std::string& line) {
    if (line.find("ostim_actor_orgasm") != std::string::npos) {
        size_t actorPos = line.find("actor:");
        if (actorPos != std::string::npos) {
            size_t nameStart = actorPos + 6;
            size_t nameEnd = line.find(",", nameStart);
            if (nameEnd == std::string::npos) nameEnd = line.find(" ", nameStart);
            if (nameEnd == std::string::npos) nameEnd = line.length();
            
            std::string actorName = line.substr(nameStart, nameEnd - nameStart);
            actorName.erase(0, actorName.find_first_not_of(" \t\r\n"));
            actorName.erase(actorName.find_last_not_of(" \t\r\n") + 1);
            
            std::string genderStr = "Unknown";
            size_t genderPos = line.find("gender:");
            if (genderPos != std::string::npos) {
                size_t genderStart = genderPos + 7;
                size_t genderEnd = line.find(",", genderStart);
                if (genderEnd == std::string::npos) genderEnd = line.find(" ", genderStart);
                if (genderEnd == std::string::npos) genderEnd = line.length();
                genderStr = line.substr(genderStart, genderEnd - genderStart);
                genderStr.erase(0, genderStr.find_first_not_of(" \t\r\n"));
                genderStr.erase(genderStr.find_last_not_of(" \t\r\n") + 1);
            }
            
            bool isPlayer = (actorName.find("Player") != std::string::npos || 
                           actorName.find("player") != std::string::npos);
            
            ProcessOrgasmEvent(actorName, genderStr, isPlayer);
        }
    }
    
    if (line.find("[Thread.cpp") != std::string::npos && line.find("changed speed to") != std::string::npos) {
        size_t speedPos = line.find("changed speed to ");
        if (speedPos != std::string::npos) {
            try {
                std::string speedStr = line.substr(speedPos + 17);
                speedStr = speedStr.substr(0, speedStr.find_first_not_of("0123456789"));
                int newSpeed = std::stoi(speedStr);
                
                if (newSpeed != g_currentOStimSpeed) {
                    g_currentOStimSpeed = newSpeed;
                    
                    std::vector<std::string> speedNames = {"Slow", "Medium", "Fast", "Rough"};
                    std::string speedName = (newSpeed >= 0 && newSpeed < static_cast<int>(speedNames.size())) 
                        ? speedNames[newSpeed] : "Unknown";
                    
                    WriteToOStimEventsLog("========================================", __LINE__);
                    WriteToOStimEventsLog("SPEED CHANGE EVENT", __LINE__);
                    WriteToOStimEventsLog("New speed: " + speedName + " (Level " + std::to_string(newSpeed) + ")", __LINE__);
                    WriteToOStimEventsLog("Current animation: " + GetLastAnimation(), __LINE__);
                    WriteToOStimEventsLog("========================================", __LINE__);
                }
            } catch (...) {
            }
        }
    }
    
    if (line.find("[Thread.cpp:195] thread 0 changed to node") != std::string::npos) {
        g_currentOStimSpeed = 0;
        
        WriteToOStimEventsLog("========================================", __LINE__);
        WriteToOStimEventsLog("ANIMATION CHANGE EVENT", __LINE__);
        WriteToOStimEventsLog("Animation changed - speed reset", __LINE__);
        WriteToOStimEventsLog("New animation: " + GetLastAnimation(), __LINE__);
        WriteToOStimEventsLog("========================================", __LINE__);
    }
}

void ProcessOStimEventData() {
    if (!IsInOStimScene()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastOStimEventCheck).count();
    
    if (elapsed < 5) {
        return;
    }
    
    g_lastOStimEventCheck = now;
    
    if (g_currentOStimSpeed > 0) {
        WriteToOStimEventsLog("========================================", __LINE__);
        WriteToOStimEventsLog("PERIODIC STATUS UPDATE", __LINE__);
        WriteToOStimEventsLog("Current animation: " + GetLastAnimation(), __LINE__);
        WriteToOStimEventsLog("Current speed level: " + std::to_string(g_currentOStimSpeed), __LINE__);
        WriteToOStimEventsLog("========================================", __LINE__);
    }
}

void ProcessOrgasmEvent(const std::string& actorName, const std::string& gender, bool isPlayer) {
    LoadClimaxConfiguration();
    
    WriteToOStimEventsLog("========================================", __LINE__);
    WriteToOStimEventsLog("ORGASM EVENT DETECTED", __LINE__);
    WriteToOStimEventsLog("Actor: " + actorName, __LINE__);
    WriteToOStimEventsLog("Gender: " + gender, __LINE__);
    WriteToOStimEventsLog("Is Player: " + std::string(isPlayer ? "Yes" : "No"), __LINE__);
    WriteToOStimEventsLog("========================================", __LINE__);
    
    bool isMale = (gender == "Male" || gender == "male");
    bool isFemale = (gender == "Female" || gender == "female");
    
    if (g_configClimax.gold.enabled && 
        ((isMale && g_configClimax.gold.male) || (isFemale && g_configClimax.gold.female))) {
        ProcessClimaxGoldReward(actorName, isPlayer);
    }
    
    if (g_configClimax.survival.enabled && 
        ((isMale && g_configClimax.survival.male) || (isFemale && g_configClimax.survival.female))) {
        ProcessClimaxSurvivalRestore(actorName, isPlayer);
    }
    
    if (g_configClimax.attributes.enabled && 
        ((isMale && g_configClimax.attributes.male) || (isFemale && g_configClimax.attributes.female))) {
        ProcessClimaxAttributesRestore(actorName, isPlayer);
    }
    
    if (g_configClimax.item1.enabled && g_configClimax.item1.plugin != "none" &&
        ((isMale && g_configClimax.item1.male) || (isFemale && g_configClimax.item1.female))) {
        ProcessClimaxItem1Reward(actorName, isPlayer);
    }
    
    if (g_configClimax.item2.enabled && g_configClimax.item2.plugin != "none" &&
        ((isMale && g_configClimax.item2.male) || (isFemale && g_configClimax.item2.female))) {
        ProcessClimaxItem2Reward(actorName, isPlayer);
    }
    
    if (g_configClimax.milk.enabled && 
        ((isMale && g_configClimax.milk.male) || (isFemale && g_configClimax.milk.female))) {
        ProcessClimaxMilkReward(actorName, isPlayer);
    }
    
    if (g_configClimax.milkWench.enabled && g_wenchMilkNPCDetected &&
        ((isMale && g_configClimax.milkWench.male) || (isFemale && g_configClimax.milkWench.female))) {
        ProcessClimaxMilkWenchReward(actorName, isPlayer);
    }
    
    if (g_configClimax.milkEthel.enabled && g_ethelNPCDetected &&
        ((isMale && g_configClimax.milkEthel.male) || (isFemale && g_configClimax.milkEthel.female))) {
        ProcessClimaxMilkEthelReward(actorName, isPlayer);
    }
}

void ProcessClimaxGoldReward(const std::string& actorName, bool isPlayer) {
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
    
    if (player && gold) {
        player->AddObjectToContainer(gold, nullptr, g_configClimax.gold.amount, nullptr);
        
        if (g_configClimax.gold.showNotification) {
            std::string msg = "OSurvival Climax - Received " + std::to_string(g_configClimax.gold.amount) + " gold";
            RE::DebugNotification(msg.c_str());
        }
        
        WriteToActionsLog("Climax Gold reward: " + std::to_string(g_configClimax.gold.amount) + " gold", __LINE__);
    }
}

void ProcessClimaxSurvivalRestore(const std::string& actorName, bool isPlayer) {
    
    auto hungerGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_HungerNeedValue");
    auto coldGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ColdNeedValue");
    auto exhaustionGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ExhaustionNeedValue");
    
    if (!hungerGlobal || !coldGlobal || !exhaustionGlobal) {
        return;
    }
    
    float currentHunger = hungerGlobal->value;
    float currentCold = coldGlobal->value;
    float currentExhaustion = exhaustionGlobal->value;
    
    float newHunger = std::max(0.0f, currentHunger - static_cast<float>(g_configClimax.survival.reductionAmountHunger));
    float newCold = std::max(0.0f, currentCold - static_cast<float>(g_configClimax.survival.reductionAmountCold));
    float newExhaustion = std::max(0.0f, currentExhaustion - static_cast<float>(g_configClimax.survival.reductionAmountExhaustion));
    
    hungerGlobal->value = newHunger;
    coldGlobal->value = newCold;
    exhaustionGlobal->value = newExhaustion;
    
    if (g_configClimax.survival.showNotification) {
        RE::DebugNotification("OSurvival Climax - Survival needs reduced");
    }
    
    WriteToActionsLog("Climax Survival restore applied", __LINE__);
}

void ProcessClimaxAttributesRestore(const std::string& actorName, bool isPlayer) {
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    auto* actorValueOwner = player->AsActorValueOwner();
    if (actorValueOwner) {
        float amount = static_cast<float>(g_configClimax.attributes.restorationAmount);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, amount);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, amount);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, amount);
        
        if (g_configClimax.attributes.showNotification) {
            std::string msg = "OSurvival Climax - Attributes restored " + std::to_string(g_configClimax.attributes.restorationAmount) + " points";
            RE::DebugNotification(msg.c_str());
        }
        
        WriteToActionsLog("Climax Attributes restore: " + std::to_string(g_configClimax.attributes.restorationAmount) + " points", __LINE__);
    }
}

void ProcessClimaxItem1Reward(const std::string& actorName, bool isPlayer) {
    
    RE::FormID itemFormID = GetFormIDFromPlugin(g_configClimax.item1.plugin, g_configClimax.item1.id);
    if (itemFormID == 0) return;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* itemForm = RE::TESForm::LookupByID(itemFormID);
    if (!player || !itemForm) return;
    
    auto* item = itemForm->As<RE::TESBoundObject>();
    if (!item) return;
    
    player->AddObjectToContainer(item, nullptr, g_configClimax.item1.amount, nullptr);
    
    if (g_configClimax.item1.showNotification) {
        std::string msg = "OSurvival Climax - Received " + std::to_string(g_configClimax.item1.amount) + " " + g_configClimax.item1.itemName;
        RE::DebugNotification(msg.c_str());
    }
    
    WriteToActionsLog("Climax Item1 reward: " + std::to_string(g_configClimax.item1.amount) + " " + g_configClimax.item1.itemName, __LINE__);
}

void ProcessClimaxItem2Reward(const std::string& actorName, bool isPlayer) {
    
    RE::FormID itemFormID = GetFormIDFromPlugin(g_configClimax.item2.plugin, g_configClimax.item2.id);
    if (itemFormID == 0) return;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* itemForm = RE::TESForm::LookupByID(itemFormID);
    if (!player || !itemForm) return;
    
    auto* item = itemForm->As<RE::TESBoundObject>();
    if (!item) return;
    
    player->AddObjectToContainer(item, nullptr, g_configClimax.item2.amount, nullptr);
    
    if (g_configClimax.item2.showNotification) {
        std::string msg = "OSurvival Climax - Received " + std::to_string(g_configClimax.item2.amount) + " " + g_configClimax.item2.itemName;
        RE::DebugNotification(msg.c_str());
    }
    
    WriteToActionsLog("Climax Item2 reward: " + std::to_string(g_configClimax.item2.amount) + " " + g_configClimax.item2.itemName, __LINE__);
}

void ProcessClimaxMilkReward(const std::string& actorName, bool isPlayer) {
    
    RE::FormID milkFormID = GetFormIDFromPlugin(g_configClimax.milk.plugin, g_configClimax.milk.id);
    if (milkFormID == 0) return;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* milkForm = RE::TESForm::LookupByID(milkFormID);
    if (!player || !milkForm) return;
    
    auto* milkItem = milkForm->As<RE::TESBoundObject>();
    if (!milkItem) return;
    
    player->AddObjectToContainer(milkItem, nullptr, g_configClimax.milk.amount, nullptr);
    
    if (g_configClimax.milk.showNotification) {
        std::string msg = "OSurvival Climax - Received " + std::to_string(g_configClimax.milk.amount) + " Milk";
        RE::DebugNotification(msg.c_str());
    }
    
    WriteToActionsLog("Climax Milk reward: " + std::to_string(g_configClimax.milk.amount) + " Milk", __LINE__);
}

void ProcessClimaxMilkWenchReward(const std::string& actorName, bool isPlayer) {
    
    RE::FormID milkFormID = GetFormIDFromPlugin(g_configClimax.milkWench.plugin, g_configClimax.milkWench.id);
    if (milkFormID == 0) return;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* milkForm = RE::TESForm::LookupByID(milkFormID);
    if (!player || !milkForm) return;
    
    auto* milkItem = milkForm->As<RE::TESBoundObject>();
    if (!milkItem) return;
    
    player->AddObjectToContainer(milkItem, nullptr, g_configClimax.milkWench.amount, nullptr);
    
    if (g_configClimax.milkWench.showNotification) {
        std::string msg = "OSurvival Climax - Received " + std::to_string(g_configClimax.milkWench.amount) + " Wench Milk";
        RE::DebugNotification(msg.c_str());
    }
    
    WriteToActionsLog("Climax Wench Milk reward: " + std::to_string(g_configClimax.milkWench.amount) + " Wench Milk", __LINE__);
}

void ProcessClimaxMilkEthelReward(const std::string& actorName, bool isPlayer) {
    
    RE::FormID milkFormID = GetFormIDFromPlugin(g_configClimax.milkEthel.pluginItem, g_configClimax.milkEthel.id);
    if (milkFormID == 0) return;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* milkForm = RE::TESForm::LookupByID(milkFormID);
    if (!player || !milkForm) return;
    
    auto* milkItem = milkForm->As<RE::TESBoundObject>();
    if (!milkItem) return;
    
    player->AddObjectToContainer(milkItem, nullptr, g_configClimax.milkEthel.amount, nullptr);
    
    if (g_configClimax.milkEthel.showNotification) {
        std::string msg = "OSurvival Climax - Received " + std::to_string(g_configClimax.milkEthel.amount) + " Milk Ethel";
        RE::DebugNotification(msg.c_str());
    }
    
    WriteToActionsLog("Climax Milk Ethel reward: " + std::to_string(g_configClimax.milkEthel.amount) + " Milk Ethel", __LINE__);
}

void SaveDefaultConfiguration() {
    fs::path iniPath = GetPluginINIPath();
    
    std::ofstream iniFile(iniPath, std::ios::trunc);
    if (!iniFile.is_open()) {
        logger::error("Failed to create default configuration file");
        return;
    }

    iniFile << "[Gold]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "Amount=300" << std::endl;
    iniFile << "IntervalMinutes=7" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Survival]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "ReductionAmount_HungerNeedValue=100" << std::endl;
    iniFile << "ReductionAmount_ColdNeedValue=100" << std::endl;
    iniFile << "ReductionAmount_ExhaustionNeedValue=100" << std::endl;
    iniFile << "IntervalSeconds=60" << std::endl;
    iniFile << "ActivationThreshold=100" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Attributes]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "RestorationAmount=50" << std::endl;
    iniFile << "IntervalSeconds=120" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Item1]" << std::endl;
    iniFile << "Enabled=false" << std::endl;
    iniFile << "ItemName=none" << std::endl;
    iniFile << "ID=xxxxxx" << std::endl;
    iniFile << "Plugin=none" << std::endl;
    iniFile << "Amount=1" << std::endl;
    iniFile << "IntervalMinutes=1" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Item2]" << std::endl;
    iniFile << "Enabled=false" << std::endl;
    iniFile << "ItemName=none" << std::endl;
    iniFile << "ID=xxxxxx" << std::endl;
    iniFile << "Plugin=none" << std::endl;
    iniFile << "Amount=1" << std::endl;
    iniFile << "IntervalMinutes=1" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Milk]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "ID=003534" << std::endl;
    iniFile << "Plugin=Dawnguard.esm" << std::endl;
    iniFile << "Amount=3" << std::endl;
    iniFile << "IntervalMinutes=3" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[BWY_Wench_Milk]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "ID=000D73" << std::endl;
    iniFile << "Plugin=YurianaWench.esp" << std::endl;
    iniFile << "Amount=3" << std::endl;
    iniFile << "IntervalMinutes=3" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[BWY_Milk_Ethel]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "ID=65FEC3" << std::endl;
    iniFile << "PluginItem=YurianaWench.esp" << std::endl;
    iniFile << "NPC=576A03" << std::endl;
    iniFile << "PluginNPC=YurianaWench.esp" << std::endl;
    iniFile << "Amount=1" << std::endl;
    iniFile << "IntervalMinutes=3" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Frostfall]" << std::endl;
    iniFile << "Enabled=true" << std::endl;
    iniFile << "ReductionAmount_Frost_ColdNeedValue=100" << std::endl;
    iniFile << "IntervalSeconds=60" << std::endl;
    iniFile << "ActivationThreshold=100" << std::endl;
    iniFile << "ShowNotification=true" << std::endl;
    iniFile << std::endl;

    iniFile << "[Notification]" << std::endl;
    iniFile << "Enabled=true" << std::endl;

    iniFile.close();
}

bool LoadConfiguration() {
    std::lock_guard<std::mutex> lock(g_configMutex);

    fs::path iniPath = GetPluginINIPath();

    if (!fs::exists(iniPath)) {
        SaveDefaultConfiguration();
    }

    std::ifstream iniFile(iniPath);
    if (!iniFile.is_open()) {
        logger::error("Failed to open configuration file");
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(iniFile, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }

        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos) {
            std::string key = line.substr(0, equalPos);
            std::string value = line.substr(equalPos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            if (currentSection == "Gold") {
                if (key == "Enabled") {
                    g_config.gold.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "Amount") {
                    g_config.gold.amount = std::stoi(value);
                } else if (key == "IntervalMinutes") {
                    g_config.gold.intervalMinutes = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.gold.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Survival") {
                if (key == "Enabled") {
                    g_config.survival.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ReductionAmount_HungerNeedValue") {
                    g_config.survival.reductionAmountHunger = std::stoi(value);
                } else if (key == "ReductionAmount_ColdNeedValue") {
                    g_config.survival.reductionAmountCold = std::stoi(value);
                } else if (key == "ReductionAmount_ExhaustionNeedValue") {
                    g_config.survival.reductionAmountExhaustion = std::stoi(value);
                } else if (key == "IntervalSeconds") {
                    g_config.survival.intervalSeconds = std::stoi(value);
                } else if (key == "ActivationThreshold") {
                    g_config.survival.activationThreshold = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.survival.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Attributes") {
                if (key == "Enabled") {
                    g_config.attributes.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "RestorationAmount") {
                    g_config.attributes.restorationAmount = std::stoi(value);
                } else if (key == "IntervalSeconds") {
                    g_config.attributes.intervalSeconds = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.attributes.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Item1") {
                if (key == "Enabled") {
                    g_config.item1.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ItemName") {
                    g_config.item1.itemName = value;
                } else if (key == "ID") {
                    g_config.item1.id = value;
                } else if (key == "Plugin") {
                    g_config.item1.plugin = value;
                } else if (key == "Amount") {
                    g_config.item1.amount = std::stoi(value);
                } else if (key == "IntervalMinutes") {
                    g_config.item1.intervalMinutes = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.item1.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Item2") {
                if (key == "Enabled") {
                    g_config.item2.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ItemName") {
                    g_config.item2.itemName = value;
                } else if (key == "ID") {
                    g_config.item2.id = value;
                } else if (key == "Plugin") {
                    g_config.item2.plugin = value;
                } else if (key == "Amount") {
                    g_config.item2.amount = std::stoi(value);
                } else if (key == "IntervalMinutes") {
                    g_config.item2.intervalMinutes = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.item2.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Milk") {
                if (key == "Enabled") {
                    g_config.milk.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ID") {
                    g_config.milk.id = value;
                } else if (key == "Plugin") {
                    g_config.milk.plugin = value;
                } else if (key == "Amount") {
                    g_config.milk.amount = std::stoi(value);
                } else if (key == "IntervalMinutes") {
                    g_config.milk.intervalMinutes = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.milk.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "BWY_Wench_Milk") {
                if (key == "Enabled") {
                    g_config.milkWench.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ID") {
                    g_config.milkWench.id = value;
                } else if (key == "Plugin") {
                    g_config.milkWench.plugin = value;
                } else if (key == "Amount") {
                    g_config.milkWench.amount = std::stoi(value);
                } else if (key == "IntervalMinutes") {
                    g_config.milkWench.intervalMinutes = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.milkWench.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "BWY_Milk_Ethel") {
                if (key == "Enabled") {
                    g_config.milkEthel.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ID") {
                    g_config.milkEthel.id = value;
                } else if (key == "PluginItem") {
                    g_config.milkEthel.pluginItem = value;
                } else if (key == "NPC") {
                    g_config.milkEthel.npc = value;
                } else if (key == "PluginNPC") {
                    g_config.milkEthel.pluginNPC = value;
                } else if (key == "Amount") {
                    g_config.milkEthel.amount = std::stoi(value);
                } else if (key == "IntervalMinutes") {
                    g_config.milkEthel.intervalMinutes = std::stoi(value);
                } else if (key == "ShowNotification") {
                    g_config.milkEthel.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Notification") {
                if (key == "Enabled") {
                    g_config.notification.enabled = (value == "1" || value == "true" || value == "True");
                }
            }
        }
    }

    iniFile.close();
    return true;
}

bool LoadClimaxConfiguration() {
    std::lock_guard<std::mutex> lock(g_configMutex);

    fs::path iniPath = GetPluginINIPath().parent_path() / "OSurvival-Mode-NG-Climax.ini";

    if (!fs::exists(iniPath)) {
        std::ofstream iniFile(iniPath, std::ios::trunc);
        if (!iniFile.is_open()) {
            return false;
        }

        iniFile << "[Gold]" << std::endl;
        iniFile << "Enabled=true" << std::endl;
        iniFile << "Amount=300" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[Survival]" << std::endl;
        iniFile << "Enabled=true" << std::endl;
        iniFile << "ReductionAmount_HungerNeedValue=100" << std::endl;
        iniFile << "ReductionAmount_ColdNeedValue=100" << std::endl;
        iniFile << "ReductionAmount_ExhaustionNeedValue=100" << std::endl;
        iniFile << "ActivationThreshold=100" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[Attributes]" << std::endl;
        iniFile << "Enabled=true" << std::endl;
        iniFile << "RestorationAmount=50" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[Item1]" << std::endl;
        iniFile << "Enabled=false" << std::endl;
        iniFile << "ItemName=none" << std::endl;
        iniFile << "ID=xxxxxx" << std::endl;
        iniFile << "Plugin=none" << std::endl;
        iniFile << "Amount=1" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[Item2]" << std::endl;
        iniFile << "Enabled=false" << std::endl;
        iniFile << "ItemName=none" << std::endl;
        iniFile << "ID=xxxxxx" << std::endl;
        iniFile << "Plugin=none" << std::endl;
        iniFile << "Amount=1" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[Milk]" << std::endl;
        iniFile << "Enabled=false" << std::endl;
        iniFile << "ID=003534" << std::endl;
        iniFile << "Plugin=HearthFires.esm" << std::endl;
        iniFile << "Amount=1" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[BWY_Wench_Milk]" << std::endl;
        iniFile << "Enabled=false" << std::endl;
        iniFile << "ID=000D73" << std::endl;
        iniFile << "Plugin=YurianaWench.esp" << std::endl;
        iniFile << "Amount=1" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[BWY_Milk_Ethel]" << std::endl;
        iniFile << "Enabled=false" << std::endl;
        iniFile << "ID=65FEC3" << std::endl;
        iniFile << "PluginItem=YurianaWench.esp" << std::endl;
        iniFile << "NPC=576A03" << std::endl;
        iniFile << "PluginNPC=YurianaWench.esp" << std::endl;
        iniFile << "Amount=1" << std::endl;
        iniFile << "EVENT=ostim_actor_orgasm" << std::endl;
        iniFile << "Male=true" << std::endl;
        iniFile << "Female=true" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile.close();
    }

    std::ifstream iniFile(iniPath);
    if (!iniFile.is_open()) {
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(iniFile, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }

        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos) {
            std::string key = line.substr(0, equalPos);
            std::string value = line.substr(equalPos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            if (currentSection == "Gold") {
                if (key == "Enabled") {
                    g_configClimax.gold.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "Amount") {
                    g_configClimax.gold.amount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.gold.event = value;
                } else if (key == "Male") {
                    g_configClimax.gold.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.gold.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.gold.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Survival") {
                if (key == "Enabled") {
                    g_configClimax.survival.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ReductionAmount_HungerNeedValue") {
                    g_configClimax.survival.reductionAmountHunger = std::stoi(value);
                } else if (key == "ReductionAmount_ColdNeedValue") {
                    g_configClimax.survival.reductionAmountCold = std::stoi(value);
                } else if (key == "ReductionAmount_ExhaustionNeedValue") {
                    g_configClimax.survival.reductionAmountExhaustion = std::stoi(value);
                } else if (key == "ActivationThreshold") {
                    g_configClimax.survival.activationThreshold = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.survival.event = value;
                } else if (key == "Male") {
                    g_configClimax.survival.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.survival.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.survival.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Attributes") {
                if (key == "Enabled") {
                    g_configClimax.attributes.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "RestorationAmount") {
                    g_configClimax.attributes.restorationAmount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.attributes.event = value;
                } else if (key == "Male") {
                    g_configClimax.attributes.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.attributes.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.attributes.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Item1") {
                if (key == "Enabled") {
                    g_configClimax.item1.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ItemName") {
                    g_configClimax.item1.itemName = value;
                } else if (key == "ID") {
                    g_configClimax.item1.id = value;
                } else if (key == "Plugin") {
                    g_configClimax.item1.plugin = value;
                } else if (key == "Amount") {
                    g_configClimax.item1.amount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.item1.event = value;
                } else if (key == "Male") {
                    g_configClimax.item1.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.item1.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.item1.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Item2") {
                if (key == "Enabled") {
                    g_configClimax.item2.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ItemName") {
                    g_configClimax.item2.itemName = value;
                } else if (key == "ID") {
                    g_configClimax.item2.id = value;
                } else if (key == "Plugin") {
                    g_configClimax.item2.plugin = value;
                } else if (key == "Amount") {
                    g_configClimax.item2.amount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.item2.event = value;
                } else if (key == "Male") {
                    g_configClimax.item2.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.item2.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.item2.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "Milk") {
                if (key == "Enabled") {
                    g_configClimax.milk.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ID") {
                    g_configClimax.milk.id = value;
                } else if (key == "Plugin") {
                    g_configClimax.milk.plugin = value;
                } else if (key == "Amount") {
                    g_configClimax.milk.amount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.milk.event = value;
                } else if (key == "Male") {
                    g_configClimax.milk.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.milk.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.milk.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "BWY_Wench_Milk") {
                if (key == "Enabled") {
                    g_configClimax.milkWench.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ID") {
                    g_configClimax.milkWench.id = value;
                } else if (key == "Plugin") {
                    g_configClimax.milkWench.plugin = value;
                } else if (key == "Amount") {
                    g_configClimax.milkWench.amount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.milkWench.event = value;
                } else if (key == "Male") {
                    g_configClimax.milkWench.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.milkWench.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.milkWench.showNotification = (value == "1" || value == "true" || value == "True");
                }
            } else if (currentSection == "BWY_Milk_Ethel") {
                if (key == "Enabled") {
                    g_configClimax.milkEthel.enabled = (value == "1" || value == "true" || value == "True");
                } else if (key == "ID") {
                    g_configClimax.milkEthel.id = value;
                } else if (key == "PluginItem") {
                    g_configClimax.milkEthel.pluginItem = value;
                } else if (key == "NPC") {
                    g_configClimax.milkEthel.npc = value;
                } else if (key == "PluginNPC") {
                    g_configClimax.milkEthel.pluginNPC = value;
                } else if (key == "Amount") {
                    g_configClimax.milkEthel.amount = std::stoi(value);
                } else if (key == "EVENT") {
                    g_configClimax.milkEthel.event = value;
                } else if (key == "Male") {
                    g_configClimax.milkEthel.male = (value == "1" || value == "true" || value == "True");
                } else if (key == "Female") {
                    g_configClimax.milkEthel.female = (value == "1" || value == "true" || value == "True");
                } else if (key == "ShowNotification") {
                    g_configClimax.milkEthel.showNotification = (value == "1" || value == "true" || value == "True");
                }
            }
        }
    }

    iniFile.close();
    return true;
}

void ValidateAndUpdatePluginsInINI() {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return;
    }

    bool needsUpdate = false;
    PluginConfig originalConfig = g_config;

    if (g_config.item1.enabled) {
        if (g_config.item1.plugin != "none") {
            auto* item1Plugin = dataHandler->LookupModByName(g_config.item1.plugin);
            if (!item1Plugin) {
                g_config.item1.enabled = false;
                needsUpdate = true;
                WriteToActionsLog("Plugin not found: " + g_config.item1.plugin + " - Disabled [Item1] in INI", __LINE__);
            }
        }
    }

    if (g_config.item2.enabled) {
        if (g_config.item2.plugin != "none") {
            auto* item2Plugin = dataHandler->LookupModByName(g_config.item2.plugin);
            if (!item2Plugin) {
                g_config.item2.enabled = false;
                needsUpdate = true;
                WriteToActionsLog("Plugin not found: " + g_config.item2.plugin + " - Disabled [Item2] in INI", __LINE__);
            }
        }
    }

    if (g_config.milk.enabled) {
        auto* milkPlugin = dataHandler->LookupModByName(g_config.milk.plugin);
        if (!milkPlugin) {
            g_config.milk.enabled = false;
            needsUpdate = true;
            WriteToActionsLog("Plugin not found: " + g_config.milk.plugin + " - Disabled [Milk] in INI", __LINE__);
        }
    }

    if (g_config.milkWench.enabled) {
        auto* wenchPlugin = dataHandler->LookupModByName(g_config.milkWench.plugin);
        if (!wenchPlugin) {
            g_config.milkWench.enabled = false;
            needsUpdate = true;
            WriteToActionsLog("Plugin not found: " + g_config.milkWench.plugin + " - Disabled [BWY_Wench_Milk] in INI", __LINE__);
        }
    }

    if (g_config.milkEthel.enabled) {
        auto* ethelPluginItem = dataHandler->LookupModByName(g_config.milkEthel.pluginItem);
        auto* ethelPluginNPC = dataHandler->LookupModByName(g_config.milkEthel.pluginNPC);
        if (!ethelPluginItem || !ethelPluginNPC) {
            g_config.milkEthel.enabled = false;
            needsUpdate = true;
            WriteToActionsLog("Plugin not found for Ethel - Disabled [BWY_Milk_Ethel] in INI", __LINE__);
        }
    }

    if (needsUpdate) {
        fs::path iniPath = GetPluginINIPath();
        std::ofstream iniFile(iniPath, std::ios::trunc);
        if (!iniFile.is_open()) {
            return;
        }

        iniFile << "[Gold]" << std::endl;
        iniFile << "Enabled=" << (g_config.gold.enabled ? "true" : "false") << std::endl;
        iniFile << "Amount=" << g_config.gold.amount << std::endl;
        iniFile << "IntervalMinutes=" << g_config.gold.intervalMinutes << std::endl;
        iniFile << "ShowNotification=" << (g_config.gold.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[Survival]" << std::endl;
        iniFile << "Enabled=" << (g_config.survival.enabled ? "true" : "false") << std::endl;
        iniFile << "ReductionAmount_HungerNeedValue=" << g_config.survival.reductionAmountHunger << std::endl;
        iniFile << "ReductionAmount_ColdNeedValue=" << g_config.survival.reductionAmountCold << std::endl;
        iniFile << "ReductionAmount_ExhaustionNeedValue=" << g_config.survival.reductionAmountExhaustion << std::endl;
        iniFile << "IntervalSeconds=" << g_config.survival.intervalSeconds << std::endl;
        iniFile << "ActivationThreshold=" << g_config.survival.activationThreshold << std::endl;
        iniFile << "ShowNotification=" << (g_config.survival.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[Attributes]" << std::endl;
        iniFile << "Enabled=" << (g_config.attributes.enabled ? "true" : "false") << std::endl;
        iniFile << "RestorationAmount=" << g_config.attributes.restorationAmount << std::endl;
        iniFile << "IntervalSeconds=" << g_config.attributes.intervalSeconds << std::endl;
        iniFile << "ShowNotification=" << (g_config.attributes.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[Item1]" << std::endl;
        iniFile << "Enabled=" << (g_config.item1.enabled ? "true" : "false") << std::endl;
        iniFile << "ItemName=" << g_config.item1.itemName << std::endl;
        iniFile << "ID=" << g_config.item1.id << std::endl;
        iniFile << "Plugin=" << g_config.item1.plugin << std::endl;
        iniFile << "Amount=" << g_config.item1.amount << std::endl;
        iniFile << "IntervalMinutes=" << g_config.item1.intervalMinutes << std::endl;
        iniFile << "ShowNotification=" << (g_config.item1.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[Item2]" << std::endl;
        iniFile << "Enabled=" << (g_config.item2.enabled ? "true" : "false") << std::endl;
        iniFile << "ItemName=" << g_config.item2.itemName << std::endl;
        iniFile << "ID=" << g_config.item2.id << std::endl;
        iniFile << "Plugin=" << g_config.item2.plugin << std::endl;
        iniFile << "Amount=" << g_config.item2.amount << std::endl;
        iniFile << "IntervalMinutes=" << g_config.item2.intervalMinutes << std::endl;
        iniFile << "ShowNotification=" << (g_config.item2.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[Milk]" << std::endl;
        iniFile << "Enabled=" << (g_config.milk.enabled ? "true" : "false") << std::endl;
        iniFile << "ID=" << g_config.milk.id << std::endl;
        iniFile << "Plugin=" << g_config.milk.plugin << std::endl;
        iniFile << "Amount=" << g_config.milk.amount << std::endl;
        iniFile << "IntervalMinutes=" << g_config.milk.intervalMinutes << std::endl;
        iniFile << "ShowNotification=" << (g_config.milk.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[BWY_Wench_Milk]" << std::endl;
        iniFile << "Enabled=" << (g_config.milkWench.enabled ? "true" : "false") << std::endl;
        iniFile << "ID=" << g_config.milkWench.id << std::endl;
        iniFile << "Plugin=" << g_config.milkWench.plugin << std::endl;
        iniFile << "Amount=" << g_config.milkWench.amount << std::endl;
        iniFile << "IntervalMinutes=" << g_config.milkWench.intervalMinutes << std::endl;
        iniFile << "ShowNotification=" << (g_config.milkWench.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[BWY_Milk_Ethel]" << std::endl;
        iniFile << "Enabled=" << (g_config.milkEthel.enabled ? "true" : "false") << std::endl;
        iniFile << "ID=" << g_config.milkEthel.id << std::endl;
        iniFile << "PluginItem=" << g_config.milkEthel.pluginItem << std::endl;
        iniFile << "NPC=" << g_config.milkEthel.npc << std::endl;
        iniFile << "PluginNPC=" << g_config.milkEthel.pluginNPC << std::endl;
        iniFile << "Amount=" << g_config.milkEthel.amount << std::endl;
        iniFile << "IntervalMinutes=" << g_config.milkEthel.intervalMinutes << std::endl;
        iniFile << "ShowNotification=" << (g_config.milkEthel.showNotification ? "true" : "false") << std::endl;
        iniFile << std::endl;

        iniFile << "[Frostfall]" << std::endl;
        iniFile << "Enabled=true" << std::endl;
        iniFile << "ReductionAmount_Frost_ColdNeedValue=100" << std::endl;
        iniFile << "IntervalSeconds=60" << std::endl;
        iniFile << "ActivationThreshold=100" << std::endl;
        iniFile << "ShowNotification=true" << std::endl;
        iniFile << std::endl;

        iniFile << "[Notification]" << std::endl;
        iniFile << "Enabled=" << (g_config.notification.enabled ? "true" : "false") << std::endl;

        iniFile.close();
    }
}

void ResolveItemFormIDs() {
    if (g_cachedItemFormIDs.resolved) {
        return;
    }
    
    if (g_config.item1.enabled && g_config.item1.plugin != "none" && g_config.item1.id != "xxxxxx") {
        g_cachedItemFormIDs.item1 = GetFormIDFromPlugin(g_config.item1.plugin, g_config.item1.id);
        if (g_cachedItemFormIDs.item1 != 0) {
            WriteToActionsLog("Item1 (" + g_config.item1.itemName + ") resolved successfully - FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.item1), __LINE__);
        } else {
            WriteToActionsLog("WARNING: Item1 (" + g_config.item1.itemName + ") FormID resolution failed", __LINE__);
        }
    }
    
    if (g_config.item2.enabled && g_config.item2.plugin != "none" && g_config.item2.id != "xxxxxx") {
        g_cachedItemFormIDs.item2 = GetFormIDFromPlugin(g_config.item2.plugin, g_config.item2.id);
        if (g_cachedItemFormIDs.item2 != 0) {
            WriteToActionsLog("Item2 (" + g_config.item2.itemName + ") resolved successfully - FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.item2), __LINE__);
        } else {
            WriteToActionsLog("WARNING: Item2 (" + g_config.item2.itemName + ") FormID resolution failed", __LINE__);
        }
    }
    
    if (g_config.milk.enabled) {
        g_cachedItemFormIDs.milkDawnguard = GetFormIDFromPlugin(g_config.milk.plugin, g_config.milk.id);
        if (g_cachedItemFormIDs.milkDawnguard != 0) {
            WriteToActionsLog("Milk (Dawnguard) resolved successfully - FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.milkDawnguard), __LINE__);
        } else {
            WriteToActionsLog("WARNING: Milk (Dawnguard) FormID resolution failed", __LINE__);
        }
    }
    
    if (g_config.milkWench.enabled) {
        g_cachedItemFormIDs.milkWench = GetFormIDFromPlugin(g_config.milkWench.plugin, g_config.milkWench.id);
        if (g_cachedItemFormIDs.milkWench != 0) {
            WriteToActionsLog("Wench Milk resolved successfully - FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.milkWench), __LINE__);
        } else {
            WriteToActionsLog("WARNING: Wench Milk FormID resolution failed", __LINE__);
        }
    }
    
    if (g_config.milkEthel.enabled) {
        g_cachedItemFormIDs.milkEthel = GetFormIDFromPlugin(g_config.milkEthel.pluginItem, g_config.milkEthel.id);
        if (g_cachedItemFormIDs.milkEthel != 0) {
            WriteToActionsLog("Milk (Ethel) resolved successfully - FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.milkEthel), __LINE__);
        } else {
            WriteToActionsLog("WARNING: Milk (Ethel) FormID resolution failed", __LINE__);
        }
    }
    
    g_cachedItemFormIDs.resolved = true;
}

void TryCaptureNPCFormIDs() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return;
    
    RE::NiPoint3 playerPos = player->GetPosition();
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;
    
    if (g_config.milkWench.enabled && !g_capturedYurianaWenchNPC.captured) {
        auto* file = dataHandler->LookupModByName(g_config.milkWench.plugin);
        if (file) {
            uint8_t modIndex = file->compileIndex;
            if (modIndex == 0xFF) {
                modIndex = file->smallFileCompileIndex;
            }
            
            for (auto& actorHandle : processLists->highActorHandles) {
                auto actor = actorHandle.get();
                if (!actor) continue;
                
                auto* actorBase = actor->GetActorBase();
                if (!actorBase) continue;
                
                uint8_t npcModIndex = (actorBase->formID >> 24) & 0xFF;
                if (npcModIndex == modIndex) {
                    RE::NiPoint3 npcPos = actor->GetPosition();
                    float distance = playerPos.GetDistance(npcPos);
                    
                    if (distance <= 500.0f) {
                        g_capturedYurianaWenchNPC.formID = actorBase->formID;
                        g_capturedYurianaWenchNPC.pluginName = g_config.milkWench.plugin;
                        g_capturedYurianaWenchNPC.captured = true;
                        g_capturedYurianaWenchNPC.lastSeen = std::chrono::steady_clock::now();
                        
                        WriteToActionsLog("Auto-captured YurianaWench NPC", __LINE__);
                        break;
                    }
                }
            }
        }
    }
    
    if (g_config.milkEthel.enabled && !g_capturedEthelNPC.captured) {
        std::string cleanID = g_config.milkEthel.npc;
        
        if (cleanID.length() >= 2 && cleanID.substr(0, 2) == "XX") {
            cleanID = cleanID.substr(2);
        }
        
        RE::FormID targetFormID = GetFormIDFromPlugin(g_config.milkEthel.pluginNPC, cleanID);
        
        if (targetFormID != 0) {
            for (auto& actorHandle : processLists->highActorHandles) {
                auto actor = actorHandle.get();
                if (!actor) continue;
                
                auto* actorBase = actor->GetActorBase();
                if (!actorBase) continue;
                
                if (actorBase->formID == targetFormID) {
                    RE::NiPoint3 npcPos = actor->GetPosition();
                    float distance = playerPos.GetDistance(npcPos);
                    
                    if (distance <= 500.0f) {
                        g_capturedEthelNPC.formID = targetFormID;
                        g_capturedEthelNPC.pluginName = g_config.milkEthel.pluginNPC;
                        g_capturedEthelNPC.captured = true;
                        g_capturedEthelNPC.lastSeen = std::chrono::steady_clock::now();
                        
                        WriteToActionsLog("Auto-captured Ethel NPC", __LINE__);
                        break;
                    }
                }
            }
        }
    }
}

void CheckForNearbyNPCs() {
    if (!IsInOStimScene()) {
        if (g_wenchMilkNPCDetected || g_ethelNPCDetected) {
            g_wenchMilkNPCDetected = false;
            g_ethelNPCDetected = false;
        }
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastNPCDetectionCheck).count();
    
    if (elapsed < 2) {
        return;
    }
    
    g_lastNPCDetectionCheck = now;
    
    TryCaptureNPCFormIDs();
    
    if (g_config.milkWench.enabled) {
        bool isNearby = false;
        
        if (g_capturedYurianaWenchNPC.captured) {
            isNearby = IsSpecificNPCNearPlayer(g_capturedYurianaWenchNPC.formID, 500.0f);
            
            if (isNearby) {
                g_capturedYurianaWenchNPC.lastSeen = now;
            }
        } else {
            isNearby = IsAnyNPCFromPluginNearPlayer(g_config.milkWench.plugin, 500.0f);
        }
        
        if (isNearby && !g_wenchMilkNPCDetected) {
            g_wenchMilkNPCDetected = true;
            if (g_config.notification.enabled && g_config.milkWench.showNotification) {
                RE::DebugNotification("OSurvival - You have a wench nearby who will assist you on this cold evening");
            }
            WriteToActionsLog("YurianaWench NPC detected nearby (Wench Milk eligible)", __LINE__);
        } else if (!isNearby && g_wenchMilkNPCDetected) {
            g_wenchMilkNPCDetected = false;
            WriteToActionsLog("YurianaWench NPC left detection range", __LINE__);
        }
    }
    
    if (g_config.milkEthel.enabled) {
        bool isNearby = false;
        
        if (g_capturedEthelNPC.captured) {
            isNearby = IsSpecificNPCNearPlayer(g_capturedEthelNPC.formID, 500.0f);
            
            if (isNearby) {
                g_capturedEthelNPC.lastSeen = now;
            }
        }
        
        if (isNearby && !g_ethelNPCDetected) {
            g_ethelNPCDetected = true;
            if (g_config.notification.enabled && g_config.milkEthel.showNotification) {
                RE::DebugNotification("OSurvival - Ethel the Cute little Cow is with you!");
            }
            WriteToActionsLog("Ethel NPC detected nearby (Milk Ethel eligible)", __LINE__);
        } else if (!isNearby && g_ethelNPCDetected) {
            g_ethelNPCDetected = false;
            WriteToActionsLog("Ethel NPC left detection range", __LINE__);
        }
    }
}

void CheckAndRewardGold() {
    LoadConfiguration();

    if (!g_config.gold.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastGoldRewardTime).count();

    int intervalSeconds = g_config.gold.intervalMinutes * 60;
    if (elapsed >= intervalSeconds) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);

        if (player && gold) {
            player->AddObjectToContainer(gold, nullptr, g_config.gold.amount, nullptr);

            if (g_config.notification.enabled && g_config.gold.showNotification) {
                std::string msg = "OSurvival - Incredible resistance rewarded with " +
                                  std::to_string(g_config.gold.amount) + " gold";
                RE::DebugNotification(msg.c_str());
            }

            WriteToActionsLog("Player received " + std::to_string(g_config.gold.amount) +
                                  " gold (OStim scene: " + GetLastAnimation() + ")",
                              __LINE__);
        }

        g_lastGoldRewardTime = now;
    }
}

void CheckAndRewardItem1() {
    LoadConfiguration();

    if (!g_config.item1.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastItem1RewardTime).count();

    int intervalSeconds = g_config.item1.intervalMinutes * 60;
    if (elapsed >= intervalSeconds) {
        if (g_cachedItemFormIDs.item1 == 0) {
            WriteToActionsLog("DEBUG: Item1 - Cached FormID is 0, skipping reward", __LINE__);
            g_lastItem1RewardTime = now;
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            WriteToActionsLog("DEBUG: Item1 - Player pointer is nullptr", __LINE__);
            g_lastItem1RewardTime = now;
            return;
        }

        auto* itemForm = RE::TESForm::LookupByID(g_cachedItemFormIDs.item1);
        if (!itemForm) {
            WriteToActionsLog("DEBUG: Item1 - TESForm::LookupByID returned nullptr for FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.item1), __LINE__);
            g_lastItem1RewardTime = now;
            return;
        }

        auto* item = itemForm->As<RE::TESBoundObject>();
        if (!item) {
            WriteToActionsLog("DEBUG: Item1 - Form is not a TESBoundObject, FormType: " + 
                std::to_string(static_cast<int>(itemForm->GetFormType())), __LINE__);
            g_lastItem1RewardTime = now;
            return;
        }

        player->AddObjectToContainer(item, nullptr, g_config.item1.amount, nullptr);

        if (g_config.notification.enabled && g_config.item1.showNotification) {
            std::string msg = "OSurvival - Received " + std::to_string(g_config.item1.amount) + " " + g_config.item1.itemName;
            RE::DebugNotification(msg.c_str());
        }

        WriteToActionsLog("Player received " + std::to_string(g_config.item1.amount) +
                              " " + g_config.item1.itemName + " (OStim scene: " + GetLastAnimation() + ")",
                          __LINE__);

        g_lastItem1RewardTime = now;
    }
}

void CheckAndRewardItem2() {
    LoadConfiguration();

    if (!g_config.item2.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastItem2RewardTime).count();

    int intervalSeconds = g_config.item2.intervalMinutes * 60;
    if (elapsed >= intervalSeconds) {
        if (g_cachedItemFormIDs.item2 == 0) {
            WriteToActionsLog("DEBUG: Item2 - Cached FormID is 0, skipping reward", __LINE__);
            g_lastItem2RewardTime = now;
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            WriteToActionsLog("DEBUG: Item2 - Player pointer is nullptr", __LINE__);
            g_lastItem2RewardTime = now;
            return;
        }

        auto* itemForm = RE::TESForm::LookupByID(g_cachedItemFormIDs.item2);
        if (!itemForm) {
            WriteToActionsLog("DEBUG: Item2 - TESForm::LookupByID returned nullptr for FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.item2), __LINE__);
            g_lastItem2RewardTime = now;
            return;
        }

        auto* item = itemForm->As<RE::TESBoundObject>();
        if (!item) {
            WriteToActionsLog("DEBUG: Item2 - Form is not a TESBoundObject, FormType: " + 
                std::to_string(static_cast<int>(itemForm->GetFormType())), __LINE__);
            g_lastItem2RewardTime = now;
            return;
        }

        player->AddObjectToContainer(item, nullptr, g_config.item2.amount, nullptr);

        if (g_config.notification.enabled && g_config.item2.showNotification) {
            std::string msg = "OSurvival - Received " + std::to_string(g_config.item2.amount) + " " + g_config.item2.itemName;
            RE::DebugNotification(msg.c_str());
        }

        WriteToActionsLog("Player received " + std::to_string(g_config.item2.amount) +
                              " " + g_config.item2.itemName + " (OStim scene: " + GetLastAnimation() + ")",
                          __LINE__);

        g_lastItem2RewardTime = now;
    }
}

void CheckAndRewardMilk() {
    LoadConfiguration();

    if (!g_config.milk.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastMilkRewardTime).count();

    int intervalSeconds = g_config.milk.intervalMinutes * 60;
    if (elapsed >= intervalSeconds) {
        if (g_cachedItemFormIDs.milkDawnguard == 0) {
            WriteToActionsLog("DEBUG: Milk (Dawnguard) - Cached FormID is 0, skipping reward", __LINE__);
            g_lastMilkRewardTime = now;
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            WriteToActionsLog("DEBUG: Milk (Dawnguard) - Player pointer is nullptr", __LINE__);
            g_lastMilkRewardTime = now;
            return;
        }

        auto* milkForm = RE::TESForm::LookupByID(g_cachedItemFormIDs.milkDawnguard);
        if (!milkForm) {
            WriteToActionsLog("DEBUG: Milk (Dawnguard) - TESForm::LookupByID returned nullptr for FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.milkDawnguard), __LINE__);
            g_lastMilkRewardTime = now;
            return;
        }

        auto* milkItem = milkForm->As<RE::TESBoundObject>();
        if (!milkItem) {
            WriteToActionsLog("DEBUG: Milk (Dawnguard) - Form is not a TESBoundObject, FormType: " + 
                std::to_string(static_cast<int>(milkForm->GetFormType())), __LINE__);
            g_lastMilkRewardTime = now;
            return;
        }

        player->AddObjectToContainer(milkItem, nullptr, g_config.milk.amount, nullptr);

        if (g_config.notification.enabled && g_config.milk.showNotification) {
            std::string msg = "OSurvival - Received " + std::to_string(g_config.milk.amount) + " Milk";
            RE::DebugNotification(msg.c_str());
        }

        WriteToActionsLog("Player received " + std::to_string(g_config.milk.amount) +
                              " Milk (OStim scene: " + GetLastAnimation() + ")",
                          __LINE__);

        g_lastMilkRewardTime = now;
    }
}

void CheckAndRewardMilkWench() {
    LoadConfiguration();

    if (!g_config.milkWench.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }
    
    if (!g_wenchMilkNPCDetected) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastMilkWenchRewardTime).count();
    int intervalSeconds = g_config.milkWench.intervalMinutes * 60;
    
    if (elapsed >= intervalSeconds) {
        if (g_cachedItemFormIDs.milkWench == 0) {
            WriteToActionsLog("DEBUG: Wench Milk - Cached FormID is 0, skipping reward", __LINE__);
            g_lastMilkWenchRewardTime = now;
            return;
        }
        
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            WriteToActionsLog("DEBUG: Wench Milk - Player pointer is nullptr", __LINE__);
            g_lastMilkWenchRewardTime = now;
            return;
        }

        auto* milkForm = RE::TESForm::LookupByID(g_cachedItemFormIDs.milkWench);
        if (!milkForm) {
            WriteToActionsLog("DEBUG: Wench Milk - TESForm::LookupByID returned nullptr for FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.milkWench), __LINE__);
            g_lastMilkWenchRewardTime = now;
            return;
        }

        auto* milkItem = milkForm->As<RE::TESBoundObject>();
        if (!milkItem) {
            WriteToActionsLog("DEBUG: Wench Milk - Form is not a TESBoundObject, FormType: " + 
                std::to_string(static_cast<int>(milkForm->GetFormType())), __LINE__);
            g_lastMilkWenchRewardTime = now;
            return;
        }
        
        player->AddObjectToContainer(milkItem, nullptr, g_config.milkWench.amount, nullptr);
        
        if (g_config.notification.enabled && g_config.milkWench.showNotification) {
            std::string msg = "OSurvival - Received " + std::to_string(g_config.milkWench.amount) + " Wench Milk";
            RE::DebugNotification(msg.c_str());
        }
        
        WriteToActionsLog("Player received " + std::to_string(g_config.milkWench.amount) +
                          " Wench Milk with NPC nearby (OStim scene: " + GetLastAnimation() + ")",
                          __LINE__);
        
        g_lastMilkWenchRewardTime = now;
    }
}

void CheckAndRewardMilkEthel() {
    LoadConfiguration();

    if (!g_config.milkEthel.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }
    
    if (!g_ethelNPCDetected) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastMilkEthelRewardTime).count();
    int intervalSeconds = g_config.milkEthel.intervalMinutes * 60;
    
    if (elapsed >= intervalSeconds) {
        if (g_cachedItemFormIDs.milkEthel == 0) {
            WriteToActionsLog("DEBUG: Milk Ethel - Cached FormID is 0, skipping reward", __LINE__);
            g_lastMilkEthelRewardTime = now;
            return;
        }
        
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            WriteToActionsLog("DEBUG: Milk Ethel - Player pointer is nullptr", __LINE__);
            g_lastMilkEthelRewardTime = now;
            return;
        }

        auto* milkForm = RE::TESForm::LookupByID(g_cachedItemFormIDs.milkEthel);
        if (!milkForm) {
            WriteToActionsLog("DEBUG: Milk Ethel - TESForm::LookupByID returned nullptr for FormID: 0x" + 
                std::to_string(g_cachedItemFormIDs.milkEthel), __LINE__);
            g_lastMilkEthelRewardTime = now;
            return;
        }

        auto* milkItem = milkForm->As<RE::TESBoundObject>();
        if (!milkItem) {
            WriteToActionsLog("DEBUG: Milk Ethel - Form is not a TESBoundObject, FormType: " + 
                std::to_string(static_cast<int>(milkForm->GetFormType())), __LINE__);
            g_lastMilkEthelRewardTime = now;
            return;
        }
        
        player->AddObjectToContainer(milkItem, nullptr, g_config.milkEthel.amount, nullptr);
        
        if (g_config.notification.enabled && g_config.milkEthel.showNotification) {
            std::string msg = "OSurvival - Received " + std::to_string(g_config.milkEthel.amount) + " Milk Ethel";
            RE::DebugNotification(msg.c_str());
        }
        
        WriteToActionsLog("Player received " + std::to_string(g_config.milkEthel.amount) +
                          " Milk Ethel with Ethel nearby (OStim scene: " + GetLastAnimation() + ")",
                          __LINE__);
        
        g_lastMilkEthelRewardTime = now;
    }
}

void CheckAndRestoreSurvivalStats() {
    LoadConfiguration();

    if (!g_config.survival.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastSurvivalReductionTime).count();

    if (elapsed < g_config.survival.intervalSeconds) {
        return;
    }

    auto hungerGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_HungerNeedValue");
    auto coldGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ColdNeedValue");
    auto exhaustionGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ExhaustionNeedValue");

    if (!hungerGlobal || !coldGlobal || !exhaustionGlobal) {
        return;
    }

    float currentHunger = hungerGlobal->value;
    float currentCold = coldGlobal->value;
    float currentExhaustion = exhaustionGlobal->value;

    if (currentHunger <= 0.0f && currentCold <= 0.0f && currentExhaustion <= 0.0f) {
        if (!g_allStatsAtZero) {
            if (g_config.notification.enabled && g_config.survival.showNotification) {
                RE::DebugNotification("OSurvival - Full recovery achieved");
            }
            WriteToActionsLog("All survival stats at 0 - fully recovered", __LINE__);
            g_allStatsAtZero = true;
            g_survivalRestorationActive = false;
        }
        return;
    }

    if (!g_survivalRestorationActive) {
        if (currentHunger > g_config.survival.activationThreshold ||
            currentCold > g_config.survival.activationThreshold ||
            currentExhaustion > g_config.survival.activationThreshold) {
            g_survivalRestorationActive = true;
            WriteToActionsLog("Survival restoration system activated", __LINE__);
        } else {
            return;
        }
    }

    float newHunger = std::max(0.0f, currentHunger - static_cast<float>(g_config.survival.reductionAmountHunger));
    float newCold = std::max(0.0f, currentCold - static_cast<float>(g_config.survival.reductionAmountCold));
    float newExhaustion =
        std::max(0.0f, currentExhaustion - static_cast<float>(g_config.survival.reductionAmountExhaustion));

    hungerGlobal->value = newHunger;
    coldGlobal->value = newCold;
    exhaustionGlobal->value = newExhaustion;

    if (g_config.notification.enabled && g_config.survival.showNotification) {
        RE::DebugNotification("OSurvival - You gain warmth with your partner and feel better");
    }

    std::stringstream logMsg;
    logMsg << "Survival stats reduced: "
           << "Hunger " << currentHunger << "->" << newHunger << ", "
           << "Cold " << currentCold << "->" << newCold << ", "
           << "Exhaustion " << currentExhaustion << "->" << newExhaustion;

    std::string logStr = logMsg.str();
    WriteToActionsLog(logStr, __LINE__);

    g_lastSurvivalReductionTime = now;
}

void CheckAndRestoreAttributes() {
    LoadConfiguration();

    if (!g_config.attributes.enabled || !IsInOStimScene() || GetLastAnimation().empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastAttributesRestorationTime).count();

    if (elapsed < g_config.attributes.intervalSeconds) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto* actorValueOwner = player->AsActorValueOwner();
    if (actorValueOwner) {
        float amount = static_cast<float>(g_config.attributes.restorationAmount);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, amount);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, amount);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, amount);

        if (g_config.notification.enabled && g_config.attributes.showNotification) {
            std::string msg =
                "OSurvival - Attributes restored " + std::to_string(g_config.attributes.restorationAmount) + " points";
            RE::DebugNotification(msg.c_str());
        }

        WriteToActionsLog("Player received " + std::to_string(g_config.attributes.restorationAmount) +
                              " points in all attributes (Health, Magicka, Stamina)",
                          __LINE__);
    }

    g_lastAttributesRestorationTime = now;
}

class OStimModEventSink : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
public:
    static OStimModEventSink& GetSingleton() {
        static OStimModEventSink singleton;
        return singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
                                          RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        std::string eventName = event->eventName.c_str();
        
        if (eventName.find("ostim_") == 0) {
            WriteToOStimEventsLog("========================================", __LINE__);
            WriteToOStimEventsLog("OSTIM MOD EVENT RECEIVED", __LINE__);
            WriteToOStimEventsLog("Event Name: " + eventName, __LINE__);
            
            std::string strArg = (event->strArg.c_str() != nullptr && strlen(event->strArg.c_str()) > 0) 
                ? std::string(event->strArg.c_str()) : "(null)";
            WriteToOStimEventsLog("String Argument: " + strArg, __LINE__);
            WriteToOStimEventsLog("Numeric Argument: " + std::to_string(event->numArg), __LINE__);
            
            if (eventName == "ostim_orgasm" || eventName == "ostim_actor_orgasm") {
                HandleOrgasm(event);
            }
            
            WriteToOStimEventsLog("========================================", __LINE__);
        }
        
        return RE::BSEventNotifyControl::kContinue;
    }

private:
    void HandleOrgasm(const SKSE::ModCallbackEvent* event) {
        WriteToOStimEventsLog("ORGASM EVENT DETECTED", __LINE__);
        
        std::string actorName = "";
        bool isPlayer = false;
        std::string gender = "";
        
        if (event->sender) {
            auto* actor = event->sender->As<RE::Actor>();
            if (actor) {
                auto* base = actor->GetActorBase();
                if (base) {
                    actorName = std::string(base->GetName());
                    gender = base->IsFemale() ? "Female" : "Male";
                    
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    isPlayer = (actor == player);
                    
                    WriteToOStimEventsLog("Actor: " + actorName, __LINE__);
                    WriteToOStimEventsLog("Gender: " + gender, __LINE__);
                    WriteToOStimEventsLog("Is Player: " + std::string(isPlayer ? "Yes" : "No"), __LINE__);
                }
            }
        }
        
        if (!actorName.empty() && !gender.empty()) {
            ProcessOrgasmEvent(actorName, gender, isPlayer);
        }
    }
};

class GameEventProcessor : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    GameEventProcessor() = default;
    ~GameEventProcessor() = default;
    GameEventProcessor(const GameEventProcessor&) = delete;
    GameEventProcessor(GameEventProcessor&&) = delete;
    GameEventProcessor& operator=(const GameEventProcessor&) = delete;
    GameEventProcessor& operator=(GameEventProcessor&&) = delete;

public:
    static GameEventProcessor& GetSingleton() {
        static GameEventProcessor singleton;
        return singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        if (event) {
            std::stringstream msg;
            msg << "Menu " << event->menuName.c_str() << " " << (event->opening ? "opened" : "closed");
            WriteToActionsLog(msg.str(), __LINE__);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

bool DetectSceneEnd(const std::string& line) {
    if (line.find("[Thread.cpp:634] closing thread") != std::string::npos) {
        WriteToAnimationsLog("DETECTED: OStim thread closing", __LINE__);
        return true;
    }
    if (line.find("[ThreadManager.cpp:174] trying to stop thread") != std::string::npos) {
        WriteToAnimationsLog("DETECTED: OStim trying to stop thread", __LINE__);
        return true;
    }
    return false;
}

std::string DetectAnimationChange(const std::string& line) {
    std::string animationName;

    if (line.find("[info]") != std::string::npos &&
        line.find("[Thread.cpp:195] thread 0 changed to node") != std::string::npos) {
        size_t nodePos = line.find("changed to node ");
        if (nodePos != std::string::npos) {
            size_t startPos = nodePos + 16;
            if (startPos < line.length()) {
                animationName = line.substr(startPos);
            }
        }
    } else if (line.find("[info]") != std::string::npos &&
               line.find("[OStimMenu.h:48] UI_TransitionRequest") != std::string::npos) {
        size_t lastOpenBrace = line.rfind('{');
        size_t lastCloseBrace = line.rfind('}');
        if (lastOpenBrace != std::string::npos && lastCloseBrace != std::string::npos &&
            lastCloseBrace > lastOpenBrace) {
            animationName = line.substr(lastOpenBrace + 1, lastCloseBrace - lastOpenBrace - 1);
        }
    }

    if (!animationName.empty()) {
        animationName.erase(animationName.find_last_not_of(" \n\r\t") + 1);
    }

    return animationName;
}

void ProcessNewLine(const std::string& line, const std::string& hashStr) {
    if (line.find("[warning]") != std::string::npos) {
        return;
    }

    if (g_processedLines.find(hashStr) != g_processedLines.end()) {
        return;
    }
    
    ParseOStimEventFromLine(line);

    if (DetectSceneEnd(line)) {
        g_processedLines.insert(hashStr);
        if (IsInOStimScene()) {
            SetInOStimScene(false);
            g_goldRewardActive = false;
            g_item1RewardActive = false;
            g_item2RewardActive = false;
            g_milkRewardActive = false;
            g_milkWenchRewardActive = false;
            g_milkEthelRewardActive = false;
            g_survivalRestorationActive = false;
            g_allStatsAtZero = false;
            g_attributesRestorationActive = false;
            
            g_detectedNPCNames.clear();
            g_npcNameToRefID.clear();
            g_sceneActors.clear();
            
            WriteToActionsLog("OStim scene ended - all reward systems stopped", __LINE__);
        }
        WriteToAnimationsLog("OStim scene ended", __LINE__);
        return;
    }

    DetectNPCNamesFromLine(line);
    
    std::string animationName = DetectAnimationChange(line);
    if (!animationName.empty()) {
        g_processedLines.insert(hashStr);

        if (animationName == GetLastAnimation()) {
            return;
        }

        SetLastAnimation(animationName);

        if (!IsInOStimScene()) {
            BuildNPCsCacheForScene();
            
            SetInOStimScene(true);
            
            bool playerExists = false;
            for (const auto& actor : g_sceneActors) {
                if (actor.refID == 0x14) {
                    playerExists = true;
                    break;
                }
            }
            
            if (!playerExists) {
                ActorInfo playerInfo = CapturePlayerInfo();
                if (playerInfo.captured) {
                    g_sceneActors.push_back(playerInfo);
                    LogActorInfo(playerInfo, true);
                }
            }
            
            WriteToOStimEventsLog("========================================", __LINE__);
            WriteToOStimEventsLog("SCENE START EVENT", __LINE__);
            WriteToOStimEventsLog("New OStim scene started", __LINE__);
            WriteToOStimEventsLog("Starting animation: " + animationName, __LINE__);
            WriteToOStimEventsLog("========================================", __LINE__);
            
            g_currentOStimSpeed = 0;
            g_lastOStimEventCheck = std::chrono::steady_clock::now();
            
            ResolveItemFormIDs();
            
            g_lastGoldRewardTime = std::chrono::steady_clock::now();
            g_goldRewardActive = true;
            g_lastItem1RewardTime = std::chrono::steady_clock::now();
            g_item1RewardActive = true;
            g_lastItem2RewardTime = std::chrono::steady_clock::now();
            g_item2RewardActive = true;
            g_lastMilkRewardTime = std::chrono::steady_clock::now();
            g_milkRewardActive = true;
            g_lastMilkWenchRewardTime = std::chrono::steady_clock::now();
            g_milkWenchRewardActive = true;
            g_lastMilkEthelRewardTime = std::chrono::steady_clock::now();
            g_milkEthelRewardActive = true;
            g_lastSurvivalReductionTime = std::chrono::steady_clock::now();
            g_survivalRestorationActive = false;
            g_allStatsAtZero = false;
            g_lastAttributesRestorationTime = std::chrono::steady_clock::now();
            g_attributesRestorationActive = true;
            g_lastNPCDetectionCheck = std::chrono::steady_clock::now();
            g_wenchMilkNPCDetected = false;
            g_ethelNPCDetected = false;

            auto hungerGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_HungerNeedValue");
            auto coldGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ColdNeedValue");
            auto exhaustionGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ExhaustionNeedValue");

            if (hungerGlobal && coldGlobal && exhaustionGlobal) {
                g_lastHungerValue = hungerGlobal->value;
                g_lastColdValue = coldGlobal->value;
                g_lastExhaustionValue = exhaustionGlobal->value;

                std::stringstream msg;
                msg << "Initial survival stats - Hunger: " << g_lastHungerValue << ", Cold: " << g_lastColdValue
                    << ", Exhaustion: " << g_lastExhaustionValue;
                std::string msgStr = msg.str();
                WriteToActionsLog(msgStr, __LINE__);
            }

            WriteToActionsLog("OStim scene started - all reward systems activated", __LINE__);
        }

        if (g_processedLines.size() > 500) {
            g_processedLines.clear();
        }

        std::string formattedAnimation = "{" + animationName + "}";
        WriteToAnimationsLog(formattedAnimation, __LINE__);
    }
}

void ProcessOStimLog() {
    try {
        if (g_isShuttingDown.load()) {
            return;
        }

        if (!g_initialDelayComplete) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsedSeconds =
                std::chrono::duration_cast<std::chrono::seconds>(currentTime - g_monitoringStartTime).count();
            if (elapsedSeconds < 5) {
                return;
            } else {
                g_initialDelayComplete = true;
                WriteToAnimationsLog("5-second initial delay complete, starting dual-path OStim.log monitoring",
                                     __LINE__);
            }
        }

        std::vector<fs::path> ostimLogPaths = {g_ostimLogPaths.primary / "OStim.log",
                                               g_ostimLogPaths.secondary / "OStim.log"};

        fs::path activeOStimLogPath;
        bool foundLog = false;

        for (const auto& logPath : ostimLogPaths) {
            if (fs::exists(logPath)) {
                activeOStimLogPath = logPath;
                foundLog = true;
                break;
            }
        }

        if (!foundLog) {
            return;
        }

        size_t currentFileSize = fs::file_size(activeOStimLogPath);
        if (currentFileSize < g_lastFileSize) {
            g_lastOStimLogPosition = 0;
            g_processedLines.clear();
            SetLastAnimation("");
            WriteToAnimationsLog("OStim.log reset detected - restarting monitoring", __LINE__);
        } else if (currentFileSize == g_lastFileSize && g_lastOStimLogPosition > 0) {
            return;
        }

        g_lastFileSize = currentFileSize;

        std::ifstream ostimLog(activeOStimLogPath, std::ios::in);
        if (!ostimLog.is_open()) {
            return;
        }

        if (g_lastOStimLogPosition == 0) {
            ostimLog.seekg(0, std::ios::beg);
        } else {
            ostimLog.seekg(g_lastOStimLogPosition);
        }

        std::string line;
        while (std::getline(ostimLog, line)) {
            size_t lineHash = std::hash<std::string>{}(line);
            std::string hashStr = std::to_string(lineHash);
            ProcessNewLine(line, hashStr);
        }

        g_lastOStimLogPosition = ostimLog.tellg();
        if (ostimLog.eof()) {
            ostimLog.clear();
            ostimLog.seekg(0, std::ios::end);
            g_lastOStimLogPosition = ostimLog.tellg();
        }

        ostimLog.close();

    } catch (const std::exception& e) {
        logger::error("Error processing OStim.log: {}", e.what());
    } catch (...) {
        logger::error("Unknown error processing OStim.log");
    }
}

void FileWatchThreadFunction() {
    WriteToAnimationsLog("File watch thread started using Windows ReadDirectoryChangesW", __LINE__);

    std::vector<fs::path> watchPaths = {g_ostimLogPaths.primary, g_ostimLogPaths.secondary};

    for (const auto& watchPath : watchPaths) {
        if (!fs::exists(watchPath)) {
            continue;
        }

        HANDLE hDir = CreateFileW(watchPath.wstring().c_str(), FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS, NULL);

        if (hDir == INVALID_HANDLE_VALUE) {
            continue;
        }

        g_directoryHandle = hDir;

        char buffer[4096];
        DWORD bytesReturned;

        while (g_fileWatchActive && !g_isShuttingDown.load()) {
            BOOL result = ReadDirectoryChangesW(
                hDir, buffer, sizeof(buffer), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME, &bytesReturned,
                NULL, NULL);

            if (!result) {
                break;
            }

            if (bytesReturned == 0) {
                continue;
            }

            FILE_NOTIFY_INFORMATION* pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);

            do {
                std::wstring filename(pNotify->FileName, pNotify->FileNameLength / sizeof(wchar_t));
                if (filename == L"OStim.log") {
                    ProcessOStimLog();
                }

                if (pNotify->NextEntryOffset == 0) {
                    break;
                }
                pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<BYTE*>(pNotify) +
                                                                      pNotify->NextEntryOffset);
            } while (true);
        }

        CloseHandle(hDir);
        g_directoryHandle = INVALID_HANDLE_VALUE;

        if (g_fileWatchActive && !g_isShuttingDown.load()) {
            continue;
        } else {
            break;
        }
    }
}

void StartFileWatch() {
    if (!g_fileWatchActive) {
        g_fileWatchActive = true;
        g_fileWatchThread = std::thread(FileWatchThreadFunction);
        WriteToAnimationsLog("File watch system activated", __LINE__);
    }
}

void StopFileWatch() {
    if (g_fileWatchActive) {
        g_fileWatchActive = false;
        if (g_directoryHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_directoryHandle);
            g_directoryHandle = INVALID_HANDLE_VALUE;
        }
        if (g_fileWatchThread.joinable()) {
            g_fileWatchThread.join();
        }
    }
}

void MonitoringThreadFunction() {
    WriteToAnimationsLog("Monitoring thread started - Watching OStim.log for animations", __LINE__);
    WriteToAnimationsLog("Monitoring OStim.log on dual paths (Primary & Secondary)", __LINE__);
    WriteToAnimationsLog("Primary: " + g_ostimLogPaths.primary.string(), __LINE__);
    WriteToAnimationsLog("Secondary: " + g_ostimLogPaths.secondary.string(), __LINE__);
    WriteToAnimationsLog("Waiting 5 seconds before starting OStim.log analysis", __LINE__);

    g_monitoringStartTime = std::chrono::steady_clock::now();
    g_initialDelayComplete = false;

    while (g_monitoringActive && !g_isShuttingDown.load()) {
        g_monitorCycles++;
        ProcessOStimLog();
        FindAndCacheNPCRefIDs();
        CheckForNearbyNPCs();
        ProcessOStimEventData();
        CheckAndRewardGold();
        CheckAndRewardItem1();
        CheckAndRewardItem2();
        CheckAndRewardMilk();
        CheckAndRewardMilkWench();
        CheckAndRewardMilkEthel();
        CheckAndRestoreSurvivalStats();
        CheckAndRestoreAttributes();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void StartMonitoringThread() {
    if (!g_monitoringActive) {
        g_monitoringActive = true;
        g_monitorCycles = 0;
        g_lastOStimLogPosition = 0;
        g_lastFileSize = 0;
        g_processedLines.clear();
        SetLastAnimation("");
        g_initialDelayComplete = false;
        SetInOStimScene(false);
        g_goldRewardActive = false;
        g_item1RewardActive = false;
        g_item2RewardActive = false;
        g_milkRewardActive = false;
        g_milkWenchRewardActive = false;
        g_milkEthelRewardActive = false;
        g_survivalRestorationActive = false;
        g_allStatsAtZero = false;
        g_attributesRestorationActive = false;
        g_wenchMilkNPCDetected = false;
        g_ethelNPCDetected = false;
        g_capturedYurianaWenchNPC.captured = false;
        g_capturedYurianaWenchNPC.formID = 0;
        g_capturedEthelNPC.captured = false;
        g_capturedEthelNPC.formID = 0;
        g_cachedItemFormIDs.resolved = false;
        g_cachedItemFormIDs.item1 = 0;
        g_cachedItemFormIDs.item2 = 0;
        g_cachedItemFormIDs.milkDawnguard = 0;
        g_cachedItemFormIDs.milkWench = 0;
        g_cachedItemFormIDs.milkEthel = 0;
        g_lastHungerValue = 0.0f;
        g_lastColdValue = 0.0f;
        g_lastExhaustionValue = 0.0f;
        g_detectedNPCNames.clear();
        g_npcNameToRefID.clear();
        g_sceneActors.clear();
        g_monitorThread = std::thread(MonitoringThreadFunction);

        WriteToAnimationsLog("MONITORING SYSTEM ACTIVATED", __LINE__);
    }
}

void StopMonitoringThread() {
    if (g_monitoringActive) {
        g_monitoringActive = false;
        if (g_monitorThread.joinable()) {
            g_monitorThread.join();
        }
    }
}

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) {
        SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        return;
    }
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::info);
}

void InitializePlugin() {
    try {
        LoadConfiguration();
        LoadClimaxConfiguration();

        g_documentsPath = GetDocumentsPath();
        g_gamePath = GetGamePath();
        g_ostimLogPaths = GetAllSKSELogsPaths();

        if (g_gamePath.empty()) {
            g_gamePath = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition";
        }

        auto logsFolder = SKSE::log::log_directory();
        if (logsFolder) {
            auto actionsLogPath = *logsFolder / "OSurvival-Mode-NG-Actions.log";
            std::ofstream clearActions(actionsLogPath, std::ios::trunc);
            clearActions.close();

            auto animationsLogPath = *logsFolder / "OSurvival-Mode-NG-Animations.log";
            std::ofstream clearAnimations(animationsLogPath, std::ios::trunc);
            clearAnimations.close();

            auto eventsLogPath = *logsFolder / "OSurvival-Mode-NG-OStimEvents.log";
            std::ofstream clearEvents(eventsLogPath, std::ios::trunc);
            clearEvents.close();

            std::vector<fs::path> ostimLogPaths = {g_ostimLogPaths.primary / "OStim.log",
                                                   g_ostimLogPaths.secondary / "OStim.log"};

            bool ostimLogFound = false;
            for (const auto& ostimLogPath : ostimLogPaths) {
                if (fs::exists(ostimLogPath)) {
                    auto fileSize = fs::file_size(ostimLogPath);
                    ostimLogFound = true;
                    break;
                }
            }

            if (!ostimLogFound) {
                WriteToAnimationsLog("OStim.log not found - waiting for OStim to create it", __LINE__);
            }
        }

        WriteToAnimationsLog("OSurvival-Mode-NG Plugin - Starting", __LINE__);
        WriteToAnimationsLog("========================================", __LINE__);
        WriteToAnimationsLog("OSurvival-Mode-NG Plugin - v5.1.0", __LINE__);
        WriteToAnimationsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToAnimationsLog("========================================", __LINE__);
        WriteToAnimationsLog("Documents: " + g_documentsPath, __LINE__);
        WriteToAnimationsLog("Game Path: " + g_gamePath, __LINE__);
        WriteToAnimationsLog("Primary SKSE Path: " + g_ostimLogPaths.primary.string(), __LINE__);
        WriteToAnimationsLog("Secondary SKSE Path: " + g_ostimLogPaths.secondary.string(), __LINE__);
        WriteToAnimationsLog("FEATURES: Dual-Path + Gold + Item1 + Item2 + Milk (Dawnguard) + Wench Milk + Milk Ethel + NPC Detection + NPC Auto-Capture + Item Auto-Resolution + Survival + Attributes + INI Config + Auto-Disable Missing Plugins + Debug Logging + OStim Events Logging + Vampire/Werewolf Detection + Enhanced Compatibility + CLIMAX SYSTEM", __LINE__);

        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("OSurvival-Mode-NG Actions Monitor - v5.1.0", __LINE__);
        WriteToActionsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("Monitoring game events: Menu + Gold + Item1 + Item2 + Milk (Dawnguard) + Wench Milk + Milk Ethel + NPC Detection + NPC Auto-Capture + Item Auto-Resolution + Survival + Attributes", __LINE__);
        WriteToActionsLog("Configuration loaded from INI file", __LINE__);
        WriteToActionsLog("INI values are reloaded before each event execution", __LINE__);
        WriteToActionsLog("NPC detection system active (500 unit radius, only during OStim scenes)", __LINE__);
        WriteToActionsLog("NPC auto-capture system enabled for dynamic FormID resolution", __LINE__);
        WriteToActionsLog("Item auto-resolution system enabled for accurate FormID detection", __LINE__);
        WriteToActionsLog("Auto-disable missing plugins system enabled", __LINE__);
        WriteToActionsLog("Debug logging enabled for FormID validation", __LINE__);
        WriteToActionsLog("Item1 and Item2 custom item reward systems enabled", __LINE__);
        WriteToActionsLog("Vampire and Werewolf detection enabled", __LINE__);
        WriteToActionsLog("", __LINE__);

        WriteToOStimEventsLog("========================================", __LINE__);
        WriteToOStimEventsLog("OSurvival-Mode-NG OStim Events Monitor - v5.1.0", __LINE__);
        WriteToOStimEventsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToOStimEventsLog("========================================", __LINE__);
        WriteToOStimEventsLog("OStim events logging system initialized", __LINE__);
        WriteToOStimEventsLog("Monitoring: Animation changes, Speed changes, Scene start/end", __LINE__);
        WriteToOStimEventsLog("========================================", __LINE__);

        g_isInitialized = true;
        WriteToAnimationsLog("PLUGIN INITIALIZED", __LINE__);
        WriteToAnimationsLog("PLUGIN FULLY ACTIVE", __LINE__);
        WriteToAnimationsLog("========================================", __LINE__);
        WriteToAnimationsLog("Starting OStim animation monitoring", __LINE__);

        auto* modEventSource = SKSE::GetModCallbackEventSource();
        if (modEventSource) {
            modEventSource->AddEventSink(&OStimModEventSink::GetSingleton());
            WriteToOStimEventsLog("OStim Mod Event Sink registered successfully", __LINE__);
            WriteToOStimEventsLog("Now listening for ostim_actor_orgasm events", __LINE__);
        } else {
            WriteToOStimEventsLog("WARNING: Failed to register Mod Event Sink", __LINE__);
        }

        StartMonitoringThread();
        StartFileWatch();

    } catch (const std::exception& e) {
        logger::error("CRITICAL ERROR in Initialize: {}", e.what());
    }
}

void ShutdownPlugin() {
    WriteToAnimationsLog("PLUGIN SHUTTING DOWN", __LINE__);
    WriteToActionsLog("PLUGIN SHUTTING DOWN", __LINE__);
    WriteToOStimEventsLog("PLUGIN SHUTTING DOWN", __LINE__);

    g_isShuttingDown = true;

    auto* modEventSource = SKSE::GetModCallbackEventSource();
    if (modEventSource) {
        modEventSource->RemoveEventSink(&OStimModEventSink::GetSingleton());
        WriteToOStimEventsLog("OStim Mod Event Sink unregistered", __LINE__);
    }

    StopFileWatch();
    StopMonitoringThread();

    WriteToAnimationsLog("========================================", __LINE__);
    WriteToAnimationsLog("Plugin shutdown complete at: " + GetCurrentTimeString(), __LINE__);
    WriteToAnimationsLog("========================================", __LINE__);
    
    WriteToOStimEventsLog("========================================", __LINE__);
    WriteToOStimEventsLog("Plugin shutdown complete at: " + GetCurrentTimeString(), __LINE__);
    WriteToOStimEventsLog("========================================", __LINE__);
}

void MessageListener(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kNewGame:
            StopFileWatch();
            StopMonitoringThread();
            g_lastOStimLogPosition = 0;
            g_lastFileSize = 0;
            g_processedLines.clear();
            SetLastAnimation("");
            g_initialDelayComplete = false;
            SetInOStimScene(false);
            g_goldRewardActive = false;
            g_item1RewardActive = false;
            g_item2RewardActive = false;
            g_milkRewardActive = false;
            g_milkWenchRewardActive = false;
            g_milkEthelRewardActive = false;
            g_survivalRestorationActive = false;
            g_allStatsAtZero = false;
            g_attributesRestorationActive = false;
            g_wenchMilkNPCDetected = false;
            g_ethelNPCDetected = false;
            g_capturedYurianaWenchNPC.captured = false;
            g_capturedYurianaWenchNPC.formID = 0;
            g_capturedEthelNPC.captured = false;
            g_capturedEthelNPC.formID = 0;
            g_cachedItemFormIDs.resolved = false;
            g_cachedItemFormIDs.item1 = 0;
            g_cachedItemFormIDs.item2 = 0;
            g_cachedItemFormIDs.milkDawnguard = 0;
            g_cachedItemFormIDs.milkWench = 0;
            g_cachedItemFormIDs.milkEthel = 0;
            g_lastHungerValue = 0.0f;
            g_lastColdValue = 0.0f;
            g_lastExhaustionValue = 0.0f;
            g_detectedNPCNames.clear();
            g_npcNameToRefID.clear();
            g_sceneActors.clear();
            InitializePlugin();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            if (!g_monitoringActive) {
                StartMonitoringThread();
            }
            if (!g_fileWatchActive) {
                StartFileWatch();
            }
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            {
                auto& eventProcessor = GameEventProcessor::GetSingleton();
                RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&eventProcessor);
                WriteToAnimationsLog("Game event processor registered", __LINE__);
                WriteToActionsLog("Event monitoring system active", __LINE__);
                
                ValidateAndUpdatePluginsInINI();
            }
            break;

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();

    logger::info("OSurvival-Mode-NG Plugin v5.1.0 - Starting");

    InitializePlugin();

    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    logger::info("Plugin loaded successfully");

    return true;
}

constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({5, 1, 0});
    v.PluginName("OSurvival-Mode-NG OStim Monitor");
    v.AuthorName("John95AC");
    v.UsesAddressLibrary();
    v.UsesSigScanning();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_LATEST_VR});

    return v;
}();