#include "esp_http_client.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "cJSON.h"
#include "asr_paraformer.h"
#include "mbedtls/md.h"   // HMAC 签名
#include "base64.h"          // Base64 编码
#include "esp_tls.h"         // HTTPS 支持
#include "esp_err.h"         // ESP 错误码定义
#include "esp_system.h"      // ESP 系统函数

// 配置日志级别
#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 3
#endif

#define TAG "ASR_CLIENT"

// 阿里云认证信息
const char* ALIYUN_ACCESS_KEY_ID = "";    #填阿里云api的id
const char* ALIYUN_ACCESS_KEY_SECRET = "";  #填阿里云的api

// Paraformer API 配置
const char* PARA_URL = "https://nls-gateway.cn-shanghai.aliyuncs.com/api/predict/paraformer-v2"; 

// base64.c
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

size_t base64_encode(const uint8_t* data, size_t len, char* out) {
    size_t out_len = (len + 2) / 3 * 4;
    uint8_t* src = (uint8_t*)data;
    char* dst = out;

    for (size_t i = 0; i < len; i += 3) {
        uint8_t b1 = src[i];
        uint8_t b2 = (i+1 < len) ? src[i+1] : 0;
        uint8_t b3 = (i+2 < len) ? src[i+2] : 0;

        *dst++ = b64_table[b1 >> 2];
        *dst++ = b64_table[((b1 & 0x03) << 4) | ((b2 & 0xf0) >> 4)];
        *dst++ = (i+1 < len) ? b64_table[((b2 & 0x0f) << 2) | ((b3 & 0xc0) >> 6)] : '=';
        *dst++ = (i+2 < len) ? b64_table[b3 & 0x3f] : '=';
    }
    *dst = '\0';
    return out_len;
}

static char asr_endpoint[128];

void asr_client_init(const char *endpoint)
{
    strncpy(asr_endpoint, endpoint, sizeof(asr_endpoint)-1);
}

char* generate_nonce() {
    static char nonce[16];
    // 实现生成 16 位随机字符串
    return nonce;
}

void generate_signature(const char* string_to_sign, const char* secret, char* signature, size_t sig_len) {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret, strlen(secret));
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)string_to_sign, strlen(string_to_sign));
    mbedtls_md_hmac_finish(&ctx, (unsigned char*)signature);
    mbedtls_md_free(&ctx);
}

char *asr_recognize(uint8_t *audio_data, size_t audio_size)
{
    // 1. Base64 编码音频数据
    char* audio_b64 = malloc(audio_size * 2);  // Base64 最大扩展 1.33x
    if (!audio_b64) {
        ESP_LOGE(TAG, "Memory allocation failed for Base64 buffer");
        return NULL;
    }
    base64_encode(audio_data, audio_size, audio_b64);

    // 2. 构建 JSON 请求体
    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "app", cJSON_CreateObject());
    cJSON_AddItemToObject(root, "audio", cJSON_CreateString(audio_b64));
    const char* post_data = cJSON_PrintUnformatted(root);
    free(audio_b64);
    cJSON_Delete(root);    char string_to_sign[256];
    char date_str[32];
    time_t now = time(NULL);
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
    char* nonce = generate_nonce();
    snprintf(string_to_sign, sizeof(string_to_sign),
         "POST\n/parafomr-v2\n%s\napplication/json\nx-acs-signature-method:HmacSHA1\nx-acs-signature-nonce:%s", 
         date_str, "unique_nonce");  // 需要生成唯一随机数作为 nonce

    char signature[32];
    generate_signature(string_to_sign, ALIYUN_ACCESS_KEY_SECRET, signature, sizeof(signature));
    
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header),
             "acs %s:%s", ALIYUN_ACCESS_KEY_ID, signature);

    // 4. 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = PARA_URL,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = NULL,  // 可选：设置 CA 证书
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Date", date_str);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // 5. 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free((void*)post_data);
        esp_http_client_cleanup(client);
        return NULL;
    }

    // 6. 解析响应
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP status: %d", status_code);
        free((void*)post_data);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int content_len = esp_http_client_get_content_length(client);
    char *response = malloc(content_len + 1);
    esp_http_client_read(client, response, content_len);
    response[content_len] = '\0';

    esp_http_client_cleanup(client);
    free((void*)post_data);

    // 7. 解析 JSON 结果
    cJSON *root = cJSON_Parse(response);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        free(response);
        return NULL;
    }
    
    cJSON *output = cJSON_GetObjectItemCaseSensitive(root, "output");
    if (!output || !cJSON_IsObject(output)) {
        ESP_LOGE(TAG, "Invalid response format: missing 'output' field");
        cJSON_Delete(root);
        free(response);
        return NULL;
    }

    cJSON *text = cJSON_GetObjectItemCaseSensitive(output, "text");
    if (!text || !cJSON_IsString(text)) {
        ESP_LOGE(TAG, "Invalid response format: missing 'text' field");
        cJSON_Delete(root);
        free(response);
        return NULL;
    }

    char *result = strdup(text->valuestring);
    cJSON_Delete(root);
    free(response);
    
    return result;
}
