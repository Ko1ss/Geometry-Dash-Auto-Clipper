#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fmt/format.h>
#include "ClipperEngine.hpp"

using namespace geode::prelude;

class FFmpegTrimmer {
public:
    static FFmpegTrimmer& get() {
        static FFmpegTrimmer instance;
        return instance;
    }

    void trimLossless(const std::string& inputPath, const RunMetadata& meta, double padFrontSec = 4.0, double padTailSec = 3.5) {
        if (inputPath.empty()) return;

        std::thread([inputPath, meta, padFrontSec, padTailSec]() {
            // Give OBS 600ms to release the Windows file handle
            std::this_thread::sleep_for(std::chrono::milliseconds(600));

            std::error_code ec;
            std::filesystem::path src(inputPath);
            if (!std::filesystem::exists(src, ec)) {
                log::warn("[GD Auto Clipper] File not found for renaming: {}", inputPath);
                return;
            }

            // Build clean sanitized filename
            std::string cleanName = sanitizeName(meta.levelName);
            std::string ext = src.extension().string();
            std::string baseFilename;

            if (meta.isCompletion) {
                baseFilename = fmt::format("{} 100% Complete", cleanName);
            } else if (meta.isStartPos) {
                baseFilename = fmt::format("{} {:.0f}-{:.0f}%", cleanName, meta.startPercent, meta.endPercent);
            } else {
                baseFilename = fmt::format("{} {:.0f}%", cleanName, meta.endPercent);
            }

            std::filesystem::path dir = src.parent_path();
            std::filesystem::path dest = dir / (baseFilename + ext);

            // If a file with this name already exists, add (1), (2), etc.
            int counter = 1;
            while (std::filesystem::exists(dest, ec)) {
                dest = dir / fmt::format("{} ({}){}", baseFilename, counter++, ext);
            }

            // Perform direct filesystem rename
            // Because OBS records continuously per session, the full attempt + reaction is already intact.
            std::filesystem::rename(src, dest, ec);

            if (!ec) {
                log::info("======================================================");
                log::info("[GD Auto Clipper] >>> SAVED AND RENAMED CLIP: {} <<<", dest.filename().string());
                log::info("[GD Auto Clipper] Full attempt + reaction intact ({:.1f}s gameplay)", meta.clipDurationSeconds);
                log::info("======================================================");
                
                // Show notification in-game
                std::string toastMsg = fmt::format("Saved: {}", dest.filename().string());
                Loader::get()->queueInMainThread([toastMsg]() {
                    Notification::create(toastMsg, NotificationIcon::Success, 2.5f)->show();
                });
            } else {
                log::warn("[GD Auto Clipper] Rename failed (file in use): {}. Retaining original.", ec.message());
            }
        }).detach();
    }

private:
    FFmpegTrimmer() = default;

    static std::string sanitizeName(const std::string& name) {
        std::string clean;
        for (char c : name) {
            if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
                clean += '_';
            } else {
                clean += c;
            }
        }
        return clean.empty() ? "Level" : clean;
    }
};