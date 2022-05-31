/*///////////////////////////////////////////////////////////
/                                                           /
/    CSE4100-02 System Programming                          /
/    Project1 phase2 :                                      /
/        Redirection and piping in your shell               /
/    20181701 Seokchan Hong                                 /
/    Copyright 2022. Seokchan Hong. All rights reserved.    /
/                                                           /
///////////////////////////////////////////////////////////*/

/* $begin shellmain */
#include "myshell.h"
#include<errno.h>
#define MAXARGS   128

/* necessary functions. copied at csapp.c*/
void app_error(char* msg) /* Application error */
{
    fprintf(stderr, "%s\n", msg);
    exit(0);
}
void unix_error(char* msg) /* Unix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(0);
}
char* Fgets(char* ptr, int n, FILE* stream)
{
    char* rptr;

    if (((rptr = fgets(ptr, n, stream)) == NULL) && ferror(stream))
        app_error("Fgets error");

    return rptr;
}
/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}
void sio_error(char s[]) /* Put error message and exit */
{
    sio_puts(s);
    _exit(1);                                      //line:csapp:sioexit
}
ssize_t sio_puts(char s[]) /* Put string */
{
    return write(STDOUT_FILENO, s, sio_strlen(s)); //line:csapp:siostrlen
}
ssize_t Sio_puts(char s[])
{
    ssize_t n;

    if ((n = sio_puts(s)) < 0)
        sio_error("Sio_puts error");
    return n;
}
pid_t Fork(void)
{
    pid_t pid;

    if ((pid = fork()) < 0)
        unix_error("Fork error");
    return pid;
}

int pipe_flag;      /* flag to check if command line has pipeline*/
int pipe_signal;    /* used in parseline(). get 1 if buf[i] == '|'*/
sigjmp_buf jump;    /* used to put newline when ctrl-c occurs*/

/* Function prototypes */
void eval(char* cmdline);
int parseline(char* buf, char** argv);
int builtin_command(char** argv);
char* mystrchr(char* buf, int length);
void div_pipe(char** argv, char** arg1, char** arg2);
void mypipe(char** arg1, char** arg2);
void sigint_handler(int sig);

int main()
{
    char cmdline[MAXLINE]; /* Command line */

    while (1) {
        /* Read */
        signal(SIGINT, SIG_IGN);        //block ctrl+c signal
        if (!sigsetjmp(jump, 1)) {
            signal(SIGINT, sigint_handler);
        }
        printf("> ");
        Fgets(cmdline, MAXLINE, stdin);
        if (feof(stdin))
            exit(0);

        pipe_flag = 0;
        /* Evaluate */
        eval(cmdline);
    }
    return 0;
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char* cmdline)
{
    char* argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */
    char route1[100];    /*changed route1*/
    char route2[100];    /*changed route2*/

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);
    if (argv[0] == NULL)
        return;   /* Ignore empty lines */

    if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
        strcpy(route1, "/bin/");
        strcpy(route2, "/usr");
        argv[0] = strcat(route1, argv[0]);
        if ((pid = Fork()) == 0) {
            signal(SIGINT, SIG_DFL);        //unblock ctrl+c signal
            if (pipe_flag > 0) {            //if command has pipeline
                char* arg1[10] = { 0 };
                char* arg2[10] = { 0 };
                div_pipe(argv, arg1, arg2);
                mypipe(arg1, arg2);
            }
            else {
                if (execve(argv[0], argv, environ) < 0) {	//ex) /bin/ls ls -al &
                    argv[0] = strcat(route2, argv[0]);
                    if (execve(argv[0], argv, environ) < 0) {
                        printf("%s: main Command not found.\n", argv[0]);
                        exit(0);
                    }
                }
            }
        }
        if (!bg) {  /* Parent waits for foreground job to terminate */
            int status;
            if (waitpid(pid, &status, 0) < 0)
                unix_error("waitfg : waitpid error");
        }
        else//when there is background process!
            printf("%d %s", pid, cmdline);
    }
    return;
}
/* $end eval */

/* $begin builtin_command */
/* builtin_command - If first arg is a builtin command, run it and return true */
int builtin_command(char** argv)
{
    if (!strcmp(argv[0], "quit")) /* quit command */
        exit(0);
    if (!strcmp(argv[0], "exit")) /* exit command */
        exit(0);
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
        return 1;
    if (!strcmp(argv[0], "cd")) { /*change directory*/
        char* path;

        if (argv[1] == NULL) {
            if ((path = (char*)getenv("HOME")) == NULL) {
                path = ".";
            }
        }
        else {
            path = argv[1];
        }
        if (chdir(path) < 0) {
            printf("No such file or directory\n");
        }
        return 1;
    }
    return 0;                     /* Not a builtin command */
}
/* $end builtin_command */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char* buf, char** argv)
{
    char* delim;         /* Points to first space delimiter */
    int argc;            /* Number of args */
    int bg;              /* Background job? */
    int length;
    char temp;

    buf[strlen(buf) - 1] = ' ';  /* Replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    length = strlen(buf);
    pipe_signal = 0;
    while ((delim = mystrchr(buf, length))) {
        if (pipe_signal == 0) {
            argv[argc++] = buf;
            *delim = '\0';
            buf = delim + 1;
            while (*buf && (*buf == ' ')) { /* make spaces to '\0' and ignore */
                *buf = '\0';
                buf++;
            }
            length = strlen(buf);
        }
        else if (pipe_signal == 1) {
            argv[argc++] = buf;
            buf = delim + 1;
            while (*buf && (*buf == ' ')) { /* make spaces to '\0' and ignore */
                *buf = '\0';
                buf++;
            }
            length = strlen(buf);
            pipe_signal = 0;
        }

    }
    argv[argc] = NULL;
    if (argc == 0)  /* Ignore blank line */
        return 1;

    /* Should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0)
        argv[--argc] = NULL;

    return bg;
}
/* $end parseline */

/* $begin parseline */
/* mystrchr - if it finds specific symbols(' ', '\'', '\"', '|') return appropriate address */
char* mystrchr(char* buf, int length) {
    int i, j;

    for (i = 0; i < length; i++) {
        if (buf[i] == '\0') {           //if buf[i] == '\0', return NULL
            return NULL;
        }
        else if (buf[i] == ' ') {       //if buf[i] == ' ', return buf[i]
            return &buf[i];
        }
        else if (buf[i] == '\"') {  //if buf[i] == '\"', find another '\"' and change it to '\0'. Thus, move values left. return buf[j-1]
            for (j = i + 1; j < length; j++) {
                if (buf[j] == '\"') {
                    break;
                }
            }
            buf[j] = '\0';
            for (int k = i; k < length - 1; k++) {
                buf[k] = buf[k + 1];
            }
            return &buf[j - 1];
        }
        else if (buf[i] == '\'') {  //if buf[i] == '\'', find another '\'' and change it to '\0'. Thus, move values left. return buf[j-1]
            for (j = i + 1; j < length; j++) {
                if (buf[j] == '\'') {
                    break;
                }
            }
            buf[j] = '\0';
            for (int k = i; k < length - 1; k++) {
                buf[k] = buf[k + 1];
            }
            buf[length - 1] = ' ';
            return &buf[j - 1];
        }
        else if (buf[i] == '|') {   //if buf[i] == '|'
            if (buf[i - 1] != '\0') {   //if pipeline is stick to left command, move values right. and change with space. return buf[i]
                for (j = length - 1; j >= i; j--) {
                    buf[j + 1] = buf[j];
                }
                buf[i] = ' ';
                buf[length + 1] = '\0';
                return &buf[i];
            }
            else {                      //if buf[i] == '|' and not stick to left command, return buf[i] 
                pipe_flag++;
                pipe_signal = 1;
                return &buf[i];
            }
        }
    }
}
/* $end mystrchr */

/* $begin div_pipe */
/* div_pipe - divide command to left part and right part of pipeline */
void div_pipe(char** argv, char** arg1, char** arg2) {
    int i, j;

    for (i = 0; argv[i] != 0; i++) {    //check argv until meet NULL
        if (*argv[i] == '|') {          //if there is pipeline
            for (j = i + 1; argv[j] != 0; j++) {    //save right part of pipeline to arg2
                arg2[j - i - 1] = argv[j];
            }
            break;
        }
        arg1[i] = argv[i];      //save left part of pipeline to arg1
    }
}
/* $end div_pipe */

/* $begin mypipe */
/* mypipe - execute command when the command has pipeline */
void mypipe(char** arg1, char** arg2) {
    char* line1[10] = { 0 };
    char* line2[10] = { 0 };
    int fd[2];
    pid_t pid;

    if (pipe(fd) == -1) {
        printf("pipe error occured!\n");
    }       //make pipe
    if ((pid = Fork()) == 0) {
        close(fd[0]);       //unnecessary
        dup2(fd[1], 1);     //change stdout to fd[1](pipe entrance)
        if ((execvp(arg1[0], arg1)) < 0) {
            printf("%s: exec1 Command not found.\n", arg1[0]);
            exit(0);
        }
    }
    else {
        close(fd[1]);       //unnecessary
        dup2(fd[0], 0);     //change stdin to fd[0](pipe exit)
        div_pipe(arg2, line1, line2);
        if (line2[0] != 0) {        //if command has more pipeline
            mypipe(line1, line2);
        }
        else {      //there's no more pipeline
            if ((execvp(arg2[0], arg2)) < 0) {
                printf("%s: exec2 Command not found.\n", arg2[0]);
                exit(0);
            }
        }
    }
}
/* $end mypipe */

void sigint_handler(int sig) {
    Sio_puts("\n");
    siglongjmp(jump, 1);
}