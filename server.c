#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "memmem.h"

#define HEADBUFFSIZE 4096 // Header exccedd this size will throw error
#define SERV_PORT 57080
#define LISTENQ 128

typedef struct sockaddr SA;

typedef enum httpMethod {
    HttpMethodGet, HttpMethodPost, HttpMethodUnknown
} HttpMethod;

typedef struct httpReqHeader {
    HttpMethod method;
    char *resource;
    int contentLength;
    char *contentType;
} HttpReqHeader;

typedef struct httpResHeader {
    int statusCode;
    char *statusMsg;
    int contentLength;
    char *contentType;
} HttpResHeader;

int connectionHandler(int connfd);
int httpHeaderParser(char *sourceData, int sourceSize, HttpReqHeader *header, char *remainData);
char *getword(char *str, char *word);
char *mygetline(char *str, char *line);
int httpGetHandler(HttpReqHeader *header, int connfd);
int httpResHeaderGenerator(HttpResHeader *header, char *writeBuff);
int httpPostHandler(HttpReqHeader *reqHeader, char *bodyData, int connfd);
int formHandler(HttpReqHeader *reqHeader, char *bodyData);
int resourceToType(const char *resource, char *contentType);

char *wwwDir = NULL;
char *uploadDir = NULL;

int main(int argc, char **argv) {
    int listenfd, connfd;
    int n;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen = sizeof(cliaddr);

    if (argc < 3) {
        printf("usage: ./server <wwwDir> <uploadDir>\n");
        return 1;
    }

    wwwDir = malloc(1024);
    uploadDir = malloc(1024);
    strcpy(wwwDir, argv[1]);
    strcpy(uploadDir, argv[2]);

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    bind(listenfd, (SA *) &servaddr, sizeof(servaddr));

    listen(listenfd, LISTENQ);

    printf("Server is running at port %d ... waiting for connections.\n", SERV_PORT);

    for ( ; ; ) {
        printf("Waiting for new connection...\n");
        connfd = accept(listenfd, (SA *) &cliaddr, &clilen);

        printf("\nClient connected! IP: %s Port: %d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

        if (fork() == 0) {
            if (fork() == 0) {
                close(listenfd);
                connectionHandler(connfd);
                printf("\nTransmission finished! IP: %s Port: %d\n\n\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
                close(connfd);
                return 0;
            }
            close(listenfd);
            close(connfd);
            return 0;
        }
        wait(NULL);

        close(connfd);
    }

    close(listenfd);

    free(wwwDir);
    free(uploadDir);

    return 0;
}

char *getword(char *str, char *word) {
    char *p = str;
    char *q = word;

    *q = '\0';

    while (*p == ' ')
        p++;
    
    while (!(*p == ' ' || *p == '\0'))
        *q++ = *p++;
    
    if (q == word)
        return NULL;

    *q = '\0';

    return p;
}

// get one line and slice space in the head
char *mygetline(char *str, char *line) {
    char *p = str;
    char *q = line;

    *q = '\0';

    while (*p == '\r' || *p == '\n' || *p == ' ')
        p++;
    
    while (!(*p == '\r' || *p == '\n' ||*p == '\0'))
        *q++ = *p++;
    
    if (q == line)
        return NULL;

    *q = '\0';

    return p;
}

int connectionHandler(int connfd) {
    HttpReqHeader *header = (HttpReqHeader *) malloc(sizeof(HttpReqHeader));
    char *headerBuffer = malloc(HEADBUFFSIZE + 1);
    char *remainBuffer = malloc(HEADBUFFSIZE + 1);
    char *bodyBuffer = NULL;
    char *bodyCursor = NULL;
    int readSize, totalReadSize, headerSize, bodySize;

    readSize = read(connfd, headerBuffer, HEADBUFFSIZE);
    printf("# FIRST READ readSize=%d\n", readSize);
    headerBuffer[readSize] = '\0';

    headerSize = httpHeaderParser(headerBuffer, readSize, header, remainBuffer);

    if (headerSize < 0) {
        printf("header parse error\n");
        return -1;
    }

    printf("\n\nheader parse: method=%d, resource=%s, contentType=%s, contentLength=%d\n", header->method, header->resource, header->contentType, header->contentLength);
    char contenttype[64];
    resourceToType(header->resource, contenttype);
    printf("resource-content-type=%s\n\n", contenttype);

    // Move body data from remainBuffer to bodyBuffer
    if (header->contentLength > 0) {
        bodyBuffer = malloc(header->contentLength + 1);
        bodyCursor = bodyBuffer;
        memcpy(bodyBuffer, remainBuffer, readSize - headerSize);
        bodyCursor += (readSize - headerSize);
    }

    // Set readSize to body data size which has been read
    totalReadSize = readSize - headerSize;

    // Read remain part of body
    while (totalReadSize < header->contentLength) {
        readSize = read(connfd, bodyCursor, header->contentLength); // read as much data as better
        bodyCursor += readSize;
        totalReadSize += readSize;
    }

    printf("## Request header\n");
    if ( fputs(headerBuffer, stdout) == EOF )
            printf("headerBuffer fputs error\n");
    if (header->method == HttpMethodPost) {
        *bodyCursor = '\0';
        printf("## Request body\n");
        if ( fputs(bodyBuffer, stdout) == EOF )
                printf("headerBuffer fputs error\n");
    }

    if (header->method == HttpMethodGet) {
        httpGetHandler(header, connfd);
    }
    else if (header->method == HttpMethodPost) {
        *bodyCursor = '\0';
        httpPostHandler(header, bodyBuffer, connfd);
    }
    else {}
    
    free(header->contentType);
    free(header->resource);
    free(header);
    free(headerBuffer);
    free(remainBuffer);
    free(bodyBuffer);

    return 0;
}

/*
sourceData should contain '\0' in the end.
Return header size in bytes if seccess, -1 if failed.
*/
int httpHeaderParser(char *sourceData, int sourceSize, HttpReqHeader *header, char *remainData) {
    // Init header
    header->contentLength = 0;
    header->contentType = malloc(HEADBUFFSIZE + 1);
    header->contentType[0] = '\0';
    header->method = HttpMethodUnknown;
    header->resource = malloc(HEADBUFFSIZE + 1);
    header->resource[0] = '/';
    header->resource[1] = '\0';

    char *endOfData = sourceData + sourceSize;

    char *endOfHeader = strstr(sourceData, "\r\n\r\n");

    if (endOfHeader == NULL) {// Header is too large.
        printf("Header is too large!\n");
        return -1;
    }
    
    endOfHeader += 4;

    int headerSize = endOfHeader - sourceData;
    int remainSize = endOfData - endOfHeader;

    printf("headerSize=%d remainSize=%d\n", headerSize, remainSize);

    memcpy(remainData, endOfHeader, remainSize);
    remainData[remainSize] = '\0';

    *endOfHeader = '\0';

    if (header == NULL)
        return headerSize;

    // Fetch lines
    char *headerLineCursor = sourceData;
    char *headerLine = malloc(HEADBUFFSIZE + 1);
    headerLineCursor = mygetline(headerLineCursor, headerLine);
    if (headerLineCursor == NULL) { // header doesn't contain method and resource
        printf("Header missing first line!\n");
        return -1;
    }

    // Fetch params
    char *paramCursor;
    char *param = malloc(HEADBUFFSIZE + 1);

    // Deal with the first line of the header
    paramCursor = headerLine;

    paramCursor = getword(paramCursor, param); // HTTP Method
    if (paramCursor == NULL) {
        printf("Http Method parse error!\n");
        return -1;
    }
    if (strcmp(param, "GET") == 0) {
        header->method = HttpMethodGet;
    }
    else if (strcmp(param, "POST") == 0) {
        header->method = HttpMethodPost;
    }
    else {
        header->method = HttpMethodUnknown;
    }

    paramCursor = getword(paramCursor, param); // HTTP Resource
    if (paramCursor == NULL) {
        printf("Http Resource parse error!\n");
        return -1;
    }
    strcpy(header->resource, param);

    paramCursor = getword(paramCursor, param); // HTTP Version
    if (paramCursor == NULL) {
        printf("Http Version parse error!\n");
        return -1;
    }

    // Deal with other header lines
    while ( (headerLineCursor = mygetline(headerLineCursor, headerLine)) != NULL ) {
        paramCursor = headerLine;

        paramCursor = getword(paramCursor, param); // Entity name

        if (strcmp(param, "Content-Type:") == 0) {
            mygetline(paramCursor, header->contentType); // Entity value
        }
        else if (strcmp(param, "Content-Length:") == 0) {
            mygetline(paramCursor, param); // Entity value
            header->contentLength = atoi(param);
        }
    }

    free(headerLine);
    free(param);

    return headerSize;
}

int httpGetHandler(HttpReqHeader *reqHeader, int connfd) {
    HttpResHeader *resHeader = (HttpResHeader *) malloc(sizeof(HttpResHeader));
    resHeader->contentLength = 0;
    resHeader->contentType = malloc(64);
    resHeader->contentType[0] = '\0';
    resHeader->statusCode = 500;
    resHeader->statusMsg = malloc(128);
    resHeader->statusMsg[0] = '\0';

    int fileSize = 0;
    char *writeBuff = NULL;
    char *writeBuffCursor = NULL;
    char * fullResourcePath = malloc(1024);
    strcpy(fullResourcePath, wwwDir);
    strcat(fullResourcePath, reqHeader->resource);

    FILE *fp = fopen(fullResourcePath, "r+");

    if (fp != NULL) {
        printf("Open file success: %s\n", fullResourcePath);
        fseek(fp, 0L, SEEK_END);
        fileSize = ftell(fp);
        rewind(fp);

        resHeader->contentLength = fileSize;
        resourceToType(fullResourcePath, resHeader->contentType);
        resHeader->statusCode = 200;
        strcpy(resHeader->statusMsg, "OK");

        writeBuff = malloc(fileSize + 1024);
        writeBuffCursor = writeBuff;

        writeBuffCursor += httpResHeaderGenerator(resHeader, writeBuffCursor);

        fread(writeBuffCursor, fileSize, 1, fp);
        writeBuffCursor += fileSize;

        fclose(fp);
    }
    else {
        printf("Open file failed: %s\n", fullResourcePath);
        resHeader->contentLength = strlen("404 not fount");
        resourceToType("/", resHeader->contentType);
        resHeader->statusCode = 404;
        strcpy(resHeader->statusMsg, "Not Found");

        writeBuff = malloc(fileSize + 1024);
        writeBuffCursor = writeBuff;

        writeBuffCursor += httpResHeaderGenerator(resHeader, writeBuffCursor);

        strcpy(writeBuffCursor, "404 not found");
        writeBuffCursor += strlen("404 not found");
    }

    // Write to connfd
    *writeBuffCursor = '\0';
    printf("## Response\n");
    fputs(writeBuff, stdout);
    write(connfd, writeBuff, writeBuffCursor - writeBuff);

    free(resHeader->contentType);
    free(resHeader->statusMsg);
    free(resHeader);
    free(writeBuff);
    free(fullResourcePath);

    return 0;
}


// Return bytes written
int httpResHeaderGenerator(HttpResHeader *header, char *writeBuff) {
    char *buffCursor = writeBuff;

    // Construct first line
    strcpy(buffCursor, "HTTP/1.1 ");
    buffCursor += strlen("HTTP/1.1 ");

    buffCursor += sprintf(buffCursor, "%d ", header->statusCode);

    strcpy(buffCursor, header->statusMsg);
    buffCursor += strlen(header->statusMsg);

    *buffCursor++ = '\r';
    *buffCursor++ = '\n';

    // Construct other lines
    buffCursor += sprintf(buffCursor, "Content-Type: ");
    strcpy(buffCursor, header->contentType);
    buffCursor += strlen(header->contentType);
    buffCursor += sprintf(buffCursor, "\r\n");

    buffCursor += sprintf(buffCursor, "Content-Length: %d\r\n", header->contentLength);

    strcpy(buffCursor, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
    buffCursor += strlen("Cache-Control: no-cache, no-store, must-revalidate\r\n");

    strcpy(buffCursor, "Pragma: no-cache\r\n");
    buffCursor += strlen("Pragma: no-cache\r\n");

    strcpy(buffCursor, "Expires: 0\r\n");
    buffCursor += strlen("Expires: 0\r\n");

    // Empty line (end of header)
    *buffCursor++ = '\r';
    *buffCursor++ = '\n';

    return buffCursor - writeBuff;
}

int httpPostHandler(HttpReqHeader *reqHeader, char *bodyData, int connfd) {
    char *contentType = malloc(64);
    getword(reqHeader->contentType, contentType);

    if (strcmp(contentType, "multipart/form-data;") == 0) {
        if ( formHandler(reqHeader, bodyData) < 0 ) {
            printf("form handle error!\n");
        }
    }
    else {
        printf("unknown POST content-type!\n");
    }

    httpGetHandler(reqHeader, connfd);

    free(contentType);
    return 0;
}

int formHandler(HttpReqHeader *reqHeader, char *bodyData) {
    char *tmp = malloc(128);
    char *boundary = malloc(128);
    char *filename = malloc(256);
    char *sectionStart = NULL;
    char *metaStart = NULL;
    char *filenamePosStart = NULL;
    char *filenamePosEnd = NULL;
    char *metaEnd = NULL;
    char *dataStart = NULL;
    char *dataEnd = NULL;
    char *sectionEnd = NULL;

    char *fullFilePath = malloc(1024);
    
    char *p = getword(reqHeader->contentType, tmp);
    if ( p == NULL || getword(p, tmp) == NULL) {
        printf("can't find possible boundary in content-type!\n");
        return -1;
    }

    p = strstr(tmp, "boundary=");
    if (p == NULL) {
        printf("can't find boundary in content-type!\n");
        return -1;
    }

    p += strlen("boundary=");

    strcpy(boundary, p);

    printf("boundary: ");
    fputs(boundary, stdout);
    printf("\n");

    sectionStart = strstr(bodyData, boundary);
    if (sectionStart == NULL) {
        printf("invalid form data!\n");
        return -1;
    }
    sectionStart += strlen(boundary); // Skip bounday string
    sectionStart += 2; // Skip "\r\n"

    if ( (reqHeader->contentLength - (sectionStart - bodyData)) <= 0 ) { // Check if exceed body
        printf("invalid form data!\n");
        return -1;
    }

    sectionEnd = memmem(sectionStart, reqHeader->contentLength - (sectionStart - bodyData), boundary, strlen(boundary)); // Can't use strstr since binary data may contain 0
    if (sectionEnd == NULL) {
        printf("invalid form data!\n");
        return -1;
    }

    while (sectionEnd != NULL) {
        metaStart = sectionStart;
        metaEnd = memmem(metaStart, reqHeader->contentLength - (metaStart - bodyData), "\r\n\r\n", strlen("\r\n\r\n"));
        if (metaEnd == NULL) {
            printf("invalid form data!\n");
            return -1;
        }
        metaEnd += 4; // Skip "\r\n\r\n"

        dataStart = metaEnd;
        dataEnd = sectionEnd - 4; // Back before "\r\n--"
        if (dataEnd < dataStart) {
            printf("invalid form data!\n");
            return -1;
        }


        filename[0] = '\0';
        if ( ( filenamePosStart = memmem(metaStart, metaEnd - metaStart, "filename=\"", strlen("filename=\"")) ) != NULL ) {
            filenamePosStart += strlen("filename=\"");
            if ( ( filenamePosEnd = memmem(filenamePosStart, metaEnd - filenamePosStart, "\"", strlen("\"")) ) != NULL ) {
                if (filenamePosEnd > filenamePosStart) {
                    memcpy(filename, filenamePosStart, filenamePosEnd - filenamePosStart);
                    filename[filenamePosEnd - filenamePosStart] = '\0';
                }
            }
        }

        FILE *fp = NULL;
        if (filename[0] != '\0') {
            printf("filename: %s\n", filename);
            strcpy(fullFilePath, uploadDir);
            strcat(fullFilePath, "/");
            strcat(fullFilePath, filename);
            fp = fopen(fullFilePath, "w+");
            if (fp != NULL) {
                printf("Open file success: %s\n", fullFilePath);
                fwrite(dataStart, dataEnd - dataStart, 1, fp);
                fclose(fp);
            }
            else {
                printf("Open file failed: %s\n", fullFilePath);
            }
        }

        sectionStart = sectionEnd + strlen(boundary); // Skip bounday string
        sectionStart += 2; // Skip "\r\n"

        if ( (reqHeader->contentLength - (sectionStart - bodyData)) <= 0 ) // Check if exceed body
            break;

        sectionEnd = memmem(sectionStart, reqHeader->contentLength - (sectionStart - bodyData), boundary, strlen(boundary));
    }

    free(tmp);
    free(boundary);
    free(filename);
    free(fullFilePath);

    return 0;
}

int resourceToType(const char *resource, char *contentType) {
    const char *p = resource;
    char *extension = malloc(16);
    char *q = extension;

    while (*p != '\0')
        p++;
    
    while (*p != '.' && *p != '/' && p > resource)
        p--;
    
    if (*p == '.') {
        p++;
        while (*p != '\0')
            *q++ = *p++;
        *q = '\0';
    }
    else {
        *q = '\0';
    }

    if (strcmp(extension, "html") == 0) {
        strcpy(contentType, "text/html");
    }
    else if (strcmp(extension, "css") == 0) {
        strcpy(contentType, "text/css");
    }
    else if (strcmp(extension, "js") == 0) {
        strcpy(contentType, "text/javascript");
    }
    else if (strcmp(extension, "png") == 0) {
        strcpy(contentType, "image/png");
    }
    else if (strcmp(extension, "jpg") == 0) {
        strcpy(contentType, "image/jpeg");
    }
    else if (strcmp(extension, "jpeg") == 0) {
        strcpy(contentType, "image/jpeg");
    }
    else {
        strcpy(contentType, "text/plain");
    }

    free(extension);

    return 0;
}
