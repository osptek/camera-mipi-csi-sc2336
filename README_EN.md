# SC2336 MIPI CSI camera module — documentation & samples

**简体中文：** [`README.md`](README.md)

---

> This repository focuses on the **SC2336 camera module**, with an ESP-IDF sample. The demo captures MIPI CSI video on ESP32-P4 and previews on a **3.95″ 480×480 MIPI display** (reference setup; the repo name reflects the camera, not the panel).

## Product overview

| Item | Description |
|:--|:--|
| Product type | **MIPI CSI camera module** |
| Sensor | **SC2336** |
| Interface | **MIPI CSI** (SCCB/I2C for configuration) |
| Spec ID | **`camera-mipi-csi-sc2336`** is the common product designation in documentation |
| Output modes | Multiple resolutions/frame rates supported (e.g. 1280×720, 1920×1080, 800×800; see menuconfig / sensor settings) |

---

## Repository layout

### Top-level

| Path | Contents |
|:--|:--|
| `assets/` | Demo images / videos (when available) |
| `docs/` | Sensor datasheets and module notes (**to be added**) |
| `examples/` | **Sample projects** |

### `examples/` layout

| Location | Description |
|:--|:--|
| `examples/` root | ESP32-P4: SC2336 MIPI CSI capture + 3.95″ 480×480 MIPI preview |

### Sample project paths

| Description | Path |
|:--|:--|
| SC2336 MIPI CSI preview demo | `examples/esp32p4c6-395mipi-480x480-camera_sc2336-demo/` |
