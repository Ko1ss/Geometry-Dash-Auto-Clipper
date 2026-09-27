#pragma once
#include "ClipperEngine.hpp"
#include <Geode/Geode.hpp>
#include <string>
#include <filesystem>
#include <thread>
#include <cstdlib>
#include <sstream>
#include <functional>
#include <algorithm>

using namespace geode::prelude;

class FFmpegTrimmer {
public:
    static FFmpegTrimmer& get() {
        static FFmpegTrimmer instance;
        return instance;
    }

    void trimLossless(const std::string& rawFilePath, const RunMetadata& metadata, double padFront = 4.0, double padTail = 3.0, std::function<void(bool ok, const std::string& trimmedPath)> callback = nullptr) {
        std::thread([rawFilePath, metadata, padFront, padTail, callback, this]() {
            try {
                if (rawFilePath.empty() || !std::filesystem::exists(rawFilePath)) {
                    log::warn("Raw clip does not exist or empty: {}", rawFilePath);
                    if (callback) callback(false, rawFilePath);
                    return;
                }

                std::filesystem::path rawPath(rawFilePath);
                std::filesystem::path parentDir = rawPath.parent_path();
                std::string ext = rawPath.extension().string();
                if (ext.empty()) ext = ".mp4";
                std::string outFilename = generateOutputFilename(metadata, ext);
                std::filesystem::path finalPath = parentDir / outFilename;

                double startTs = std::max(0.0, metadata.clipStartTimestamp - padFront);
                double totalDuration = metadata.clipDurationSeconds + padFront + padTail;

                std::ostringstream ss;
                ss << "ffmpeg -y -ss " << startTs
                   << " -i \"" << rawFilePath << "\""
                   << " -t " << totalDuration
                   << " -avoid_negative_ts make_zero -c copy \"" 
                   << finalPath.string() << "\"";

                std::string cmd = ss.str();
                log::info("Executing FFmpeg trim: {}", cmd);
                int result = std::system(cmd.c_str());

                if (result == 0) {
                    log::info("Clip trimmed successfully: {}", finalPath.string());
                    Loader::get()->queueInMainThread([outFilename]() {
                        Notification::create(
                            "Clip Saved: " + outFilename,
                            NotificationIcon::Success,
                            3.0f
                        )->show();
                    });
                    if (callback) callback(true, finalPath.string());
                } else {
                    log::warn("FFmpeg returned non-zero code: {}. Retaining raw recording.", result);
                    if (callback) callback(false, rawFilePath);
                }
            } catch (const std::exception& e) {
                log::error("Exception in trimLossless: {}", e.what());
                if (callback) callback(false, rawFilePath);
            }
        }).detach();
    }

    void trimAsync(const std::string& rawFilePath, const RunMetadata& metadata) {
        trimLossless(rawFilePath, metadata, 4.0, 3.0, nullptr);
    }

private:
    FFmpegTrimmer() = default;

    std::string generateOutputFilename(const RunMetadata& metadata, const std::string& extension = ".mp4") {
        std::string cleanName = sanitizeFilename(metadata.levelName);
        if (cleanName.empty()) cleanName = "Level";

        std::string ext = extension;
        if (ext.empty() || ext[0] != '.') ext = "." + ext;

        if (metadata.isCompletion) {
            return cleanName + " 100% [Complete]" + ext;
        }

        std::ostringstream ss;
        if (metadata.startPercent > 1.0f) {
            ss << cleanName << " " << static_cast<int>(metadata.startPercent) << "-" << static_cast<int>(metadata.endPercent) << "%" << ext;
        } else {
            ss << cleanName << " " << static_cast<int>(metadata.endPercent) << "%" << ext;
        }
        return ss.str();
    }

    std::string sanitizeFilename(const std::string& name) {
        std::string clean = name;
        for (char& c : clean) {
            if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 32) {
                c = '_';
            }
        }
        return clean;
    }
};
