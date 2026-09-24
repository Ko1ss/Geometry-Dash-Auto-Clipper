#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <functional>

using namespace geode::prelude;

struct RunMetadata {
    int runId = 0;
    std::string levelName = "Level";
    int levelId = 0;
    float startPercent = 0.0f;
    float endPercent = 0.0f;
    float previousPB = 0.0f;
    bool isCompletion = false;
    bool isStartPos = false;
    double clipStartTimestamp = 0.0;
    double clipDurationSeconds = 30.0;
};

class ClipperEngine {
public:
    static ClipperEngine& get();

    void ensureOBSConnected();
    void onLevelLoaded(PlayLayer* layer, GJGameLevel* level);
    void onLevelExited();
    
    void onPlayerSpawned(PlayLayer* layer, int runId, float startPercent, bool isStartPos);
    void scheduleDelayGuard(PlayLayer* layer, int runId, float startPercent, bool isStartPos);
    void cancelPendingDelayGuard();

    void onPlayerPaused();
    void onPlayerResumed();

    // Returns true if run was deemed worthy
    bool onRunEnded(PlayLayer* layer, int runId, float startPercent, float endPercent, bool isCompletion, float durationSec, bool isStartPos);

private:
    ClipperEngine();
    bool evaluateRunWorthiness(const RunMetadata& run, std::string& outReason);
    void purgeFile(const std::string& filePath, const std::string& reason);

    std::string m_currentLevelName;
    int m_currentLevelId = 0;
    std::atomic<int> m_activeRecordingRunId{-1};
    std::atomic<bool> m_isRecordingActive{false};
    std::atomic<bool> m_isStoppingRecord{false};
    std::atomic<bool> m_isFinalizingWorthyClip{false};
    std::atomic<bool> m_pendingStartOnStop{false};

    bool m_isPaused = false;
    std::chrono::steady_clock::time_point m_pauseStartTime;

    std::unordered_map<int, float> m_startPosBest;
    float m_fullRunPB = 0.0f;

    std::chrono::steady_clock::time_point m_activeRunStartTime;
    std::atomic<int> m_pendingDelayGuardRunId{-1};
};