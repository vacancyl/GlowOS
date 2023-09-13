#include "shell.h"
#include "global.h"
#include "stdint.h"
#include "string.h"
#include "syscall.h"
#include "stdio.h"
#include "file.h"
#include "debug.h"

#define cmd_len 128   // 最大支持128个字符
#define MAX_ARG_NR 16 // 命令名外支持15个参数

char cmd_line[cmd_len] = {0}; // 存储输入的命令
char cwd_cache[64] = {0};     // 目录的缓存 执行cd则移动到其他目录去
char *argv[MAX_ARG_NR];       // 参数 每个元素都是char* ,存放的是字符的首地址
char final_path[MAX_PATH_LEN];

// 固定输出提示副
void print_prompt(void)
{
    printf("[vacancy@localhost %s]$ ", cwd_cache);
}

// 最多读入count字节到buf
void readline(char *buf, int32_t count)
{
    ASSERT(buf != NULL && count > 0);
    char *pos = buf;
    // 默认没有到回车就不停止 、一个一个字节读
    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count)
    {
        switch (*pos)
        {
        // 清屏
        case 'l' - 'a':
            *pos = 0;
            clear();
            print_prompt();
            printf("%s", buf); // 把刚刚键入的字符打印
            break;

        // 清除输入
        case 'u' - 'a':
            while (buf != pos)
            {
                putchar('\b');
                *(pos--) = 0;
            }
            break;
        case '\n':
        case '\r':
            *pos = 0;
            putchar('\n');
            return;

        case '\b':
            if (buf[0] != '\b') // 也就是当前的buf是第一个，那就直接什么都不干就行了
            {
                --pos;
                putchar('\b');
            }
            break;

        default:
            putchar(*pos);
            ++pos;
        }
    }
    printf("readline: cant fine entry_key in the cmd_line,max num of char is 128\n");
}

// 解析键入的字符cmd_str token为分割符号 各个单词存入到argv中
int32_t cmd_parse(char *cmd_str, char **argv, char token)
{
    ASSERT(cmd_str != NULL);
    int32_t arg_idx = 0;

    // 初始化指针数组
    while (arg_idx < MAX_ARG_NR)
    {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    char *next = cmd_str;
    int32_t argc = 0;
    while (*next)
    {
        while (*next == token)
            ++next; // 去除空格
        if (*next == 0)
            break;

        argv[argc] = next; // 是个地址 头地址+offset 存储的也是地址

        while (*next && *next != token) // 直到结束或者分隔
            ++next;

        if (*next)
            *(next++) = 0; // 设置单词的结束符

        if (argc > MAX_ARG_NR)
            return -1;

        ++argc;
    }
    return argc; // 多少个参数
}

void my_shell(void)
{
    cwd_cache[0] = '/';
    cwd_cache[1] = 0;
    int argc = -1;

    while (1)
    {
        print_prompt();
        memset(cmd_line, 0, cmd_len);
        memset(final_path, 0, MAX_PATH_LEN);
        readline(cmd_line, cmd_len);
        if (cmd_line[0] == 0)
            continue;

        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1)
        {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }

        if (!strcmp("ls", argv[0]))
            buildin_ls(argc, argv);
        else if (!strcmp("pwd", argv[0]))
            buildin_pwd(argc, argv);
        else if (!strcmp("ps", argv[0]))
            buildin_ps(argc, argv);
        else if (!strcmp("cd", argv[0]))
        {
            if (buildin_cd(argc, argv) != NULL)
            {
                memset(cwd_cache, 0, 64);
                strcpy(cwd_cache, final_path);
            }
        }
        else if (!strcmp("clear", argv[0]))
            buildin_clear(argc, argv);
        else if (!strcmp("mkdir", argv[0]))
            buildin_mkdir(argc, argv);
        else if (!strcmp("rmdir", argv[0]))
            buildin_rmdir(argc, argv);
        else if (!strcmp("rm", argv[0]))
            buildin_rm(argc, argv);
        else
        {   // 如果是外部命令,需要从磁盘上加载
            int32_t pid = fork();
            if (pid)
            { 
                /*
                这个 while 必须要加上，否则父进程一般情况下会比子进程先执行，因此会进行下－轮循环将 findl_path 清空，这样子进程将无法从 final_path 中获得参数＊
                */
                // 父进程
                while (1)
                    ;
            }
            else
            { // 子进程
                make_clear_abs_path(argv[0], final_path);//类似于 /xx/xx.bin agv1 argv2
                argv[0] = final_path;

                /* 先判断下文件是否存在 */
                struct stat file_stat;
                memset(&file_stat, 0, sizeof(struct stat));
                if (stat(argv[0], &file_stat) == -1)
                {
                    printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
                }
                else
                {
                    
                    execv(argv[0], argv);
                }
                while (1)
                    ;
            }
        }
    }
    PANIC("my_shell: should not be here");
}
