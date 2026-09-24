#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "ClipperEngine.hpp"

using namespace geode::prelude;

class $modify(AutoClipperPlayLayer, PlayLayer) {
    struct Fields {
        bool m_isRunActive = false;
        bool m_isStartPos = false;
        float m_startPercent = 0.0f;
        float m_highestPercentThisRun = 0.0f;
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
        m_fields->m_highestPercentThisRun = 0.0f;
        
        ClipperEngine::get().onLevelLoaded(this, level);
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (m_fields->m_isRunActive && m_player1) {
            float current = this->getCurrentPercent();
            if (current <= 0.01f && this->m_levelLength > 0.0f) {
                current = (m_player1->getPositionX() / this->m_levelLength) * 100.0f;
            }

            if (current > m_fields->m_highestPercentThisRun) {
                m_fields->m_highestPercentThisRun = current;
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        if (this->m_level && this->m_level->isPlatformer()) {
            return;
        }

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
            startP = (len > 0.0f) ? ((startX / len) * 100.0f) : this->getCurrentPercent();
            m_fields->m_startPercent = startP;
            m_fields->m_highestPercentThisRun = startP;
            log::info("[GD Auto Clipper] Reset from StartPos at {:.2f}%", startP);
        } else {
            startP = this->getCurrentPercent();
            m_fields->m_isStartPos = (startP > 1.0f);
            m_fields->m_startPercent = startP;
            m_fields->m_highestPercentThisRun = startP;
        }

        ClipperEngine::get().onPlayerSpawned(this, currentRunId, m_fields->m_startPercent, m_fields->m_isStartPos);
    }

    void destroyPlayer(PlayerObject* player, GameObject* p1) {
        if (m_fields->m_isRunActive && player == m_player1) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_fields->m_spawnTime
            ).count();

            if (elapsedMs < 100) {
                PlayLayer::destroyPlayer(player, p1);
                return;
            }

            m_fields->m_isRunActive = false;

            float endPercent = m_fields->m_highestPercentThisRun;
            float currentP = this->getCurrentPercent();
            if (currentP > endPercent) endPercent = currentP;

            if (this->m_levelLength > 0.0f && m_player1) {
                float xPercent = (m_player1->getPositionX() / this->m_levelLength) * 100.0f;
                if (xPercent > endPercent) endPercent = xPercent;
            }

            float durationSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_fields->m_spawnTime).count();
            float gained = endPercent - m_fields->m_startPercent;

            log::info("[GD Auto Clipper] Player died at {:.2f}% (attempt started at {:.2f}%, gained: +{:.2f}%)", 
                      endPercent, m_fields->m_startPercent, gained);
            
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
        if (m_fields->m_isRunActive) {
            m_fields->m_isRunActive = false;
            float endPercent = m_fields->m_highestPercentThisRun;
            float durationSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_fields->m_spawnTime).count();
            ClipperEngine::get().onRunEnded(this, m_fields->m_runId, m_fields->m_startPercent, endPercent, false, durationSec, m_fields->m_isStartPos);
        } else {
            ClipperEngine::get().onLevelExited();
        }
        PlayLayer::onQuit();
    }
};

class $modify(AutoClipperPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        
        bool allowPause = Mod::get()->getSettingValue<bool>("allow-pause-recording");
        if (allowPause) {
            ClipperEngine::get().onPlayerPaused();
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