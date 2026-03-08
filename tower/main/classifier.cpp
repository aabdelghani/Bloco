#include "classifier.h"
#include "model_data.h"

#include <cstring>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "classifier";

// Tensor arena — allocated in PSRAM for the ESP32-S3
static constexpr int kTensorArenaSize = 64 * 1024;
static uint8_t *tensor_arena = nullptr;

static const tflite::Model *model = nullptr;
static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input_tensor = nullptr;

// Scratch buffer for resized RGB888 image
static uint8_t *resize_buf = nullptr;

// ---------------------------------------------------------------------------
// Nearest-neighbor resize RGB565 → RGB888 at MODEL_INPUT_SIZE × MODEL_INPUT_SIZE
// ---------------------------------------------------------------------------
static void resize_rgb565_to_rgb888(const uint8_t *src, int src_w, int src_h,
                                     uint8_t *dst, int dst_w, int dst_h)
{
    for (int dy = 0; dy < dst_h; dy++) {
        int sy = dy * src_h / dst_h;
        if (sy >= src_h) sy = src_h - 1;
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = dx * src_w / dst_w;
            if (sx >= src_w) sx = src_w - 1;

            // RGB565 little-endian: [low_byte, high_byte]
            int src_idx = (sy * src_w + sx) * 2;
            uint8_t lo = src[src_idx];
            uint8_t hi = src[src_idx + 1];
            uint16_t pixel = (hi << 8) | lo;

            uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            uint8_t b = (pixel & 0x1F) << 3;

            int dst_idx = (dy * dst_w + dx) * 3;
            dst[dst_idx + 0] = r;
            dst[dst_idx + 1] = g;
            dst[dst_idx + 2] = b;
        }
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
extern "C" esp_err_t classifier_init(void)
{
    model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version %lu != expected %d",
                 (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return ESP_FAIL;
    }

    // Register only the ops our CNN uses
    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddFullyConnected();
    resolver.AddReshape();
    resolver.AddSoftmax();
    resolver.AddQuantize();

    // Allocate tensor arena in PSRAM
    tensor_arena = (uint8_t *)heap_caps_malloc(kTensorArenaSize,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tensor_arena) {
        ESP_LOGE(TAG, "Failed to allocate tensor arena in PSRAM");
        return ESP_ERR_NO_MEM;
    }

    // Create interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed");
        return ESP_FAIL;
    }

    input_tensor = interpreter->input(0);
    ESP_LOGI(TAG, "Model loaded: input [%d,%d,%d,%d] type=%d",
             input_tensor->dims->data[0], input_tensor->dims->data[1],
             input_tensor->dims->data[2], input_tensor->dims->data[3],
             input_tensor->type);

    // Allocate resize buffer
    resize_buf = (uint8_t *)heap_caps_malloc(
        MODEL_INPUT_SIZE * MODEL_INPUT_SIZE * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resize_buf) {
        ESP_LOGE(TAG, "Failed to allocate resize buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Classifier ready (%d classes, %dx%d input)",
             MODEL_NUM_CLASSES, MODEL_INPUT_SIZE, MODEL_INPUT_SIZE);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Identify
// ---------------------------------------------------------------------------
extern "C" uint8_t classifier_identify(const uint8_t *img, int w, int h)
{
    if (!interpreter || !input_tensor || !resize_buf) {
        ESP_LOGW(TAG, "Classifier not initialized");
        return BLOCK_UNKNOWN;
    }

    // Resize RGB565 crop → RGB888 at model input size
    resize_rgb565_to_rgb888(img, w, h, resize_buf, MODEL_INPUT_SIZE, MODEL_INPUT_SIZE);

    // Feed into model input tensor
    int input_size = MODEL_INPUT_SIZE * MODEL_INPUT_SIZE * 3;
    if (input_tensor->type == kTfLiteUInt8) {
        memcpy(input_tensor->data.uint8, resize_buf, input_size);
    } else if (input_tensor->type == kTfLiteInt8) {
        for (int i = 0; i < input_size; i++) {
            input_tensor->data.int8[i] = (int8_t)(resize_buf[i] ^ 0x80);
        }
    } else {
        ESP_LOGE(TAG, "Unexpected input type: %d", input_tensor->type);
        return BLOCK_UNKNOWN;
    }

    // Run inference
    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke() failed");
        return BLOCK_UNKNOWN;
    }

    // Read output — find best class
    TfLiteTensor *output = interpreter->output(0);
    int best_idx = 0;
    int8_t best_score = -128;

    for (int i = 0; i < MODEL_NUM_CLASSES; i++) {
        int8_t score = output->data.int8[i];
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    // Convert to approximate confidence
    float confidence = (best_score - output->params.zero_point) * output->params.scale;

    uint8_t block_type = class_to_block_type[best_idx];
    ESP_LOGI(TAG, "Result: class=%d conf=%.2f → block 0x%02X", best_idx, confidence, block_type);

    if (confidence < 0.5f) {
        ESP_LOGW(TAG, "Low confidence (%.2f) — returning UNKNOWN", confidence);
        return BLOCK_UNKNOWN;
    }

    return block_type;
}
