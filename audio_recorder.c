#define CONFIG_LOG_MAXIMUM_LEVEL ESP_LOG_INFO
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#define TAG "AUDIO_REC"

static i2s_chan_handle_t i2s_handle;

#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT
#define BUFFER_SIZE 2048

// 引脚定义
#define MIC_SD  GPIO_NUM_17
#define MIC_SCK GPIO_NUM_16
#define MIC_WS  GPIO_NUM_15
#define BUTTON_PIN GPIO_NUM_4

void audio_recorder_init(void)
{
    // 配置I2S通道
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = BUFFER_SIZE / 4,
        .auto_clear = false
    };
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &i2s_handle));
    
    // 配置I2S标准模式
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = BITS_PER_SAMPLE,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = 16,
            .ws_pol = false,
            .bit_shift = true
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_handle));
    
    // 配置按钮
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_config);
}

bool audio_recorder_button_pressed(void)
{
    return gpio_get_level(BUTTON_PIN) == 0; // 低电平有效
}

uint8_t *audio_record(uint32_t duration_ms, size_t *out_size)
{
    const size_t num_samples = (SAMPLE_RATE * duration_ms) / 1000;
    const size_t buffer_size = num_samples * (BITS_PER_SAMPLE / 8);
    
    // 使用PSRAM分配缓冲区
    uint8_t *buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        *out_size = 0;
        return NULL;
    }
    
    size_t bytes_read = 0;
    size_t total_bytes = 0;
    
    // 读取音频数据
    while (total_bytes < buffer_size) {
        if (i2s_channel_read(i2s_handle, buffer + total_bytes, 
                          buffer_size - total_bytes, &bytes_read, 
                          portMAX_DELAY) == ESP_OK) {
            total_bytes += bytes_read;
        }
    }
    
    *out_size = buffer_size;
    return buffer;
}