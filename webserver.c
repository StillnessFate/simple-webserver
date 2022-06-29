#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <ctype.h>
#include <errno.h>
#include "webserver.h"

pthread_mutex_t mutex;
struct socketQueue socketQueue;

int main(int argc, char* argv[])
{
	int serverSock, clientSock;
	struct sockaddr_in serverAddr, clientAddr;
	int clientAddrLen = sizeof(clientAddr);
	int port = 8080;
	int threadNum = get_nprocs() * 2;
	pthread_t* pthread = (pthread_t*)malloc(sizeof(pthread_t*) * threadNum);
	char responseBuff[RESPONSE_BUFFER_SIZE + 1];
	memset(&socketQueue, 0, sizeof(socketQueue));
	pthread_mutex_init(&mutex, NULL);

	if (1 < argc) {
		port = atoi(argv[1]);
	}

	for (int i = 0; i < threadNum; i++)
	{
		if (pthread_create(&pthread[i], NULL, threadProc, NULL) < 0)
		{
			perror("pthread create error");
			return 1;
		}
	}

	if ((serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket error");
		return 1;
	}

	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);

	if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
		perror("bind error");
		return 1;
	}

	if (listen(serverSock, 5) < 0) {
		perror("listen error");
		return 1;
	}

	printf("Server was running on port %d...\n", port);

	while ((clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &clientAddrLen)) > 0) {
		printf("Connected : %s\n", (char*)inet_ntoa(clientAddr.sin_addr));
		if (addSocketInQueue(clientSock) < 0) {
			sendError(responseBuff, 503, STATUS_TEXT_503, clientSock);
			close(clientSock);
		}
	}

	for (int i = 0; i < threadNum; i++)
	{
		pthread_join(pthread[i], NULL);
	}
	pthread_mutex_destroy(&mutex);
	free(pthread);

	return 0;
}

void* threadProc(void* data)
{
	char requestBuff[REQUEST_BUFFER_SIZE + 1];
	char responseBuff[RESPONSE_BUFFER_SIZE + 1];
	char urlBuff[MAX_URL_LENGTH + 1];
	struct requestLine line;
	struct requestHeader headers[MAX_HEADER_COUNT + 1];
	int socket, error;
	struct timespec reqTime;
	reqTime.tv_sec = 0;
	reqTime.tv_nsec = 100000000;


	while (1)
	{
		if ((socket = getSocketInQueue()) < 0) {
			nanosleep(&reqTime, NULL);
			continue;
		}

		if ((error = getRequest(requestBuff, socket)) < 0) {
			if (error == -2) {
				sendError(responseBuff, 408, STATUS_TEXT_408, socket);
			}
			else if (error == -3) {
				sendError(responseBuff, 413, STATUS_TEXT_413, socket);
			}
			else {
				sendError(responseBuff, 500, STATUS_TEXT_500, socket);
			}
		}
		else {
			printf("%s", requestBuff);
			if ((error = parseRequest(requestBuff, &line, headers)) == 0) {
				if (!strcmp(line.method, "GET")) {
					sendResponse(responseBuff, urlBuff, &line, headers, socket);
				}
				else {
					sendError(responseBuff, 405, STATUS_TEXT_405, socket);
				}
			}
			else {
				sendError(responseBuff, 500, STATUS_TEXT_500, socket);
			}
		}

		close(socket);
	}
}

int getRequest(char* buff, int socket) {
	int bytesRead = 0;
	int totalBytesRead = 0;
	int error = 0;

	while (1)
	{
		if ((bytesRead = recv(socket, &buff[totalBytesRead], REQUEST_BUFFER_SIZE - totalBytesRead, MSG_NOSIGNAL)) <= 0) {
			error = -1;
			break;
		}
		totalBytesRead += bytesRead;

		buff[totalBytesRead] = '\0';

		if (4 <= totalBytesRead && !strcmp(&buff[totalBytesRead - 4], "\r\n\r\n")) {
			break;
		}

		if (REQUEST_BUFFER_SIZE <= totalBytesRead) {
			error = -3;
			break;
		}
	}
	
	return error;
}

int parseRequest(char* buff, struct requestLine* line, struct requestHeader* headers) {
	int error = 0;
	int count = 0;
	char* ptr = buff;
	char* tempPtr;

	// parse line
	ptr = strstr(ptr, "\r\n");
	if (!ptr) {
		return -1;
	}
	*ptr = '\0';
	ptr += 2;

	line->method = buff;
	tempPtr = strchr(line->method, ' ');
	if (!tempPtr) {
		return -1;
	}
	*tempPtr = '\0';
	line->url = tempPtr + 1;
	tempPtr = strchr(line->url, ' ');
	if (!tempPtr) {
		return -1;
	}
	*tempPtr = '\0';
	line->protocol = tempPtr + 1;

	// parse headers
	do
	{
		headers[count].key = ptr;
		ptr = strstr(ptr, "\r\n");
		if (!ptr) {
			break;
		}
		*ptr = '\0';
		ptr += 2;

		tempPtr = strchr(headers[count].key, ':');
		if (tempPtr) {
			*tempPtr = '\0';
			headers[count].value = tempPtr + 1;
			while (*(headers[count].value) == ' ')
			{
				headers[count].value++;
			}
		}
		else {
			headers[count].value = NULL;
		}


		count++;
	} while (*ptr != '\0' && strcmp(ptr, "\r\n") && count < MAX_HEADER_COUNT);

	headers[count].key = NULL;

	return error;
}

int sendResponse(char* responseBuff, char* urlBuff, struct requestLine* line, struct requestHeader* headers, int socket) {
	static char* baseFormat =
		"HTTP/1.1 200 OK\r\n"
		"Connection: close\r\n"
		"Server: Simple WebServer\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n\r\n";
	int error = 0;
	char* ptr;
	char* ext = NULL;
	char* contentType = "text/plain";
	char* fileName = "";
	FILE* fp;
	long fileSize, bytesRead, totalBytesRead;
	long bytesSend, totalBytesSend;

	if (ptr = strchr(line->url, '?')) {
		*ptr = '\0';
	}

	if (!strcmp(line->url, "/")) {
		fileName = "index.html";
	}
	else {
		fileName = line->url + 1;
	}

	urlDecode(urlBuff, fileName);
	fileName = urlBuff;

	if (access(fileName, F_OK) < 0) {
		sendError(responseBuff, 404, STATUS_TEXT_404, socket);
		return -1;
	}
	else if (access(fileName, R_OK) < 0) {
		sendError(responseBuff, 403, STATUS_TEXT_403, socket);
		return -1;
	}

	for (int i = strlen(fileName) - 1; 0 <= i; i--)
	{
		if (fileName[i] == '.') {
			ext = &(fileName[i + 1]);
			break;
		}
		else if (fileName[i] == '/') {
			break;
		}
	}

	if (!strcasecmp(ext, "HTML")) {
		contentType = "text/html";
	}
	else if (!strcasecmp(ext, "JS")) {
		contentType = "text/javascript";
	}
	else if (!strcasecmp(ext, "CSS")) {
		contentType = "text/css";
	}
	else if (!strcasecmp(ext, "GIF")) {
		contentType = "image/gif";
	}
	else if (!strcasecmp(ext, "JPG") || !strcasecmp(ext, "JPEG")) {
		contentType = "image/jpeg";
	}
	else if (!strcasecmp(ext, "PNG")) {
		contentType = "image/png";
	}
	else if (!strcasecmp(ext, "MP3")) {
		contentType = "audio/mpeg";
	}
	else if (!strcasecmp(ext, "PDF")) {
		contentType = "application/pdf";
	}

	fp = fopen(fileName, "r");
	if (!fp) {
		sendError(responseBuff, 500, STATUS_TEXT_500, socket);
		return -2;
	}
	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	sprintf(responseBuff, baseFormat, contentType, fileSize);

	if (0 < send(socket, responseBuff, strlen(responseBuff), MSG_NOSIGNAL)) {
		totalBytesRead = 0;
		while ((bytesRead = fread(responseBuff, 1, RESPONSE_BUFFER_SIZE, fp)) != -1)
		{
			bytesSend = 0;
			totalBytesSend = 0;
			while (1)
			{
				bytesSend = send(socket, responseBuff, bytesRead, MSG_NOSIGNAL);
				
				if (bytesSend <= 0) {
					fileSize = -1;
					error = -4;
					break;
				}
				totalBytesSend += bytesSend;
				if (bytesRead <= totalBytesSend) {
					break;
				}
			}
			if (bytesSend)

			totalBytesRead += bytesRead;
			if (fileSize <= totalBytesRead) {
				break;
			}
		}
	}
	else {
		error = -3;
	}

	fclose(fp);

	return error;
}

int addSocketInQueue(int socket) {
	int error = -1;

	pthread_mutex_lock(&mutex);
	if (socketQueue.count < MAX_SOCKET_QUEUE_SIZE) {
		socketQueue.queue[socketQueue.rear++] = socket;
		socketQueue.rear %= MAX_SOCKET_QUEUE_SIZE;
		socketQueue.count++;
		error = 0;
	}
	pthread_mutex_unlock(&mutex);

	return error;
}

int getSocketInQueue() {
	int socket = -1;

	pthread_mutex_lock(&mutex);
	if (0 < socketQueue.count) {
		socket = socketQueue.queue[socketQueue.front++];
		socketQueue.front %= MAX_SOCKET_QUEUE_SIZE;
		socketQueue.count--;
	}
	pthread_mutex_unlock(&mutex);

	return socket;
}

void urlDecode(char* dst, const char* src)
{
	char a, b;

	while (*src) {
		if ((*src == '%') &&
			((a = src[1]) && (b = src[2])) &&
			(isxdigit(a) && isxdigit(b))) {
			if (a >= 'a')
				a -= 'a' - 'A';
			if (a >= 'A')
				a -= ('A' - 10);
			else
				a -= '0';
			if (b >= 'a')
				b -= 'a' - 'A';
			if (b >= 'A')
				b -= ('A' - 10);
			else
				b -= '0';
			*dst++ = 16 * a + b;
			src += 3;
		}
		else if (*src == '+') {
			*dst++ = ' ';
			src++;
		}
		else {
			*dst++ = *src++;
		}
	}
	*dst++ = '\0';
}

void sendError(char* responseBuff, int statusCode, char* statusText, int socket) {
	static char* baseFormat =
		"HTTP/1.1 %d %s\r\n"
		"Connection: close\r\n"
		"Server: Simple WebServer\r\n\r\n"
		"%s";

	sprintf(responseBuff, baseFormat, statusCode, statusText, statusText);
	send(socket, responseBuff, strlen(responseBuff), MSG_NOSIGNAL);
}
