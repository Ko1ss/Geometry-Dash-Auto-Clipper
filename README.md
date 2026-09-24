# GD Auto Clipper

PLEASE NOTE THIS IS THE INITAL RELEASE BUGS MAY EXIST PLEASE REPORT TO @Ko1ss ON DISCORD! 

**GD Auto Clipper** is an automated clipping and recording tool for Geometry Dash that integrates directly with **OBS Studio** via WebSocket v5.

Never lose a reaction, fail, or new personal best again! The mod runs quietly in the background and automatically decides when a run is worth saving.

---

### ✨ Features

- **🎯 Smart Worthiness Evaluation:**
  - **New Personal Bests:** Automatically detects and saves runs that beat your current normal mode record from 0%.
  - **StartPos Records & Practice Grinds:** Automatically saves runs that beat your previous record from that StartPos, or any run gaining more than your set minimum threshold (e.g., +5%).
  - **100% Completions:** Always captures full level completions!
  - **Floor Percentage:** Set a threshold (e.g. 50%) to save any attempt that reaches the drop or late-game sections.
- **🎙️ Full Reaction Capture:**
  - Configurable post-death reaction buffer (default: 3.5s) to ensure your death sound, explosion, voice reaction, and audio fadeout are never cut off.
- **⚡ Dual Capture Strategies:**
  - **Continuous Session:** Keeps OBS recording continuously during play sessions, cutting worthy clips automatically with zero disk stutter on quick restarts.
  - **Replay Buffer (RAM):** Works with OBS Replay Buffer to save clips with zero disk writes until you hit a record!
- **🛑 Zero File Bloat:** Throwaway deaths on spikes 1% in are seamlessly ignored.

---

### 🚀 Setup Instructions

1. Open **OBS Studio**.
2. Go to **Tools** ➔ **WebSocket Server Settings**.
3. Check **Enable WebSocket server**.
   - Server Port: `4455` (default).
   - ⚠️ **Recommendation:** Uncheck **"Enable Authentication"** (remove password) for the best and smoothest experience with instant auto-connect!
   - *(If you choose to keep authentication enabled, make sure to enter the exact password in the GD Auto Clipper mod settings).*
4. In OBS **Settings** ➔ **Output** ➔ **Recording**:
   - Recommended: Set **Keyframe Interval** to `1s` or `2s` for instant clip finalization.
5. Launch Geometry Dash and play! Your worthy clips will automatically appear in your OBS Recordings folder (`Videos`).
