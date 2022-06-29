#pragma once

#define MAX_SOCKET_QUEUE_SIZE 10
#define REQUEST_BUFFER_SIZE 20480
#define RESPONSE_BUFFER_SIZE 20480
#define MAX_URL_LENGTH 2048
#define MAX_HEADER_COUNT 30

#define STATUS_TEXT_403 "Forbidden"
#define STATUS_TEXT_404 "Not Found"
#define STATUS_TEXT_405 "Method Not Allowed"
#define STATUS_TEXT_408 "Request Timeout"
#define STATUS_TEXT_413 "Payload Too Large"
#define STATUS_TEXT_500 "Internal Server Error"
#define STATUS_TEXT_503 "Server Too Busy"

struct requestLine {
    char* method;
    char* url;
    char* protocol;
};

struct requestHeader {
    char* key;
    char* value;
};

struct socketQueue {
    int queue[MAX_SOCKET_QUEUE_SIZE];
    int front;
    int rear;
    int count;
};

void* threadProc(void* data);
int getRequest(char* buff, int socket);
int parseRequest(char* buff, struct requestLine* line, struct requestHeader* headers);
int sendResponse(char* responseBuff, char* urlBuff, struct requestLine* line, struct requestHeader* headers, int socket);
int addSocketInQueue(int socket);
int getSocketInQueue();
void urlDecode(char* dst, const char* src);
void sendError(char* responseBuff, int statusCode, char* statusText, int socket);