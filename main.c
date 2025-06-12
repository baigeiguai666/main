#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_asr.h"
#include "app_ble.h"
#include "audio_recorder.h"
#include <string.h>
#include "chatllm.h"

#define TAG "MAIN"

// 定义聊天消息缓冲区
static ChatMessage messages[2];  // 存储用户输入和AI回复

void app_main()
{
    // 初始化音频采集
    audio_recorder_init();
    
    // 主循环
    while (1) {
        // 检测录音按键
        if (audio_recorder_button_pressed()) {
            // 录制3秒音频
            size_t audio_size;
            uint8_t *audio_data = audio_record(3000, &audio_size);
            
            if (audio_data) {
                // 发送到Paraformer服务进行语音识别
                char *result = asr_recognize(audio_data, audio_size);
                if (result) {
                    ESP_LOGI(TAG, "语音识别结果: %s", result);
                    
                    // 准备发送到 DeepSeek
                    strncpy(messages[0].role, "user", sizeof(messages[0].role));
                    strncpy(messages[0].content, result, sizeof(messages[0].content));
                    
                    // 调用 DeepSeek API
                    char *ai_response = chat_with_deepseek(messages, 1);
                    if (ai_response) {
                        ESP_LOGI(TAG, "AI回复: %s", ai_response);
                        // 发送AI回复到BLE
                        app_ble_send_data(ai_response, strlen(ai_response));
                        free(ai_response);
                    }
                    
                    free(result);
                } else {
                    ESP_LOGE(TAG, "语音识别失败");
                }
                free(audio_data);
            } 
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 添加短暂延时避免过于频繁的检查
    }
}