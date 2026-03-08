# Bloco Tower — Camera-Based Block Reader

The Tower is a camera-based alternative to the I2C Board reader. It uses an ESP32-S3 with an OV2640 camera to photograph blocks arranged in slots, classifies their icons using an on-device TFLite Micro CNN, and sends the program to the robot via ESP-NOW — the same protocol the Board uses. The robot doesn't know the difference.

## Hardware

- **MCU:** ESP32-S3 with PSRAM (e.g., Freenove ESP32-S3 WROOM CAM)
- **Camera:** OV2640 (320×240 RGB565)
- **LED:** WS2812 on GPIO 48 (status indicator)
- **Button:** BOOT button on GPIO 0 (scan / pair)
- **Illumination:** Configurable GPIO (default 4) for lighting blocks during capture

Camera board selection is done via `idf.py menuconfig` → Tower Configuration.

## How It Works

```
[Blocks in slots] → [Camera captures 320×240 frame]
                          ↓
                   [Crop per slot]
                          ↓
                   [Resize to 48×48 RGB888]
                          ↓
                   [TFLite Micro CNN inference]
                          ↓
                   [block_data_t array]
                          ↓
                   [ESP-NOW → Robot]
```

1. Press BOOT button → camera captures a frame with LED illumination
2. Frame is divided into N horizontal strips (one per slot, configurable 1–16)
3. Each strip is resized to 48×48 and fed through the INT8 quantized CNN
4. Predictions above 50% confidence are mapped to block types
5. Program is sent via ESP-NOW: `MSG_PROGRAM_START` → `MSG_BLOCK_DATA` × N → `MSG_PROGRAM_END`
6. Robot responds with `MSG_PROGRAM_ACK`

## Pairing

Same as the Board — hold BOOT for 4 seconds on both Tower and Robot. Pairing is stored in NVS and persists across reboots.

| LED Color       | Meaning          |
|-----------------|------------------|
| Green (solid)   | Paired           |
| Red (solid)     | Not paired       |
| Blue (blinking) | Pairing mode     |
| White (flash)   | Capturing frame  |

## Build

```bash
source ~/.espressif/v5.5.2/esp-idf/export.sh
cd tower
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Menuconfig Options

```bash
idf.py menuconfig
# Tower Configuration →
#   Camera board (Freenove / ESP32-S3-EYE / Custom)
#   Number of block slots (default 8)
#   LED lighting GPIO (default 4)
```

## ML Model

### Current Model

| Property            | Value                              |
|---------------------|------------------------------------|
| Architecture        | 3-layer CNN (16→32→64 filters)     |
| Input               | 48×48×3 RGB, uint8                 |
| Output              | 4 classes (softmax)                |
| Quantization        | INT8 (full integer)                |
| Size                | 182 KB                             |
| Validation accuracy | 96.2%                              |
| Inference target    | ESP32-S3 via TFLite Micro + ESP-NN |

### Classes

| Class | Direction | Block Type          | Hex    |
|-------|-----------|---------------------|--------|
| 0     | Down      | `BLOCK_BACKWARD`    | `0x11` |
| 1     | Left      | `BLOCK_TURN_LEFT`   | `0x13` |
| 2     | Right     | `BLOCK_TURN_RIGHT`  | `0x12` |
| 3     | Up        | `BLOCK_FORWARD`     | `0x10` |

### Training Dataset

[Kaggle Directions Dataset](https://www.kaggle.com/datasets/jithinnambiarj/directions) (CC0 Public Domain)
- 3,593 images of arrows in varied shapes, colors, and border thickness
- 224×224 JPEG, resized to 48×48 during training
- Split: 80% train / 20% validation

### Retraining

```bash
cd tower/model
source .venv/bin/activate   # or create: python3 -m venv .venv && pip install tensorflow pillow numpy
python train.py
```

This produces:
- `model.keras` — full Keras model
- `model.tflite` — INT8 quantized TFLite model (182 KB)
- `model_data.h` — C array header for firmware

After retraining, copy the header into firmware:
```bash
cp tower/model/model_data.h tower/main/model_data.h
cd tower && idf.py build
```

### Adding New Block Types

1. Add labeled images to `tower/model/data/Direction/<ClassName>/`
2. Update `CLASS_TO_BLOCK_TYPE` in `train.py` to map new class names to block type IDs from `common/include/block_types.h`
3. Rerun `python train.py`
4. Copy `model_data.h` to `tower/main/` and rebuild

## Project Structure

```
tower/
├── CMakeLists.txt          # ESP-IDF project root
├── sdkconfig.defaults      # ESP32-S3 target, PSRAM, custom partitions
├── partitions.csv          # 2MB app partition (fits TFLite model)
├── main/
│   ├── CMakeLists.txt      # Component: main.c, camera.c, classifier.cpp
│   ├── idf_component.yml   # Dependencies: esp32-camera, led_strip, esp-tflite-micro
│   ├── Kconfig.projbuild   # Camera board, slot count, light GPIO
│   ├── main.c              # App entry: init, pairing, capture-classify-send loop
│   ├── camera.h / .c       # OV2640 init, capture, LED lighting
│   ├── classifier.h        # C API: classifier_init(), classifier_identify()
│   ├── classifier.cpp      # TFLite Micro inference, RGB565→RGB888 resize
│   └── model_data.h        # Auto-generated C array of INT8 TFLite model
└── model/
    ├── train.py            # Training script (TensorFlow/Keras)
    ├── model.keras         # Trained Keras model
    ├── model.tflite        # INT8 quantized TFLite model
    ├── model_data.h        # Generated C header (source copy)
    ├── README.md           # ML pipeline notes
    └── data/               # Training dataset (not checked in)
        └── Direction/
            ├── Up/         # 921 images
            ├── Down/       # 830 images
            ├── Left/       # 921 images
            └── Right/      # 921 images
```

## Memory Layout

| Resource         | Location | Size     |
|------------------|----------|----------|
| Firmware binary  | Flash    | 1.1 MB   |
| TFLite model     | Flash    | 182 KB   |
| Tensor arena     | PSRAM    | 64 KB    |
| Camera buffers   | PSRAM    | ~300 KB  |
| Resize buffer    | PSRAM    | 6.75 KB  |

## Dependencies

| Component                    | Version | Purpose              |
|------------------------------|---------|----------------------|
| `espressif/esp32-camera`     | ^2.0.0  | OV2640 camera driver |
| `espressif/led_strip`        | *       | WS2812 LED           |
| `espressif/esp-tflite-micro` | ^1.3.5  | TFLite Micro runtime |
| `common`                     | local   | block_types, espnow_protocol |

---

## Changelog

### [0.1.0] - 2026-03-08

Initial tower firmware with ML-based arrow classification.

**Firmware scaffold:**
- Camera capture with OV2640 at 320×240 RGB565
- Board-specific GPIO pin configs via Kconfig (Freenove, ESP32-S3-EYE, Custom)
- LED illumination control during capture
- ESP-NOW communication — identical protocol to Board reader
- Pairing system with NVS persistence (4s BOOT hold)
- WS2812 LED status: green/red/blue blink/white flash
- Custom partition table (2MB app) to fit TFLite model

**ML classifier:**
- Trained CNN on Kaggle Directions dataset (3,593 arrow images, CC0 license)
- 3-layer CNN: Conv2D(16) → Conv2D(32) → Conv2D(64) → Dense(64) → Dense(4)
- 96.2% validation accuracy on 4-class arrow direction task
- INT8 quantized via TFLite converter (182 KB model)
- Integrated with TFLite Micro + ESP-NN acceleration on ESP32-S3
- RGB565 → RGB888 nearest-neighbor resize to 48×48 model input
- Confidence threshold at 50% — low-confidence predictions return BLOCK_UNKNOWN
- Class mapping: Up→Forward, Down→Backward, Left→Turn Left, Right→Turn Right

**Training pipeline (`tower/model/train.py`):**
- Downloads and trains on Kaggle Directions dataset
- Data augmentation: random flip + rotation
- Exports INT8 TFLite model + C header array for firmware embedding
- Generates `model_data.h` with class-to-block-type lookup table
