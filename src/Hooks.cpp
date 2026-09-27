#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "ClipperEngine.hpp"

using namespace geode::prelude;

static float getAccuratePercent(PlayLayer* layer) {
    if (!layer || !layer->m_player1) return 0.0f;

    // 1. Try GD's native percentage
    float p = layer->getCurrentPercent();
    if (p > 0.01f) {
        return p;
    }

    // 2. Physical player position fallback
    float playerX = layer->m_player1->getPositionX();
    float totalLen = layer->m_levelLength;

    if (totalLen > 0.0f && playerX > 0.0f) {
        p = (playerX / totalLen) * 100.0f;
    }

    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;
    return p;
}

class $modify(AutoClipperPlayLayer, PlayLayer) {
    struct Fields {
        bool m_isRunActive = false;
        bool m_isStartPos = false;
        float m_startPercent = 0.0f;
        std::chrono::steady_clock::time_point m_spawnTime;
        int m_runId = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        m_fields->m_runId = 0;
        m_fields->m_isRunActive = false;
        m_fields->m_isStartPos = false;
        m_fields->m_startPercent = 0.0f;
        
        ClipperEngine::get().onLevelLoaded(this, level);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        m_fields->m_runId++;
        int currentRunId = m_fields->m_runId;
        m_fields->m_spawnTime = std::chrono::steady_clock::now();
        m_fields->m_isRunActive = true;

        bool hasStartPos = (this->m_startPosObject != nullptr);
        float startP = 0.0f;

        if (hasStartPos && this->m_startPosObject) {
            m_fields->m_isStartPos = true;
            float startX = this->m_startPosObject->getPositionX();
            float len = this->m_levelLength;
            if (len > 0.0f) {
                startP = (startX / len) * 100.0f;
            } else {
                startP = getAccuratePercent(this);
            }
            m_fields->m_startPercent = startP;
            log::info("[GD Auto Clipper] Reset from StartPos at {:.2f}%", startP);
        } else {
            startP = getAccuratePercent(this);
            m_fields->m_isStartPos = (startP > 1.0f);
            m_fields->m_startPercent = startP;
        }

        ClipperEngine::get().onPlayerSpawned(this, currentRunId, m_fields->m_startPercent, m_fields->m_isStartPos);
    }

    void destroyPlayer(PlayerObject* player, GameObject* p1) {
        if (m_fields->m_isRunActive && player == m_player1) {
            m_fields->m_isRunActive = false;

            float endPercent = this->getCurrentPercent();
            if (endPercent <= 0.01f && this->m_levelLength > 0.0f && m_player1) {
                endPercent = (m_player1->getPositionX() / this->m_levelLength) * 100.0f;
            }

            // StartPos relative displacement guard
            if (std::abs(endPercent - m_fields->m_startPercent) < 0.01f && m_fields->m_isStartPos && this->m_startPosObject && m_player1) {
                float distTraveled = m_player1->getPositionX() - this->m_startPosObject->getPositionX();
                if (distTraveled > 50.0f && this->m_levelLength > 0.0f) {
                    endPercent = (m_player1->getPositionX() / this->m_levelLength) * 100.0f;
                }
            }

            float durationSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_fields->m_spawnTime).count();
            
            log::info("[GD Auto Clipper] Player died at {:.2f}% (attempt started at {:.2f}%, gained: +{:.2f}%)", 
                      endPercent, m_fields->m_startPercent, (endPercent - m_fields->m_startPercent));
            
            ClipperEngine::get().onRunEnded(this, m_fields->m_runId, m_fields->m_startPercent, endPercent, false, durationSec, m_fields->m_isStartPos);
        }

        PlayLayer::destroyPlayer(player, p1);
    }

    void levelComplete() {
        if (m_fields->m_isRunActive) {
            m_fields->m_isRunActive = false;
            float durationSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_fields->m_spawnTime).count();
            ClipperEngine::get().onRunEnded(this, m_fields->m_runId, m_fields->m_startPercent, 100.0f, true, durationSec, m_fields->m_isStartPos);
        }

        PlayLayer::levelComplete();
    }

    void onQuit() {
        ClipperEngine::get().cancelPendingDelayGuard();
        ClipperEngine::get().onLevelExited();
        PlayLayer::onQuit();
    }
};

class $modify(AutoClipperPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        
        bool allowPause = Mod::get()->getSettingValue<bool>("allow-pause-recording");
        if (allowPause) {
            log::info("[GD Auto Clipper] Player paused game; recording kept active for timing/nerve control.");
            ClipperEngine::get().onPlayerPaused();
        } else {
            log::info("[GD Auto Clipper] Player paused game; pause recording is disabled, stopping clip.");
            ClipperEngine::get().onLevelExited();
        }
    }

    void onResume(CCObject* sender) {
        ClipperEngine::get().onPlayerResumed();
        PauseLayer::onResume(sender);
    }

    void onQuit(CCObject* sender) {
        ClipperEngine::get().onLevelExited();
        PauseLayer::onQuit(sender);
    }

    void onRestart(CCObject* sender) {
        ClipperEngine::get().onLevelExited();
        PauseLayer::onRestart(sender);
    }
};

class $modify(AutoClipperMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        ClipperEngine::get().ensureOBSConnected();
        return true;
    }
};
