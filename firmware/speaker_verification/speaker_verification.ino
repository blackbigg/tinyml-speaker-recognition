/**
 * ==============================================================================
 * TinyML On-Device Speaker Verification & Acoustic Control System
 * ==============================================================================
 * Target Hardware : Arduino Nano 33 BLE (Nordic nRF52840, Arm Cortex-M4F @ 64MHz)
 * Audio Sensor    : MP34DT05 PDM Digital Microphone (16 kHz / 16-bit Mono)
 * Framework       : Edge Impulse C++ SDK & CMSIS-NN (INT8 Quantized 1D-CNN)
 * Author          : Cheng-Feng Liu (劉誠豐), National Taipei University of Technology
 * License         : MIT License
 * ==============================================================================
 */

#include <PDM.h>
/* 修正為您實際的標頭檔名稱 */
#include <Edge_Computing_project_inferencing.h> 

// -------------------- 系統設定 --------------------
#define EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW 4
const int LED_PIN = LED_BUILTIN;      
const float CONFIDENCE_THRESHOLD = 0.55; // 信心度門檻
bool led_on = false;                  

// -------------------- VAD (音量過濾) 設定 --------------------
float VAD_THRESHOLD = 0.02;           
const int CALIBRATION_SLICES = 8;     
const float VAD_MARGIN = 0.01;        

// -------------------- 音訊緩衝區 --------------------
typedef struct {
    signed short *buffers[2];
    unsigned char buf_select;
    unsigned char buf_ready;
    unsigned int buf_count;
    unsigned int n_samples;
} inference_t;

static inference_t inference;
static bool record_ready = false;
static signed short *sampleBuffer;
static bool debug_nn = false;
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);

// -------------------- Setup 階段 --------------------
void setup() {
    Serial.begin(115200);
    while (!Serial); 
    Serial.println("Speaker Verification System starting...");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    run_classifier_init(); 
    if (!microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE)) {
        Serial.println("ERR: Failed to setup audio buffer");
        return;
    }

    // 啟動等待，確保麥克風穩定
    delay(2000); 
    VAD_THRESHOLD = calibrate_rms_background();
    //VAD_THRESHOLD = 0.02;
    Serial.println("\n--- System Ready: Start Commanding ---\n");
}

// -------------------- Main Loop --------------------
void loop() {
    if (!microphone_inference_record()) return;

    // 1. VAD 過濾：低於門檻則不處理，節省運算資源
    float rms = calculate_rms(&inference.buffers[inference.buf_select ^ 1][0], EI_CLASSIFIER_SLICE_SIZE);
    if (rms < VAD_THRESHOLD) return; 

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    // 2. 執行神經網路推論
    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) return;

    // 3. 輸出與邏輯判定 (僅在一個完整的模型視窗結束時輸出)
    if (++print_results >= EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW) {
        
        Serial.println("----------------------------------------"); // 增加分割線
        Serial.print("Analysis Result (DSP: "); Serial.print(result.timing.dsp);
        Serial.print("ms, Class: "); Serial.print(result.timing.classification);
        Serial.println("ms)");

        float p_go_me = 0, p_stop_me = 0, p_go_others = 0, p_stop_others = 0;

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            const char *label = result.classification[ix].label;
            float value = result.classification[ix].value;

            // 儲存關鍵分數
            if (strcmp(label, "go_me") == 0) p_go_me = value;
            else if (strcmp(label, "stop_me") == 0) p_stop_me = value;
            else if (strcmp(label, "go_others") == 0) p_go_others = value;
            else if (strcmp(label, "stop_others") == 0) p_stop_others = value;
            
            // 格式化輸出分數
            Serial.print("  ["); Serial.print(label); Serial.print("]: ");
            Serial.println(value, 4);
        }

        // 4. 聲紋驗證決策
        if (p_go_me >= CONFIDENCE_THRESHOLD) {
            Serial.println(">> ACTION: OWNER_GO detected -> LED ON");
            led_on = true;
        } 
        else if (p_stop_me >= CONFIDENCE_THRESHOLD) {
            Serial.println(">> ACTION: OWNER_STOP detected -> LED OFF");
            led_on = false;
        } 
        else if (p_go_others >= CONFIDENCE_THRESHOLD || p_stop_others >= CONFIDENCE_THRESHOLD) {
            Serial.println(">> REJECT: Unauthorized person detected.");
        }
        else {
            Serial.println(">> STATUS: No valid command.");
        }

        digitalWrite(LED_PIN, led_on ? HIGH : LOW); 
        Serial.println("----------------------------------------");
        print_results = 0;
    }
}

/**
 * 輔助函式：RMS 計算與校準
 */
float calculate_rms(const signed short *data, size_t length) {
    double sum_sq = 0.0;
    for (size_t i = 0; i < length; i++) {
        float f = data[i] / 32768.0f;
        sum_sq += f * f;
    }
    return sqrt(sum_sq / (float)length);
}

float calibrate_rms_background() {
    float sum_rms = 0;
    Serial.println("Calibrating... Please be silent.");
    
    for (int i = 0; i < CALIBRATION_SLICES; i++) {
        // 加入超時判斷，避免永遠卡死
        uint32_t start_ms = millis();
        while (!inference.buf_ready) {
            if (millis() - start_ms > 2000) {
                Serial.println("Error: Microphone timeout!");
                return 0.05; // 回傳一個預設值，不要讓系統卡死
            }
            delay(1);
        }
        
        float rms = calculate_rms(&inference.buffers[inference.buf_select ^ 1][0], EI_CLASSIFIER_SLICE_SIZE);
        Serial.print("Sample "); Serial.print(i); Serial.print(" RMS: "); Serial.println(rms, 4);
        sum_rms += rms;
        inference.buf_ready = 0; // 手動重置狀態
    }
    return (sum_rms / CALIBRATION_SLICES) + VAD_MARGIN;
}
/**
 * PDM 底層音訊處理
 */
static void pdm_data_ready_inference_callback(void) {
    int bytesAvailable = PDM.available();
    int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);
    if (record_ready) {
        for (int i = 0; i < bytesRead >> 1; i++) {
            inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];
            if (inference.buf_count >= inference.n_samples) {
                inference.buf_select ^= 1;
                inference.buf_count = 0;
                inference.buf_ready = 1;
            }
        }
    }
}

static bool microphone_inference_start(uint32_t n_samples) {
    inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));
    inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));
    sampleBuffer = (signed short *)malloc((n_samples >> 1) * sizeof(signed short));
    if (!inference.buffers[0] || !inference.buffers[1] || !sampleBuffer) return false;

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;

    PDM.onReceive(&pdm_data_ready_inference_callback);
    PDM.setBufferSize((n_samples >> 1) * sizeof(int16_t));
    if (!PDM.begin(1, EI_CLASSIFIER_FREQUENCY)) return false;
    PDM.setGain(127);
    record_ready = true;
    return true;
}

static bool microphone_inference_record(void) {
    if (inference.buf_ready == 1) return false;
    while (inference.buf_ready == 0) delay(1);
    inference.buf_ready = 0;
    return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}