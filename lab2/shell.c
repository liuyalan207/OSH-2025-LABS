#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <limits.h> // For PATH_MAX

#define MAX_CMD_LENGTH 256
#define MAX_ARGS 64

sigjmp_buf env;

// 全局变量：存储后台进程的 PID
pid_t bg_pids[100];
int bg_pid_count = 0;

// 字符串分割函数
char** split(char* s, const char* delimiter, int* count) {
    char** tokens = malloc(MAX_ARGS * sizeof(char*));
    if (tokens == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    char* token = strtok(s, delimiter);
    int i = 0;
    while (token != NULL && i < MAX_ARGS - 1) { // 防止越界
        tokens[i++] = token;
        token = strtok(NULL, delimiter);
    }
    tokens[i] = NULL; // 添加结束标志
    *count = i;
    return tokens;
}

// 释放字符串数组
void free_args(char** args) {
    if (args != NULL) {
        free(args);
    }
}

// 执行外部命令
void executeCommand(char** args, int is_background) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        // 子进程
        if (execvp(args[0], args) == -1) {
            perror("execvp failed");
            exit(255);
        }
    } else {
        // 父进程
        if (!is_background) {
            int ret = waitpid(pid, NULL, 0);
            if (ret < 0) {
                perror("waitpid failed");
            }
        } else {
            bg_pids[bg_pid_count++] = pid;
            printf("Running in background with PID %d\n", pid);
        }
    }
}

// 内建命令：wait
void builtin_wait() {
    while (bg_pid_count > 0) {
        int ret = waitpid(bg_pids[0], NULL, 0);
        if (ret < 0) {
            perror("waitpid failed");
            break;
        }
        for (int i = 0; i < bg_pid_count - 1; i++) {
            bg_pids[i] = bg_pids[i + 1];
        }
        bg_pid_count--;
    }
}

// 内建命令：fg
void builtin_fg(int pid) {
    if (pid == -1) {
        if (bg_pid_count == 0) {
            printf("No background jobs\n");
            return;
        }
        pid = bg_pids[0];
    }

    int ret = kill(pid, SIGCONT); // 恢复进程
    if (ret < 0) {
        perror("kill failed");
        return;
    }

    ret = waitpid(pid, NULL, 0); // 等待进程完成
    if (ret < 0) {
        perror("waitpid failed");
        return;
    }

    // 从后台进程列表中移除
    for (int i = 0; i < bg_pid_count; i++) {
        if (bg_pids[i] == pid) {
            for (int j = i; j < bg_pid_count - 1; j++) {
                bg_pids[j] = bg_pids[j + 1];
            }
            bg_pid_count--;
            break;
        }
    }
}

// 内建命令：bg
void builtin_bg(int pid) {
    if (pid == -1) {
        if (bg_pid_count == 0) {
            printf("No background jobs\n");
            return;
        }
        pid = bg_pids[0];
    }

    int ret = kill(pid, SIGCONT); // 恢复进程
    if (ret < 0) {
        perror("kill failed");
        return;
    }

    printf("Running in background with PID %d\n", pid);
}

// 解析重定向
void ParseRedirection(char** args, int is_background) {
    int fd_in = -1, fd_out = -1, fd_append = -1;
    int i = 0;

    // 查找重定向符号
    while (args[i]) {
        if (strcmp(args[i], "<") == 0) {
            fd_in = open(args[i + 1], O_RDONLY);
            if (fd_in < 0) {
                perror("open failed");
                return;
            }
            args[i] = NULL; // 移除重定向符号和文件名
            i += 2;
        } else if (strcmp(args[i], ">") == 0) {
            fd_out = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("open failed");
                return;
            }
            args[i] = NULL; // 移除重定向符号和文件名
            i += 2;
        } else if (strcmp(args[i], ">>") == 0) {
            fd_append = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd_append < 0) {
                perror("open failed");
                return;
            }
            args[i] = NULL; // 移除重定向符号和文件名
            i += 2;
        } else {
            i++;
        }
    }

    // 重定向标准输入
    if (fd_in != -1) {
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);
    }

    // 重定向标准输出
    if (fd_out != -1) {
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
    }

    // 重定向标准输出（追加模式）
    if (fd_append != -1) {
        dup2(fd_append, STDOUT_FILENO);
        close(fd_append);
    }

    executeCommand(args, is_background);
}

// 内建命令：cd
void builtin_cd(char** args) {
    if (args[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
    } else if (chdir(args[1]) != 0) {
        perror("cd failed");
    }
}

// 内建命令：pwd
void builtin_pwd() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("getcwd failed");
    }
}

// 处理管道
void handlePipes(char** commands[], int num_commands) {
    int pipes[num_commands - 1][2];
    pid_t pids[num_commands];

    // 创建管道
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe failed");
            return;
        }
    }

    // 创建子进程并执行命令
    for (int i = 0; i < num_commands; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork failed");
            return;
        }

        if (pids[i] == 0) {
            // 输入重定向
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // 输出重定向
            if (i < num_commands - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // 关闭所有管道
            for (int j = 0; j < num_commands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // 执行命令
            execvp(commands[i][0], commands[i]);
            perror("execvp failed");
            exit(255);
        }
    }

    // 父进程关闭管道
    for (int i = 0; i < num_commands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // 等待所有子进程完成
    for (int i = 0; i < num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

// 信号处理函数
void signalHandler(int signal) {
    if (signal == SIGINT) {
        siglongjmp(env, 1); // 跳转到 sigsetjmp 设置的跳转点
    }
}

// 设置信号处理
void setupSignalHandling() {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);
}

int main() {
    setupSignalHandling(); // 设置信号处理
    char**args; 
    char cmd[MAX_CMD_LENGTH]; // 用来存储读入的一行命令
    while (1) {
        if (sigsetjmp(env, 1) == 0) { // 设置跳转点
            printf("$ "); // 打印提示符

            if (fgets(cmd, MAX_CMD_LENGTH, stdin) == NULL) {
                continue; // 如果读取失败，继续下一次循环
            }

            cmd[strcspn(cmd, "\n")] = '\0'; // 去掉换行符

            int arg_count;
            args = split(cmd, " ", &arg_count); // 按空格分割命令为单词

            // 没有可处理的命令
            if (arg_count == 0) {
                free_args(args);
                continue;
            }

            // 检查是否是后台命令
            int is_background = 0;
            if (arg_count > 1 && strcmp(args[arg_count - 1], "&") == 0) {
                is_background = 1;
                args[arg_count - 1] = NULL;
                arg_count--;
            }

            // 内建命令：exit
            if (strcmp(args[0], "exit") == 0) {
                if (arg_count <= 1) {
                    free_args(args);
                    return 0;
                }

                int code = atoi(args[1]); // 将字符串转换为整数
                if (code < 0) {
                    printf("Invalid exit code\n");
                } else {
                    free_args(args);
                    return code;
                }
            }

            // 内建命令：wait
            if (strcmp(args[0], "wait") == 0) {
                builtin_wait();
                free_args(args);
                continue;
            }

            // 内建命令：fg
            if (strcmp(args[0], "fg") == 0) {
                int pid = (arg_count == 2) ? atoi(args[1]) : -1;
                builtin_fg(pid);
                free_args(args);
                continue;
            }

            // 内建命令：bg
            if (strcmp(args[0], "bg") == 0) {
                int pid = (arg_count == 2) ? atoi(args[1]) : -1;
                builtin_bg(pid);
                free_args(args);
                continue;
            }

            // 内建命令：cd
            if (strcmp(args[0], "cd") == 0) {
                builtin_cd(args);
                free_args(args);
                continue;
            }

            // 内建命令：pwd
            if (strcmp(args[0], "pwd") == 0) {
                builtin_pwd();
                free_args(args);
                continue;
            }

            // 检查是否包含管道符号
            int has_pipe = 0;
            for (int i = 0; args[i]; i++) {
                if (strcmp(args[i], "|") == 0) {
                    has_pipe = 1;
                    break;
                }
            }

            if (has_pipe) {
                // 分割命令
                char** commands[MAX_ARGS];
                int command_count = 0;

                int start = 0;
                for (int i = 0; args[i]; i++) {
                    if (strcmp(args[i], "|") == 0) {
                        args[i] = NULL;
                        commands[command_count++] = &args[start];
                        start = i + 1;
                    }
                }
                commands[command_count++] = &args[start];

                // 处理管道
                handlePipes(commands, command_count);
            } else {
                // 检查是否包含重定向符号
                int has_redirection = 0;
                for (int i = 0; args[i]; i++) {
                    if (strcmp(args[i], "<") == 0 || strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0) {
                        has_redirection = 1;
                        break;
                    }
                }

                if (has_redirection) {
                    ParseRedirection(args, is_background);
                } else {
                    executeCommand(args, is_background);
                }
            }
        } else {
            // 如果捕获到 SIGINT 信号，重新显示提示符
            printf("\n");
        }

        free_args(args); // 释放动态分配的内存
    }
    return 0;
}
