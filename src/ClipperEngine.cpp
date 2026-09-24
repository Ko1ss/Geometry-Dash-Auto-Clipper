#include "ClipperEngine.hpp"
#include "OBSManager.hpp"
#include "FFmpegTrimmer.hpp"
#include <filesystem>
#include <cmath>
#include <fmt/format.h>

using namespace geode::prelude;

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
    m_currentLevelId = level->m_levelID.value();
    m_fullRunPB = static_cast<float>(level->m_normalPercent.value());
    m_startPosBest.clear();
    m_isPaused = false;
    m_isFinalizingWorthyClip = false;

    ensureOBSConnected();

    std::string mode = Mod::get()->getSettingValue<std::string>("recording-mode");
    if (mode == "Replay Buffer (RAM)") {
        OBSManager::get().startReplayBuffer();
        log::info("[GD Auto Clipper] Replay Buffer enabled for level: {}", m_currentLevelName);
    } else {
        m_isRecordingActive = true;
        OBSManager::get().startRecord([](bool ok) {
            if (ok) log::info("[GD Auto Clipper] Continuous OBS Recording ACTIVE.");
        });
    }
}

void ClipperEngine::onLevelExited() {
    cancelPendingDelayGuard();
    m_isPaused = false;

    if (m_isFinalizingWorthyClip.load()) {
        return;
    }

    if (m_isRecordingActive.load()) {
        m_isRecordingActive = false;
        OBSManager::get().stopRecord([this](const std::string& path) {
            purgeFile(path, "Level exited");
        });
    }
}

void ClipperEngine::onPlayerPaused() {
    m_isPaused = true;
    m_pauseStartTime = std::chrono::steady_clock::now();
}

void ClipperEngine::onPlayerResumed() {
    m_isPaused = false;
}

void ClipperEngine::onPlayerSpawned(PlayLayer* layer, int runId, float startPercent, bool isStartPos) {
    m_activeRecordingRunId = runId;
    m_activeRunStartTime = std::chrono::steady_clock::now();

    // Auto-recover if recording dropped
    std::string mode = Mod::get()->getSettingValue<std::string>("recording-mode");
    if (mode != "Replay Buffer (RAM)" && !m_isRecordingActive.load() && !m_isFinalizingWorthyClip.load()) {
        m_isRecordingActive = true;
        OBSManager::get().startRecord();
    }
}

void ClipperEngine::scheduleDelayGuard(PlayLayer* layer, int runId, float startPercent, bool isStartPos) {}
void ClipperEngine::cancelPendingDelayGuard() {}

bool ClipperEngine::onRunEnded(PlayLayer* layer, int runId, float startPercent, float endPercent, bool isCompletion, float durationSec, bool isStartPos) {
    // If currently saving a worthy run, ignore throwaway deaths
    if (m_isFinalizingWorthyClip.load()) {
        return false;
    }

    RunMetadata meta;
    meta.runId = runId;
    meta.levelName = m_currentLevelName.empty() ? "Level" : m_currentLevelName;
    meta.levelId = m_currentLevelId;
    meta.startPercent = startPercent;
    meta.endPercent = endPercent;
    meta.previousPB = m_fullRunPB;
    meta.isCompletion = isCompletion;
    meta.isStartPos = isStartPos;
    meta.clipStartTimestamp = 0.0;
    meta.clipDurationSeconds = durationSec;

    std::string reason;
    bool isWorthy = evaluateRunWorthiness(meta, reason);

    if (!isWorthy) {
        return false;
    }

    // Worthy run: update PB immediately so subsequent attempts know the new record!
    if (!isStartPos && endPercent > m_fullRunPB) {
        m_fullRunPB = endPercent;
    }

    m_isFinalizingWorthyClip = true;
    double padTail = Mod::get()->getSettingValue<double>("padding-tail-sec");
    if (padTail < 1.0) padTail = 3.5;

    log::info("[GD Auto Clipper] >>> WORTHY RUN DETECTED ({})! Securing clip with {:.1f}s tail... <<<", reason, padTail);

    std::string mode = Mod::get()->getSettingValue<std::string>("recording-mode");

    if (mode == "Replay Buffer (RAM)") {
        std::thread([this, meta, padTail, reason]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((padTail + 0.5) * 1000.0)));
            OBSManager::get().saveReplayBuffer([this, meta, reason](const std::string& clipPath) {
                m_isFinalizingWorthyClip = false;
                log::info("======================================================");
                log::info("[GD Auto Clipper] CLIP SAVED VIA REPLAY BUFFER: {}", clipPath);
                log::info("[GD Auto Clipper] Reason: {}", reason);
                log::info("======================================================");
                FFmpegTrimmer::get().trimLossless(clipPath, meta, 4.0, 3.5);
            });
        }).detach();
    } else {
        std::thread([this, meta, padTail, reason]() {
            int totalWaitMs = static_cast<int>((padTail + 1.0) * 1000.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(totalWaitMs));

            m_isRecordingActive = false;
            OBSManager::get().stopRecord([this, meta, padTail, reason](const std::string& rawPath) {
                m_isFinalizingWorthyClip = false;

                log::info("======================================================");
                log::info("[GD Auto Clipper] CLIP CAPTURED TO DISK: {}", rawPath);
                log::info("[GD Auto Clipper] Reason: {}", reason);
                log::info("======================================================");

                double padFront = Mod::get()->getSettingValue<double>("padding-front-sec");
                FFmpegTrimmer::get().trimLossless(rawPath, meta, padFront, padTail);

                // Give OBS 500ms to finalize its file handle before restarting
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                m_isRecordingActive = true;
                OBSManager::get().startRecord([](bool ok) {
                    log::info("[GD Auto Clipper] Continuous OBS Recording re-armed for next runs.");
                });
            });

            // Watchdog fallback
            std::this_thread::sleep_for(std::chrono::milliseconds(8000));
            if (m_isFinalizingWorthyClip.load()) {
                m_isFinalizingWorthyClip = false;
                m_isRecordingActive = true;
                OBSManager::get().startRecord();
            }
        }).detach();
    }

    return true;
}

bool ClipperEngine::evaluateRunWorthiness(const RunMetadata& run, std::string& outReason) {
    if (run.isCompletion) {
        outReason = fmt::format("Level Complete! ({:.0f}% -> 100%)", run.startPercent);
        return true;
    }

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
            outReason = fmt::format("New StartPos Record: {:.0f}% -> {:.1f}% (+{:.1f}%, beat {:.1f}%)", 
                                    run.startPercent, run.endPercent, gained, prevBest);
            return true;
        }

        if (gained >= static_cast<float>(minGain)) {
            if (run.endPercent > prevBest) {
                m_startPosBest[startKey] = run.endPercent;
            }
            outReason = fmt::format("Solid StartPos Run: {:.1f}% -> {:.1f}% (+{:.1f}% >= {}% slider)", 
                                    run.startPercent, run.endPercent, gained, minGain);
            return true;
        }

        outReason = fmt::format("StartPos throwaway: +{:.1f}% gained (below {}% slider)", gained, minGain);
        return false;
    }

    // Normal 0% run PB check
    if (run.endPercent > m_fullRunPB && Mod::get()->getSettingValue<bool>("auto-save-pbs")) {
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
        for (int attempt = 0; attempt < 5; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::error_code ec;
            if (std::filesystem::exists(filePath, ec)) {
                if (std::filesystem::remove(filePath, ec)) {
                    log::info("[GD Auto Clipper] Purged throwaway session: {} ({})", filePath, reason);
                    return;
                }
            }
        }
    }).detach();
}