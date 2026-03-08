# Tower ML Model

Block icon classifier for the camera-based tower reader.

## Pipeline

1. **Collect dataset** — Capture labeled images from the tower camera using a data collection script (TODO)
2. **Train model** — Python + TensorFlow/Keras CNN trained on block icon crops (RGB565 → block type)
3. **Export** — Quantize to INT8 TFLite, convert to C header array (`model_data.h`)
4. **Integrate** — Replace stub in `classifier.c` with TFLite Micro inference

## Input format

- RGB565, dimensions match slot crop size (320 × `240/NUM_SLOTS`)
- Model should output one of the block type IDs from `common/include/block_types.h`

## Files (once trained)

- `train.py` — Training script
- `model.tflite` — Quantized TFLite model
- `model_data.h` — C array for embedding in firmware
