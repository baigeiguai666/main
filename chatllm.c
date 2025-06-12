#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "cJSON.h"

// DeepSeek API 配置
#define DEEPSEEK_API_ENDPOINT "https://api.deepseek.com/v1/chat/completions"
#define DEEPSEEK_API_KEY "sk-8e4332e1ce2b44f3889f26bb0bc9e91b"  // API密钥
#define DEEPSEEK_MODEL "deepseek-chat"           // 使用DeepSeek聊天模型

// 定义对话消息结构
typedef struct {
    char role[16];      // "system", "user" 或 "assistant"
    char content[256];  // 消息内容
} ChatMessage;

// 创建对话请求
char* build_deepseek_request(ChatMessage messages[], int message_count) {
    cJSON *root = cJSON_CreateObject();
    
    // 添加模型参数
    cJSON_AddStringToObject(root, "model", DEEPSEEK_MODEL);
    
    // 添加消息数组
    cJSON *messages_array = cJSON_AddArrayToObject(root, "messages");
    for (int i = 0; i < message_count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", messages[i].role);
        cJSON_AddStringToObject(msg, "content", messages[i].content);
        cJSON_AddItemToArray(messages_array, msg);
    }
    
    // 添加其他参数
    cJSON_AddNumberToObject(root, "max_tokens", 512);
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON_AddBoolToObject(root, "stream", false);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// 处理DeepSeek API响应
char* parse_deepseek_response(char *response) {
    cJSON *root = cJSON_Parse(response);
    if (!root) {
        printf("Failed to parse JSON response\n");
        return NULL;
    }
    
    // 检查错误
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *error_msg = cJSON_GetObjectItem(error, "message");
        printf("API Error: %s\n", error_msg->valuestring);
        cJSON_Delete(root);
        return NULL;
    }
    
    // 提取回复内容
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_GetArraySize(choices) > 0) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        cJSON *message = cJSON_GetObjectItem(first_choice, "message");
        cJSON *content = cJSON_GetObjectItem(message, "content");
        
        char *reply = strdup(content->valuestring);  // 复制回复内容
        cJSON_Delete(root);
        return reply;
    }
    
    cJSON_Delete(root);
    return NULL;
}

// HTTP 事件处理程序
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static char *response_buffer = NULL;
    static size_t response_len = 0;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // 追加接收到的数据到缓冲区
            response_buffer = realloc(response_buffer, response_len + evt->data_len + 1);
            memcpy(response_buffer + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_buffer[response_len] = '\0';
            break;
            
        case HTTP_EVENT_ON_FINISH:
            // 处理完整的响应
            if (response_buffer) {
                char *reply = parse_deepseek_response(response_buffer);
                if (reply) {
                    printf("\nDeepSeek回复: %s\n", reply);
                    free(reply);
                }
                free(response_buffer);
                response_buffer = NULL;
                response_len = 0;
            }
            break;
            
        case HTTP_EVENT_ERROR:
            printf("HTTP请求出错\n");
            if (response_buffer) {
                free(response_buffer);
                response_buffer = NULL;
                response_len = 0;
            }
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

// 调用DeepSeek API
void call_deepseek_api(ChatMessage messages[], int message_count) {
    // 构建请求JSON
    char *request_json = build_deepseek_request(messages, message_count);
    if (!request_json) {
        printf("创建请求失败\n");
        return;
    }
    
    printf("发送请求到DeepSeek:\n%s\n", request_json);
    
    // 配置HTTP客户端
    esp_http_client_config_t config = {
        .url = DEEPSEEK_API_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .buffer_size = 4096,
        .user_data = NULL,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", "Bearer " DEEPSEEK_API_KEY);
    
    // 设置请求体
    esp_http_client_set_post_field(client, request_json, strlen(request_json));
    
    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        printf("HTTP请求失败: %s\n", esp_err_to_name(err));
    }
    
    // 清理资源
    esp_http_client_cleanup(client);
    free(request_json);
}

// 示例使用
void deepseek_example(void) {
    // 创建对话消息
    ChatMessage messages[] = {
        {"system", "你是一个乐于助人的AI助手"},
        {"user", "请解释量子计算的基本原理"}
    };
    int message_count = sizeof(messages) / sizeof(messages[0]);
    
    // 调用API
    call_deepseek_api(messages, message_count);
}