#include <Geode/Geode.hpp>
#include "OBSManager.hpp"
#include "ClipperEngine.hpp"

using namespace geode::prelude;

void initOBSManager() {
    auto port = Mod::get()->getSavedValue<int64_t>("obs-port", 4455);
    auto password = Mod::get()->getSavedValue<std::string>("obs-password", "");
    
    OBSManager::get().init("127.0.0.1", static_cast<int>(port), password);
    log::info("OBS Manager initialized with port: {}", port);
}

// Initialize mod on boot
$on_mod(Loaded) {
    log::info("GD Auto Clipper loaded!");
    initOBSManager();
}
