#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>
#include <errno.h>

#define PORT 8000
#define BUFFER_SIZE 1024 * 1024 // 最大读取 1MiB 数据
#define MAX_PATH_LEN 1024
#define MAX_PATH 1024

// 发送标准响应
void send_response(int client_fd, const char *status, const char *content_type, const char *content, size_t content_length) {
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response),
             "HTTP/1.0 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "\r\n",
             status, content_type, content_length);
    write(client_fd, response, strlen(response));
    if (content && content_length > 0) {
        write(client_fd, content, content_length);
    }
}

// 发送错误响应
void send_error(int client_fd, int status_code) {
    const char *status = (status_code == 404) ? "404 Not Found" : "500 Internal Server Error";
    const char *error_message = (status_code == 404) ? "The requested resource was not found." : "Internal server error.";
    send_response(client_fd, status, "text/plain", error_message, strlen(error_message));
}

// 检查路径是否安全（防止路径穿越）
int is_safe_path(const char *root, const char *path) {
    char resolved_path[MAX_PATH];
    if (!realpath(path, resolved_path)) {
        return 0;
    }
    return strncmp(resolved_path, root, strlen(root)) == 0;
}

// 解析 HTTP 请求行
int parse_request(char *request, char *method, char *path, char *protocol) {
    if (sscanf(request, "%s %s %s", method, path, protocol) != 3) {
        return -1; // 格式不正确
    }
    return 0;
}

// 处理客户端连接的线程函数
void* handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE] = {0};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }

    // 解析请求行
    char method[16], path[MAX_PATH_LEN], protocol[16];
    if (parse_request(buffer, method, path, protocol) != 0) {
        send_error(client_fd, 500);
        close(client_fd);
        return NULL;
    }

    // 检查是否为合法的 GET 请求
    if (strcmp(method, "GET") != 0 || strcmp(protocol, "HTTP/1.0") != 0) {
        send_error(client_fd, 500);
        close(client_fd);
        return NULL;
    }

    // 构造文件路径
    char file_path[MAX_PATH_LEN];
    char current_dir[MAX_PATH];
    if (!getcwd(current_dir, sizeof(current_dir))) {
        send_error(client_fd, 500);
        close(client_fd);
        return NULL;
    }

    // 特殊处理根路径：返回 ./index.html
    if (strcmp(path, "/") == 0) {
        strncpy(file_path, "./index.html", sizeof(file_path));
        file_path[sizeof(file_path) - 1] = '\0'; // 确保字符串结尾
    } else {
        int len = snprintf(file_path, sizeof(file_path), ".%s", path);
        if (len < 0 || len >= sizeof(file_path)) {
            send_error(client_fd, 500);
            close(client_fd);
            return NULL;
        }
    }

    // 检查路径是否合法（防止路径穿越）
    if (!is_safe_path(current_dir, file_path)) {
        send_error(client_fd, 500);
        close(client_fd);
        return NULL;
    }

    // 检查文件是否存在
    if (access(file_path, F_OK) == -1) {
        send_error(client_fd, 404);
        close(client_fd);
        return NULL;
    }

    // 打开文件
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        send_error(client_fd, 404);
        close(client_fd);
        return NULL;
    }

    struct stat file_stat;
    fstat(fd, &file_stat);
    off_t file_size = file_stat.st_size;

    // 读取文件内容
    char *file_content = malloc(file_size);
    if (!file_content) {
        close(fd);
        send_error(client_fd, 500);
        close(client_fd);
        return NULL;
    }

    if (read(fd, file_content, file_size) != file_size) {
        free(file_content);
        close(fd);
        send_error(client_fd, 500);
        close(client_fd);
        return NULL;
    }

    close(fd);

    // 发送响应
    send_response(client_fd, "200 OK", "text/html", file_content, file_size);

    // 清理资源
    free(file_content);
    close(client_fd);
    return NULL;
}

// 主函数：启动服务器
int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置地址复用（允许重启后快速重连）
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听 0.0.0.0
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) == -1) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Serving HTTP on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (*client_fd == -1) {
            free(client_fd);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd) != 0) {
            free(client_fd);
            close(*client_fd);
            continue;
        }

        pthread_detach(tid); // 自动回收线程资源
    }

    close(server_fd);
    return 0;
}
