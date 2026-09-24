# 🎬 Geometry Dash Auto Clipper

<p align="center">
  <img src="logo.png" width="128" height="128" alt="GD Auto Clipper Logo">
</p>

<p align="center">
  <b>Automatic clipping and recording for Geometry Dash via OBS Studio WebSocket v5.</b><br>
  Never lose a reaction, fail, new personal best, or completion again!
</p>

<p align="center">
  <a href="https://github.com/Ko1ss/Geometry-Dash-Auto-Clipper/releases"><img src="https://img.shields.io/github/v/release/Ko1ss/Geometry-Dash-Auto-Clipper?style=flat-square&color=00ff88" alt="Release"></a>
  <a href="https://geode-sdk.org"><img src="https://img.shields.io/badge/Geode-v5.10.1-blue?style=flat-square" alt="Geode"></a>
  <a href="https://linktr.ee/Ko1ss"><img src="https://img.shields.io/badge/Linktree-Ko1ss-brightgreen?style=flat-square" alt="Linktree"></a>
</p>

---

## ✨ Features

- 🎯 **Smart Worthiness Detection**:
  - **Full Run Personal Bests**: Automatically saves clips when beating your current 0% record.
  - **StartPos Runs & Practice**: Detects new bests from any StartPos or solid practice runs meeting your minimum percentage gain slider.
  - **100% Completions**: Always archives full completions.
  - **Custom Floor Threshold**: Guarantee clips for runs passing a specific percentage (e.g. 50%+).
- 🎙️ **Post-Death Reaction Padding**:
  - Captures 3.5s+ of footage after death so your voice reaction, death sound, and PB text are never cut off.
- ⚡ **Dual Capture Strategies**:
  - **Continuous Session (Recommended)**: Keeps OBS recording continuously during play sessions, cutting worthy clips automatically with zero disk stutter on quick restarts.
  - **Replay Buffer (RAM)**: Works directly with OBS Replay Buffer to save clips with zero disk writes until you hit a record.
- 🛑 **Zero File Bloat**: Throwaway deaths on early obstacles are seamlessly ignored.

---

## 🚀 Quick Start / Setup

### 1. Configure OBS Studio
1. Open **OBS Studio**.
2. Go to **Tools** ➔ **WebSocket Server Settings**.
3. Check **Enable WebSocket server**.
   - **Server Port**: `4455` (default).
   - ⚠️ **Recommendation**: Uncheck **"Enable Authentication"** for instant, frictionless connection! *(If enabled, enter your password into the mod settings in-game).*
4. In OBS **Settings** ➔ **Output** ➔ **Recording**:
   - *(Recommended)* Set **Keyframe Interval** to `1s` or `2s` for instant clip finalization.

### 2. Install Mod
1. Download the latest `.geode` file from [Releases](https://github.com/Ko1ss/Geometry-Dash-Auto-Clipper/releases).
2. Move it to your Geometry Dash `geode/mods` folder.
3. Launch Geometry Dash and play! Your worthy clips will automatically appear in your OBS Recordings folder (`Videos`).

---

## ⚙️ Mod Settings

| Setting | Default | Description |
| :--- | :--- | :--- |
| **OBS WebSocket Port** | `4455` | Port configured in OBS WebSocket server settings |
| **OBS WebSocket Password** | `""` | Password (leave blank if authentication is off) |
| **Recording Strategy** | `Continuous Session` | Continuous Session or Replay Buffer (RAM) |
| **Auto-Save Full Run PBs** | `Enabled` | Automatically saves any attempt beating your 0% record |
| **Minimum Floor %** | `15%` | Floor percentage threshold to always save runs |
| **StartPos Minimum Gain %** | `5%` | Minimum distance gained from StartPos to save |
| **Save StartPos Records** | `Enabled` | Automatically saves new records from specific StartPositions |
| **Post-Death Reaction Padding**| `3.5s` | Seconds to continue recording after death for reactions |

---

## 🔨 Building from Source

1. Install the [Geode CLI](https://docs.geode-sdk.org/getting-started/cli).
2. Clone this repository:
   ```bash
   git clone https://github.com/Ko1ss/Geometry-Dash-Auto-Clipper.git
   cd Geometry-Dash-Auto-Clipper
