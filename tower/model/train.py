#!/usr/bin/env python3
"""
Train a small CNN to classify arrow directions (Up/Down/Left/Right)
for the Bloco Tower camera-based block reader.

Output:
  - model.keras          — full Keras model
  - model.tflite         — INT8 quantized TFLite model
  - model_data.h         — C array header for ESP32-S3 firmware

Usage:
  cd tower/model
  source .venv/bin/activate
  python train.py
"""

import os
import numpy as np
import tensorflow as tf
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

DATA_DIR = Path(__file__).parent / "data" / "Direction"
OUTPUT_DIR = Path(__file__).parent
IMG_SIZE = 48          # Small input for ESP32-S3 inference speed
BATCH_SIZE = 32
EPOCHS = 20
VALIDATION_SPLIT = 0.2

# Arrow direction → block type mapping (from common/include/block_types.h)
CLASS_TO_BLOCK_TYPE = {
    "Up":    0x10,  # BLOCK_FORWARD
    "Down":  0x11,  # BLOCK_BACKWARD
    "Left":  0x13,  # BLOCK_TURN_LEFT
    "Right": 0x12,  # BLOCK_TURN_RIGHT
}

# ---------------------------------------------------------------------------
# Load dataset
# ---------------------------------------------------------------------------

print(f"Loading dataset from {DATA_DIR}")
print(f"Image size: {IMG_SIZE}x{IMG_SIZE}")

train_ds = tf.keras.utils.image_dataset_from_directory(
    DATA_DIR,
    validation_split=VALIDATION_SPLIT,
    subset="training",
    seed=42,
    image_size=(IMG_SIZE, IMG_SIZE),
    batch_size=BATCH_SIZE,
    label_mode="int",
)

val_ds = tf.keras.utils.image_dataset_from_directory(
    DATA_DIR,
    validation_split=VALIDATION_SPLIT,
    subset="validation",
    seed=42,
    image_size=(IMG_SIZE, IMG_SIZE),
    batch_size=BATCH_SIZE,
    label_mode="int",
)

class_names = train_ds.class_names
num_classes = len(class_names)
print(f"Classes: {class_names} ({num_classes})")

# Build class index → block type lookup
class_index_to_block_type = []
for name in class_names:
    block_type = CLASS_TO_BLOCK_TYPE.get(name, 0x00)
    class_index_to_block_type.append(block_type)
    print(f"  {name} (index {class_names.index(name)}) → block type 0x{block_type:02X}")

# Prefetch for performance
AUTOTUNE = tf.data.AUTOTUNE
train_ds = train_ds.cache().shuffle(1000).prefetch(buffer_size=AUTOTUNE)
val_ds = val_ds.cache().prefetch(buffer_size=AUTOTUNE)

# ---------------------------------------------------------------------------
# Build model — small CNN suitable for microcontroller
# ---------------------------------------------------------------------------

model = tf.keras.Sequential([
    # Rescale pixel values to [0, 1]
    tf.keras.layers.Rescaling(1.0 / 255, input_shape=(IMG_SIZE, IMG_SIZE, 3)),

    # Data augmentation
    tf.keras.layers.RandomFlip("horizontal_and_vertical"),
    tf.keras.layers.RandomRotation(0.1),

    # Conv blocks
    tf.keras.layers.Conv2D(16, 3, padding="same", activation="relu"),
    tf.keras.layers.MaxPooling2D(),

    tf.keras.layers.Conv2D(32, 3, padding="same", activation="relu"),
    tf.keras.layers.MaxPooling2D(),

    tf.keras.layers.Conv2D(64, 3, padding="same", activation="relu"),
    tf.keras.layers.MaxPooling2D(),

    # Classifier head
    tf.keras.layers.Flatten(),
    tf.keras.layers.Dropout(0.3),
    tf.keras.layers.Dense(64, activation="relu"),
    tf.keras.layers.Dense(num_classes),
])

model.compile(
    optimizer="adam",
    loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
    metrics=["accuracy"],
)

model.summary()

# ---------------------------------------------------------------------------
# Train
# ---------------------------------------------------------------------------

print(f"\nTraining for {EPOCHS} epochs...")
history = model.fit(train_ds, validation_data=val_ds, epochs=EPOCHS)

# Final accuracy
val_loss, val_acc = model.evaluate(val_ds)
print(f"\nValidation accuracy: {val_acc:.4f}")

# Save Keras model
keras_path = OUTPUT_DIR / "model.keras"
model.save(keras_path)
print(f"Saved Keras model: {keras_path}")

# ---------------------------------------------------------------------------
# Convert to TFLite with INT8 quantization
# ---------------------------------------------------------------------------

print("\nConverting to INT8 TFLite...")

# Representative dataset for quantization calibration
def representative_dataset():
    for images, _ in val_ds.take(10):
        for img in images:
            yield [np.expand_dims(img.numpy(), axis=0).astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.uint8
converter.inference_output_type = tf.int8
tflite_model = converter.convert()

tflite_path = OUTPUT_DIR / "model.tflite"
with open(tflite_path, "wb") as f:
    f.write(tflite_model)
print(f"Saved TFLite model: {tflite_path} ({len(tflite_model)} bytes)")

# ---------------------------------------------------------------------------
# Generate C header for firmware embedding
# ---------------------------------------------------------------------------

header_path = OUTPUT_DIR / "model_data.h"
with open(header_path, "w") as f:
    f.write("// Auto-generated by train.py — do not edit\n")
    f.write("#pragma once\n\n")
    f.write("#include <stdint.h>\n\n")

    # Model data
    f.write(f"// TFLite model ({len(tflite_model)} bytes)\n")
    f.write(f"alignas(16) const unsigned char model_data[] = {{\n")
    for i, byte in enumerate(tflite_model):
        if i % 16 == 0:
            f.write("    ")
        f.write(f"0x{byte:02x},")
        if i % 16 == 15 or i == len(tflite_model) - 1:
            f.write("\n")
        else:
            f.write(" ")
    f.write("};\n")
    f.write(f"const unsigned int model_data_len = {len(tflite_model)};\n\n")

    # Class index → block type mapping
    f.write("// Class index → block_type_t mapping\n")
    f.write(f"// Classes: {class_names}\n")
    f.write(f"const uint8_t class_to_block_type[{num_classes}] = {{\n")
    for i, name in enumerate(class_names):
        bt = class_index_to_block_type[i]
        f.write(f"    0x{bt:02X},  // {i} = {name}\n")
    f.write("};\n\n")

    # Model input config
    f.write(f"#define MODEL_INPUT_SIZE   {IMG_SIZE}\n")
    f.write(f"#define MODEL_NUM_CLASSES  {num_classes}\n")

print(f"Saved C header: {header_path}")

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

print(f"\n{'='*50}")
print(f"Training complete!")
print(f"  Validation accuracy: {val_acc:.1%}")
print(f"  TFLite model size:   {len(tflite_model):,} bytes")
print(f"  Input:               {IMG_SIZE}x{IMG_SIZE} RGB (uint8)")
print(f"  Output:              {num_classes} classes")
print(f"  Class mapping:")
for i, name in enumerate(class_names):
    bt = class_index_to_block_type[i]
    f"    [{i}] {name} → 0x{bt:02X}"
    print(f"    [{i}] {name} → 0x{bt:02X}")
print(f"{'='*50}")
