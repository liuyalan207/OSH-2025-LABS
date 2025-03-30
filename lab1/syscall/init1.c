#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#define SYS_hello 548 // 替换为实际的系统调用号

int main()
{
    char buf[100]; // 足够大的缓冲区
    int ret;

    // 测试长度充足的情况
    ret = syscall(SYS_hello, buf, sizeof(buf));
    if (ret == 0) {
        printf("Length sufficient: %s\n", buf);
    } else {
        printf("Length sufficient: Failed with return value %d\n", ret);
    }

    // 测试长度不足的情况
    ret = syscall(SYS_hello, buf, 10); // 缓冲区长度小于字符串长度
    if (ret == 0) {
        printf("Length insufficient: %s\n", buf);
    } else {
        printf("Length insufficient: Failed with return value %d\n", ret);
    }

    while (1) {} // 防止程序退出导致 kernel panic
    return 0;
}
