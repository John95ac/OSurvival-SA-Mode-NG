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
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
namespace logger = SKSE::log;

// ===== GLOBAL VARIABLES =====
static std::deque<std::string> g_actionLines;
static std::deque<std::string> g_animationLines;
static std::string g_documentsPath;
static std::string g_gamePath;
static bool g_isInitialized = false;
static std::mutex g_logMutex;
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

// Gold reward system for OStim scenes
static bool g_inOStimScene = false;
static std::chrono::steady_clock::time_point g_lastGoldRewardTime;
static std::atomic<bool> g_goldRewardActive(false);

// Survival stats restoration system
static float g_lastHungerValue = 0.0f;
static float g_lastColdValue = 0.0f;
static float g_lastExhaustionValue = 0.0f;
static std::chrono::steady_clock::time_point g_lastSurvivalReductionTime;
static std::atomic<bool> g_survivalRestorationActive(false);
static bool g_allStatsAtZero = false;

// Attributes restoration system
static std::chrono::steady_clock::time_point g_lastAttributesRestorationTime;
static std::atomic<bool> g_attributesRestorationActive(false);

// ===== FORWARD DECLARATIONS =====
void StartMonitoringThread();
void StopMonitoringThread();
void WriteToActionsLog(const std::string& message, int lineNumber = 0);
void WriteToAnimationsLog(const std::string& message, int lineNumber = 0);
void CheckAndRewardGold();
void CheckAndRestoreSurvivalStats();
void CheckAndRestoreAttributes();

// ===== UTILITY FUNCTIONS =====
std::string SafeWideStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    try {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed <= 0) {
            size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
            if (size_needed <= 0) return std::string();
            std::string result(size_needed, 0);
            int converted =
                WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
            if (converted <= 0) return std::string();
            return result;
        }
        std::string result(size_needed, 0);
        int converted =
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
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

// ===== TIME FUNCTIONS =====
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

// ===== LOG WRITERS =====
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

    if (g_animationLines.size() > 200) {
        g_animationLines.pop_front();
    }

    std::ofstream animationsFile(logPath, std::ios::trunc);
    if (animationsFile.is_open()) {
        for (const auto& line : g_animationLines) {
            animationsFile << line << std::endl;
        }
        animationsFile.close();
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

    if (g_actionLines.size() > 200) {
        g_actionLines.pop_front();
    }

    std::ofstream actionsFile(logPath, std::ios::trunc);
    if (actionsFile.is_open()) {
        for (const auto& line : g_actionLines) {
            actionsFile << line << std::endl;
        }
        actionsFile.close();
    }
}

// ===== GOLD REWARD SYSTEM =====
void CheckAndRewardGold() {
    if (!g_inOStimScene || g_lastAnimation.empty()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - g_lastGoldRewardTime).count();
    
    // Reward every 7 minutes (420 seconds)
    if (elapsed >= 420) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F); // Gold Form ID
        
        if (player && gold) {
            player->AddObjectToContainer(gold, nullptr, 300, nullptr); // Add 300 gold
            
            // Show notification in top-left corner
            RE::DebugNotification("OSurvival - WOW you have incredible resistance, you have been awarded 300 septim");
            
            logger::info("Rewarded 300 gold to player during OStim scene (Animation: {})", g_lastAnimation);
            WriteToActionsLog("Player received 300 gold (OStim scene: " + g_lastAnimation + ")", __LINE__);
        }
        
        g_lastGoldRewardTime = now;
    }
}

// ===== SURVIVAL STATS RESTORATION SYSTEM =====
void CheckAndRestoreSurvivalStats() {
    if (!g_inOStimScene || g_lastAnimation.empty()) {
        return;
    }

    // Check if we should process (every 60 seconds)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastSurvivalReductionTime).count();
    
    if (elapsed < 60) {
        return; // Not time yet
    }

    // Get global variables
    auto hungerGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_HungerNeedValue");
    auto coldGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ColdNeedValue");
    auto exhaustionGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ExhaustionNeedValue");

    if (!hungerGlobal || !coldGlobal || !exhaustionGlobal) {
        logger::error("Failed to find survival global variables!");
        return;
    }

    // Get current values
    float currentHunger = hungerGlobal->value;
    float currentCold = coldGlobal->value;
    float currentExhaustion = exhaustionGlobal->value;

    // Check if all are already at zero
    if (currentHunger <= 0.0f && currentCold <= 0.0f && currentExhaustion <= 0.0f) {
        if (!g_allStatsAtZero) {
            RE::DebugNotification("OSurvival - You and your partner are fully recovered");
            logger::info("All survival stats reached 0 - restoration complete");
            WriteToActionsLog("All survival stats at 0 - fully recovered", __LINE__);
            g_allStatsAtZero = true;
            g_survivalRestorationActive = false;
        }
        return;
    }

    // Check if any stat is above 100 to start restoration
    if (!g_survivalRestorationActive) {
        if (currentHunger > 100.0f || currentCold > 100.0f || currentExhaustion > 100.0f) {
            g_survivalRestorationActive = true;
            logger::info("Survival restoration activated - at least one stat above 100");
            WriteToActionsLog("Survival restoration system activated", __LINE__);
        } else {
            return; // Don't activate if all stats are below 100
        }
    }

    // Reduce 100 points from each stat (minimum 0)
    float newHunger = std::max(0.0f, currentHunger - 100.0f);
    float newCold = std::max(0.0f, currentCold - 100.0f);
    float newExhaustion = std::max(0.0f, currentExhaustion - 100.0f);

    // Apply new values
    hungerGlobal->value = newHunger;
    coldGlobal->value = newCold;
    exhaustionGlobal->value = newExhaustion;

    // Show notification
    RE::DebugNotification("OSurvival - Restored 100 points of each survival stat");
    
    // Log the changes
    std::stringstream logMsg;
    logMsg << "Survival stats reduced by 100: "
           << "Hunger " << currentHunger << "->" << newHunger << ", "
           << "Cold " << currentCold << "->" << newCold << ", "
           << "Exhaustion " << currentExhaustion << "->" << newExhaustion;
    
    std::string logStr = logMsg.str();
    logger::info("{}", logStr);
    WriteToActionsLog(logStr, __LINE__);

    // Update timer
    g_lastSurvivalReductionTime = now;
}

// ===== ATTRIBUTES RESTORATION SYSTEM =====
void CheckAndRestoreAttributes() {
    if (!g_inOStimScene || g_lastAnimation.empty()) {
        return;
    }

    // Check if we should process (every 2 minutes = 120 seconds)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastAttributesRestorationTime).count();
    
    if (elapsed < 120) {
        return; // Not time yet
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    // Restore 50 points to Health, Magicka, and Stamina
    auto* actorValueOwner = player->AsActorValueOwner();
    if (actorValueOwner) {
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, 50.0f);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, 50.0f);
        actorValueOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, 50.0f);

        // Show notification
        RE::DebugNotification("OSurvival - Small bonus of 50 points in all attributes");

        logger::info("Restored 50 points to Health, Magicka, and Stamina during OStim scene");
        WriteToActionsLog("Player received 50 points in all attributes (Health, Magicka, Stamina)", __LINE__);
    }

    // Update timer
    g_lastAttributesRestorationTime = now;
}

// ===== EVENT PROCESSOR CLASS =====
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

// ===== OSTIM LOG PROCESSING =====
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
                WriteToAnimationsLog("5-second initial delay complete, starting OStim.log monitoring", __LINE__);
            }
        }

        auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) {
            return;
        }

        auto ostimLogPath = *logsFolder / "OStim.log";

        if (!fs::exists(ostimLogPath)) {
            return;
        }

        size_t currentFileSize = fs::file_size(ostimLogPath);

        if (currentFileSize < g_lastFileSize) {
            g_lastOStimLogPosition = 0;
            g_processedLines.clear();
            g_lastAnimation = "";
            logger::info("OStim.log was truncated, resetting position");
            WriteToAnimationsLog("OStim.log reset detected - restarting monitoring", __LINE__);
        } else if (currentFileSize == g_lastFileSize && g_lastOStimLogPosition > 0) {
            return;
        }

        g_lastFileSize = currentFileSize;

        std::ifstream ostimLog(ostimLogPath, std::ios::in);
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
            if (line.find("[warning]") != std::string::npos) {
                continue;
            }

            size_t lineHash = std::hash<std::string>{}(line);
            std::string hashStr = std::to_string(lineHash);

            if (g_processedLines.find(hashStr) != g_processedLines.end()) {
                continue;
            }

            bool isThreadStop = false;

            if (line.find("[Thread.cpp:634] closing thread") != std::string::npos) {
                isThreadStop = true;
                WriteToAnimationsLog("DETECTED: OStim thread closing", __LINE__);
            } else if (line.find("[ThreadManager.cpp:174] trying to stop thread") != std::string::npos) {
                isThreadStop = true;
                WriteToAnimationsLog("DETECTED: OStim trying to stop thread", __LINE__);
            }

            if (isThreadStop) {
                g_processedLines.insert(hashStr);
                
                // Deactivate all systems when exiting OStim scene
                if (g_inOStimScene) {
                    g_inOStimScene = false;
                    g_goldRewardActive = false;
                    g_survivalRestorationActive = false;
                    g_allStatsAtZero = false;
                    g_attributesRestorationActive = false;
                    WriteToActionsLog("OStim scene ended - all reward systems stopped", __LINE__);
                    logger::info("All reward systems deactivated");
                }
                
                WriteToAnimationsLog("OStim scene ended", __LINE__);
                continue;
            }

            bool isAnimationLine = false;
            std::string animationName;

            if (line.find("[info]") != std::string::npos &&
                line.find("[Thread.cpp:195] thread 0 changed to node") != std::string::npos) {
                size_t nodePos = line.find("changed to node ");
                if (nodePos != std::string::npos) {
                    size_t startPos = nodePos + 16;
                    if (startPos < line.length()) {
                        animationName = line.substr(startPos);
                        isAnimationLine = true;
                    }
                }
            } else if (line.find("[info]") != std::string::npos &&
                       line.find("[OStimMenu.h:48] UI_TransitionRequest") != std::string::npos) {
                size_t lastOpenBrace = line.rfind('{');
                size_t lastCloseBrace = line.rfind('}');

                if (lastOpenBrace != std::string::npos && lastCloseBrace != std::string::npos &&
                    lastCloseBrace > lastOpenBrace) {
                    animationName = line.substr(lastOpenBrace + 1, lastCloseBrace - lastOpenBrace - 1);
                    if (!animationName.empty()) {
                        isAnimationLine = true;
                    }
                }
            }

            if (isAnimationLine && !animationName.empty()) {
                animationName.erase(animationName.find_last_not_of(" \n\r\t") + 1);

                g_processedLines.insert(hashStr);

                if (animationName == g_lastAnimation) {
                    continue;
                }

                g_lastAnimation = animationName;

                // Activate all reward systems when entering OStim scene
                if (!g_inOStimScene) {
                    g_inOStimScene = true;
                    g_lastGoldRewardTime = std::chrono::steady_clock::now();
                    g_goldRewardActive = true;
                    
                    // Initialize survival restoration system
                    g_lastSurvivalReductionTime = std::chrono::steady_clock::now();
                    g_survivalRestorationActive = false;
                    g_allStatsAtZero = false;
                    
                    // Initialize attributes restoration system
                    g_lastAttributesRestorationTime = std::chrono::steady_clock::now();
                    g_attributesRestorationActive = true;
                    
                    // Read initial survival values
                    auto hungerGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_HungerNeedValue");
                    auto coldGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ColdNeedValue");
                    auto exhaustionGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Survival_ExhaustionNeedValue");
                    
                    if (hungerGlobal && coldGlobal && exhaustionGlobal) {
                        g_lastHungerValue = hungerGlobal->value;
                        g_lastColdValue = coldGlobal->value;
                        g_lastExhaustionValue = exhaustionGlobal->value;
                        
                        std::stringstream msg;
                        msg << "Initial survival stats - Hunger: " << g_lastHungerValue 
                            << ", Cold: " << g_lastColdValue 
                            << ", Exhaustion: " << g_lastExhaustionValue;
                        
                        std::string msgStr = msg.str();
                        logger::info("{}", msgStr);
                        WriteToActionsLog(msgStr, __LINE__);
                    }
                    
                    WriteToActionsLog("OStim scene started - gold rewards activated (300 gold every 7 minutes)", __LINE__);
                    WriteToActionsLog("OStim scene started - survival restoration system ready (-100 points every 1 minute)", __LINE__);
                    WriteToActionsLog("OStim scene started - attributes restoration activated (50 points every 2 minutes)", __LINE__);
                    logger::info("All reward systems activated for animation: {}", animationName);
                }

                if (g_processedLines.size() > 500) {
                    g_processedLines.clear();
                }

                std::string formattedAnimation = "{" + animationName + "}";
                WriteToAnimationsLog(formattedAnimation, __LINE__);
                
                logger::info("Animation detected: {}", animationName);
            }
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

// ===== MONITORING THREAD =====
void MonitoringThreadFunction() {
    WriteToAnimationsLog("Monitoring thread started - Watching OStim.log for animations", __LINE__);

    auto logsFolder = SKSE::log::log_directory();
    if (logsFolder) {
        auto ostimLogPath = *logsFolder / "OStim.log";
        WriteToAnimationsLog("Monitoring OStim.log at: " + ostimLogPath.string(), __LINE__);
        WriteToAnimationsLog("Waiting 5 seconds before starting OStim.log analysis...", __LINE__);
    }

    g_monitoringStartTime = std::chrono::steady_clock::now();
    g_initialDelayComplete = false;

    while (g_monitoringActive && !g_isShuttingDown.load()) {
        g_monitorCycles++;
        ProcessOStimLog();
        
        // Check if player should receive gold reward
        CheckAndRewardGold();
        
        // Check if player should receive survival stats restoration
        CheckAndRestoreSurvivalStats();
        
        // Check if player should receive attributes restoration
        CheckAndRestoreAttributes();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    logger::info("Monitoring thread stopped");
}

void StartMonitoringThread() {
    if (!g_monitoringActive) {
        g_monitoringActive = true;
        g_monitorCycles = 0;
        g_lastOStimLogPosition = 0;
        g_lastFileSize = 0;
        g_processedLines.clear();
        g_lastAnimation = "";
        g_initialDelayComplete = false;
        g_inOStimScene = false;
        g_goldRewardActive = false;
        g_survivalRestorationActive = false;
        g_allStatsAtZero = false;
        g_attributesRestorationActive = false;
        g_lastHungerValue = 0.0f;
        g_lastColdValue = 0.0f;
        g_lastExhaustionValue = 0.0f;
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

        logger::info("Monitoring system stopped");
    }
}

// ===== PATH FUNCTIONS =====
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

std::string GetGamePath() {
    try {
        std::string mo2Path = GetEnvVar("MO2_MODS_PATH");
        if (!mo2Path.empty()) {
            logger::info("Found game path via MO2_MODS_PATH: {}", mo2Path);
            return mo2Path;
        }

        std::string vortexPath = GetEnvVar("VORTEX_MODS_PATH");
        if (!vortexPath.empty()) {
            logger::info("Found game path via VORTEX_MODS_PATH: {}", vortexPath);
            return vortexPath;
        }

        std::string skyrimMods = GetEnvVar("SKYRIM_MODS_FOLDER");
        if (!skyrimMods.empty()) {
            logger::info("Found game path via SKYRIM_MODS_FOLDER: {}", skyrimMods);
            return skyrimMods;
        }

        std::vector<std::string> registryKeys = {"SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition",
                                                 "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1457087920",
                                                 "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\489830",
                                                 "SOFTWARE\\WOW6432Node\\Valve\\Steam\\Apps\\611670"};

        HKEY hKey;
        char pathBuffer[MAX_PATH] = {0};
        DWORD pathSize = sizeof(pathBuffer);

        for (const auto& key : registryKeys) {
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                if (RegQueryValueExA(hKey, "Installed Path", NULL, NULL, (LPBYTE)pathBuffer, &pathSize) ==
                    ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    std::string result(pathBuffer);
                    if (!result.empty()) {
                        logger::info("Found game path via registry: {}", result);
                        return result;
                    }
                }
                RegCloseKey(hKey);
            }
        }

        std::vector<std::string> commonPaths = {
            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "C:\\Program Files\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "D:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "E:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "F:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\Steam\\steamapps\\common\\Skyrim Special Edition",
            "G:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition"};

        for (const auto& pathCandidate : commonPaths) {
            try {
                if (fs::exists(pathCandidate) && fs::is_directory(pathCandidate)) {
                    logger::info("Found game path via common paths: {}", pathCandidate);
                    return pathCandidate;
                }
            } catch (...) {
                continue;
            }
        }

        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
            fs::path fullPath(exePath);
            std::string gamePath = fullPath.parent_path().string();
            logger::info("Using executable directory as game path (universal fallback): {}", gamePath);
            return gamePath;
        }

        return "";
    } catch (...) {
        return "";
    }
}

// ===== SETUP LOG =====
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

// ===== MAIN INITIALIZATION =====
void InitializePlugin() {
    try {
        g_documentsPath = GetDocumentsPath();
        g_gamePath = GetGamePath();

        if (g_gamePath.empty()) {
            logger::error("Could not determine game path! Plugin may not work correctly.");
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

            auto ostimLogPath = *logsFolder / "OStim.log";
            if (fs::exists(ostimLogPath)) {
                logger::info("OStim.log found at: {}", ostimLogPath.string());
                WriteToAnimationsLog("OStim.log found in SKSE logs folder", __LINE__);

                auto fileSize = fs::file_size(ostimLogPath);
                logger::info("OStim.log size: {} bytes", fileSize);
            } else {
                logger::warn("OStim.log not found at: {}", ostimLogPath.string());
                WriteToAnimationsLog("OStim.log not found - waiting for OStim to create it", __LINE__);
            }
        }

        WriteToAnimationsLog("OSurvival-Mode-NG Plugin - Starting...", __LINE__);
        WriteToAnimationsLog("========================================", __LINE__);
        WriteToAnimationsLog("OSurvival-Mode-NG Plugin - v2.2.0", __LINE__);
        WriteToAnimationsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToAnimationsLog("========================================", __LINE__);
        WriteToAnimationsLog("Documents: " + g_documentsPath, __LINE__);
        WriteToAnimationsLog("Game Path: " + g_gamePath, __LINE__);
        WriteToAnimationsLog("FEATURES: OStim Animation Monitor + Gold + Survival + Attributes Restoration", __LINE__);

        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("OSurvival-Mode-NG Actions Monitor - v2.2.0", __LINE__);
        WriteToActionsLog("Started: " + GetCurrentTimeString(), __LINE__);
        WriteToActionsLog("========================================", __LINE__);
        WriteToActionsLog("Monitoring game events: Menu + Gold + Survival + Attributes", __LINE__);
        WriteToActionsLog("Gold reward system: 300 gold every 7 minutes during OStim scenes", __LINE__);
        WriteToActionsLog("Survival restoration system: -100 points every 1 minute during OStim scenes", __LINE__);
        WriteToActionsLog("Attributes restoration system: 50 points every 2 minutes during OStim scenes", __LINE__);
        WriteToActionsLog("", __LINE__);

        g_isInitialized = true;
        WriteToAnimationsLog("PLUGIN INITIALIZED", __LINE__);

        WriteToAnimationsLog("PLUGIN FULLY ACTIVE", __LINE__);
        WriteToAnimationsLog("========================================", __LINE__);
        WriteToAnimationsLog("Starting OStim animation monitoring...", __LINE__);

        StartMonitoringThread();

    } catch (const std::exception& e) {
        logger::error("CRITICAL ERROR in Initialize: {}", e.what());
    }
}

void ShutdownPlugin() {
    logger::info("PLUGIN SHUTTING DOWN");
    WriteToAnimationsLog("PLUGIN SHUTTING DOWN", __LINE__);
    WriteToActionsLog("PLUGIN SHUTTING DOWN", __LINE__);

    g_isShuttingDown = true;

    StopMonitoringThread();

    WriteToAnimationsLog("========================================", __LINE__);
    WriteToAnimationsLog("Plugin shutdown complete at: " + GetCurrentTimeString(), __LINE__);
    WriteToAnimationsLog("========================================", __LINE__);

    logger::info("Plugin shutdown complete");
}

// ===== MESSAGE LISTENER =====
void MessageListener(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kNewGame:
            logger::info("kNewGame: New game started - resetting system");
            StopMonitoringThread();
            g_lastOStimLogPosition = 0;
            g_lastFileSize = 0;
            g_processedLines.clear();
            g_lastAnimation = "";
            g_initialDelayComplete = false;
            g_inOStimScene = false;
            g_goldRewardActive = false;
            g_survivalRestorationActive = false;
            g_allStatsAtZero = false;
            g_attributesRestorationActive = false;
            InitializePlugin();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            logger::info("kPostLoadGame: Game loaded - checking monitoring");
            if (!g_monitoringActive) {
                StartMonitoringThread();
            }
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("kDataLoaded: Game fully loaded");

            {
                auto& eventProcessor = GameEventProcessor::GetSingleton();

                RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&eventProcessor);

                logger::info("Game event processor registered for menu events");
                WriteToAnimationsLog("Game event processor registered", __LINE__);
                WriteToActionsLog("Event monitoring system active", __LINE__);
            }

            break;

        default:
            break;
    }
}

// ===== SKSE EXPORT FUNCTIONS =====
SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    SetupLog();

    logger::info("OSurvival-Mode-NG Plugin v2.2.0 - Gold + Survival + Attributes Restoration - Starting...");

    InitializePlugin();

    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);

    logger::info("Plugin loaded successfully");

    return true;
}

constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({2, 2, 0});
    v.PluginName("OSurvival-Mode-NG OStim Monitor");
    v.AuthorName("John95AC");
    v.UsesAddressLibrary();
    v.UsesSigScanning();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST, SKSE::RUNTIME_LATEST_VR});

    return v;
}();