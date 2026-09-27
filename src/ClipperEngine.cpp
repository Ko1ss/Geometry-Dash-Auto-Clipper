#include "ClipperEngine.hpp"
#include "OBSManager.hpp"
#include "FFmpegTrimmer.hpp"
#include <filesystem>
#include <cmath>

ClipperEngine& ClipperEngine::get() {
    static ClipperEngine instance;
    return instance;
}

ClipperEngine::ClipperEngine() = default;

void ClipperEngine::ensureOBSConnected() {
    if (!OBSManager::get().isConnected()) {
        int64_t port = Mod::get()->getSettingValue<int64_t>("obs-port");
        std::string password = Mod::get()->getSettingValue<std::string>("obs-password");
        OBSManager::get().init("127.0.0.1", static_cast<int>(port), password);
    }
}

void ClipperEngine::onLevelLoaded(PlayLayer* layer, GJGameLevel* level) {
    if (!level) return;
    m_currentLevelName = level->m_levelName;
    m_currentLevelId = level->m_levelID;
    m_fullRunPB = static_cast<float>(level->m_normalPercent);
    m_startPosBest.clear();
    ensureOBSConnected();
}

void ClipperEngine::onLevelExited() {
    cancelPendingDelayGuard();
    if (m_isRecordingActive) {
        OBSManager::get().stopRecord([this](const std::string& path) {
            purgeFile(path, "Level exited while recording");
        });
        m_isRecordingActive = false;
        m_activeRecordingRunId = -1;
    }
}

void ClipperEngine::onPlayerPaused() {
    m_isPaused = true;
    m_pauseStartTime = std::chrono::steady_clock::now();
}

void ClipperEngine::onPlayerResumed() {
    if (m_isPaused) {
        m_isPaused = false;
        auto pauseDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_pauseStartTime
        ).count();

        int maxPause = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-pause-duration-sec"));
        
        if (maxPause > 0 && pauseDuration > maxPause) {
            log::warn("[GD Auto Clipper] Player was paused for {}s (exceeds {}s limit). Aborting recording.", 
                      pauseDuration, maxPause);
            onLevelExited();
        } else {
            log::info("[GD Auto Clipper] Player resumed after {}s pause. Recording continues!", pauseDuration);
        }
    }
}

void ClipperEngine::onPlayerSpawned(PlayLayer* layer, int runId, float startPercent, bool isStartPos) {
    bool alwaysRecord = Mod::get()->getSettingValue<bool>("always-record-and-purge");

    if (alwaysRecord) {
        m_activeRecordingRunId = runId;
        m_activeRunStartTime = std::chrono::steady_clock::now();
        m_isRecordingActive = true;

        OBSManager::get().startRecord([runId](bool ok) {
            if (ok) {
                log::info("[GD Auto Clipper] Instant Record Started for attempt #{}", runId);
            }
        });
    } else {
        scheduleDelayGuard(layer, runId, startPercent, isStartPos);
    }
}

void ClipperEngine::scheduleDelayGuard(PlayLayer* layer, int runId, float startPercent, bool isStartPos) {
    m_pendingDelayGuardRunId = runId;
    double delaySec = Mod::get()->getSettingValue<double>("delay-guard-sec");

    std::thread([this, runId, delaySec]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delaySec * 1000.0)));
        Loader::get()->queueInMainThread([this, runId]() {
            if (m_pendingDelayGuardRunId == runId && !m_isRecordingActive) {
                m_activeRecordingRunId = runId;
                m_activeRunStartTime = std::chrono::steady_clock::now();
                m_isRecordingActive = true;
                OBSManager::get().startRecord();
            }
        });
    }).detach();
}

void ClipperEngine::cancelPendingDelayGuard() {
    m_pendingDelayGuardRunId = -1;
}

void ClipperEngine::onRunEnded(PlayLayer* layer, int runId, float startPercent, float endPercent, bool isCompletion, float durationSec, bool isStartPos) {
    cancelPendingDelayGuard();

    RunMetadata meta;
    meta.runId = runId;
    meta.levelName = m_currentLevelName.empty() ? "Level" : m_currentLevelName;
    meta.levelId = m_currentLevelId;
    meta.startPercent = startPercent;
    meta.endPercent = endPercent;
    meta.isCompletion = isCompletion;
    meta.isStartPos = isStartPos;
    meta.clipStartTimestamp = 0.0;
    meta.clipDurationSeconds = durationSec;

    std::string reason;
    bool isWorthy = evaluateRunWorthiness(meta, reason);

    bool alwaysRecord = Mod::get()->getSettingValue<bool>("always-record-and-purge");

    if (alwaysRecord || m_isRecordingActive) {
        m_isRecordingActive = false;
        m_activeRecordingRunId = -1;

        OBSManager::get().stopRecord([this, isWorthy, meta, reason](const std::string& rawPath) {
            if (!isWorthy) {
                purgeFile(rawPath, reason);
            } else {
                log::info("[GD Auto Clipper] Keeping clip: {} ({})", rawPath, reason);
                double padFront = Mod::get()->getSettingValue<double>("padding-front-sec");
                double padTail = Mod::get()->getSettingValue<double>("padding-tail-sec");
                FFmpegTrimmer::get().trimLossless(rawPath, meta, padFront, padTail);
            }
        });
        return;
    }

    std::string strategy = Mod::get()->getSettingValue<std::string>("recording-mode");
    if (strategy.find("Replay Buffer") != std::string::npos && isWorthy) {
        log::info("[GD Auto Clipper] Worthy run via Replay Buffer: {}", reason);
        OBSManager::get().saveReplayBuffer([meta](const std::string& savedClipPath) {
            FFmpegTrimmer::get().trimLossless(savedClipPath, meta, 4.0, 3.0);
        });
    } else if (!isWorthy) {
        log::info("[GD Auto Clipper] Run discarded: {}", reason);
    }
}

bool ClipperEngine::evaluateRunWorthiness(const RunMetadata& run, std::string& outReason) {
    // 1. Level Completion (100%)
    if (run.isCompletion) {
        outReason = fmt::format("Level Complete! ({:.0f}% -> 100%)", run.startPercent);
        return true;
    }

    // 2. StartPos Runs
    if (run.isStartPos) {
        float gained = run.endPercent - run.startPercent;

        if (run.endPercent >= 100.0f) {
            outReason = fmt::format("StartPos run to 100%! ({:.1f}% -> 100%)", run.startPercent);
            return true;
        }

        int minGain = static_cast<int>(Mod::get()->getSettingValue<int64_t>("startpos-min-gain"));
        bool savePBs = Mod::get()->getSettingValue<bool>("startpos-save-pbs");

        int startKey = static_cast<int>(std::round(run.startPercent));
        float prevBest = m_startPosBest[startKey];

        if (savePBs && run.endPercent > prevBest && gained >= 2.0f) {
            m_startPosBest[startKey] = run.endPercent;
            outReason = fmt::format("New StartPos Best: {:.1f}% -> {:.1f}% (+{:.1f}%, beat {:.1f}%)", 
                                    run.startPercent, run.endPercent, gained, prevBest);
            return true;
        }

        if (gained >= static_cast<float>(minGain)) {
            outReason = fmt::format("Solid StartPos Run: {:.1f}% -> {:.1f}% (+{:.1f}% >= {}% slider)", 
                                    run.startPercent, run.endPercent, gained, minGain);
            return true;
        }

        outReason = fmt::format("StartPos throwaway: +{:.1f}% gained (below {}% slider)", gained, minGain);
        return false;
    }

    // 3. Regular 0% Runs
    if (run.endPercent > m_fullRunPB && Mod::get()->getSettingValue<bool>("auto-save-pbs")) {
        m_fullRunPB = run.endPercent;
        outReason = fmt::format("New Personal Best: {:.1f}%", run.endPercent);
        return true;
    }

    int minFloor = static_cast<int>(Mod::get()->getSettingValue<int64_t>("min-floor-percent"));
    if (run.endPercent >= static_cast<float>(minFloor)) {
        outReason = fmt::format("Passed Floor: {:.1f}% >= {}% slider", run.endPercent, minFloor);
        return true;
    }

    outReason = fmt::format("Died below floor: {:.1f}% < {}% slider", run.endPercent, minFloor);
    return false;
}

void ClipperEngine::purgeFile(const std::string& filePath, const std::string& reason) {
    if (filePath.empty()) return;
    std::thread([filePath, reason]() {
        // Retry loop to handle Windows OBS file locks
        for (int attempt = 0; attempt < 6; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::error_code ec;
            if (std::filesystem::exists(filePath, ec)) {
                if (std::filesystem::remove(filePath, ec)) {
                    log::info("[GD Auto Clipper] Purged throwaway clip: {} ({})", filePath, reason);
                    return;
                }
            }
        }
    }).detach();
}
