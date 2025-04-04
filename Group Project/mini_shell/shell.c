#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 100
#define HISTORY_FILE "history.txt"

void handle_sigint(int sig) {
    write(STDOUT_FILENO, "\nCaught CTRL+C\nsh> ", 20);
}

void save_history(const char *cmd) {
    FILE *file = fopen(HISTORY_FILE, "a");
    if (file) {
        fprintf(file, "%s\n", cmd);
        fclose(file);
    }
}

void trim_newline(char *str) {
    str[strcspn(str, "\n")] = 0;
}

void exec_cmd(char *cmd);

void execute_piped(char *cmds[], int n) {
    int i;
    int pipefd[2], in_fd = 0;

    for (i = 0; i < n; i++) {
        pipe(pipefd);
        pid_t pid = fork();
        if (pid == 0) {
            dup2(in_fd, 0);
            if (i < n - 1)
                dup2(pipefd[1], 1);
            close(pipefd[0]);
            char *argv[MAX_ARGS];
            char *token = strtok(cmds[i], " ");
            int j = 0;
            while (token) {
                argv[j++] = token;
                token = strtok(NULL, " ");
            }
            argv[j] = NULL;
            execvp(argv[0], argv);
            perror("execvp failed");
            exit(1);
        } else {
            wait(NULL);
            close(pipefd[1]);
            in_fd = pipefd[0];
        }
    }
}

void handle_redirection(char *cmd) {
    int in = -1, out = -1, append = 0;
    char *input = strchr(cmd, '<');
    char *output = strstr(cmd, ">>") ? strstr(cmd, ">>") : strchr(cmd, '>');

    if (input) {
        *input = 0;
        input++;
        input = strtok(input, " \t");
        in = open(input, O_RDONLY);
        if (in < 0) {
            perror("Input file error");
            return;
        }
    }

    if (output) {
        append = strstr(cmd, ">>") != NULL;
        *output = 0;
        output += append ? 2 : 1;
        output = strtok(output, " \t");
        out = open(output, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
        if (out < 0) {
            perror("Output file error");
            return;
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (in != -1) dup2(in, STDIN_FILENO);
        if (out != -1) dup2(out, STDOUT_FILENO);
        char *argv[MAX_ARGS];
        char *token = strtok(cmd, " ");
        int i = 0;
        while (token) {
            argv[i++] = token;
            token = strtok(NULL, " ");
        }
        argv[i] = NULL;
        execvp(argv[0], argv);
        perror("execvp");
        exit(1);
    } else {
        wait(NULL);
    }
    if (in != -1) close(in);
    if (out != -1) close(out);
}

void exec_cmd(char *cmd) {
    if (strchr(cmd, '|')) {
        char *cmds[10];
        int n = 0;
        char *token = strtok(cmd, "|");
        while (token) {
            cmds[n++] = token;
            token = strtok(NULL, "|");
        }
        execute_piped(cmds, n);
    } else if (strchr(cmd, '<') || strchr(cmd, '>')) {
        handle_redirection(cmd);
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            char *argv[MAX_ARGS];
            char *token = strtok(cmd, " ");
            int i = 0;
            while (token) {
                argv[i++] = token;
                token = strtok(NULL, " ");
            }
            argv[i] = NULL;
            execvp(argv[0], argv);
            perror("execvp");
            exit(1);
        } else {
            wait(NULL);
        }
    }
}

void parse_and_execute(char *line) {
    char *commands[100];
    int n = 0;
    char *token = strtok(line, ";");
    while (token) {
        commands[n++] = token;
        token = strtok(NULL, ";");
    }

    for (int i = 0; i < n; i++) {
        char *seq_cmds[100];
        int s = 0;
        token = strtok(commands[i], "&&");
        while (token) {
            seq_cmds[s++] = token;
            token = strtok(NULL, "&&");
        }

        int success = 1;
        for (int j = 0; j < s && success; j++) {
            trim_newline(seq_cmds[j]);
            if (strlen(seq_cmds[j]) == 0) continue;
            save_history(seq_cmds[j]);
            pid_t pid = fork();
            if (pid == 0) {
                exec_cmd(seq_cmds[j]);
                exit(0);
            } else {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                    success = 0;
            }
        }
    }
}

int main() {
    signal(SIGINT, handle_sigint);
    char line[MAX_CMD_LEN];
    while (1) {
        printf("sh> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strcmp(line, "\n") == 0) continue;
        trim_newline(line);
        if (strcmp(line, "exit") == 0) break;
        parse_and_execute(line);
    }
    return 0;
}
