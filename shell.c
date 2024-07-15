#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT_SIZE     1024
#define MAX_TOKEN_SIZE     64
#define MAX_NUM_TOKENS     64
#define MAX_NUM_FG_PROC    64

enum exec_modes {
	BACKGND_EXEC     = 1,
	FOREGND_SEQ_EXEC = 2,
	FOREGND_PAR_EXEC = 3
};

pid_t backgnd_pgid = 0;
pid_t foregnd_pgid = 0;

/**
 * @brief Splits the string by space and returns an array of tokens.
 *
 * @param line The string to tokenize.
 * @returns The tokens as a null-terminated array of null-terminated
 *          character strings.
 */
char **tokenize(char *line)
{
	char **tokens  = (char **) malloc(MAX_NUM_TOKENS * sizeof(char *));
	char *token    = (char *)  malloc(MAX_TOKEN_SIZE * sizeof(char));
	int  token_idx = 0;
	int  token_num = 0;

	for (int i = 0; i < strlen(line); i++) {
		char ch = line[i];
		if (ch == ' ' || ch == '\n' || ch == '\t') {
			token[token_idx] = '\0';
			if (token_idx) {
				tokens[token_num] = (char *) malloc(
					MAX_TOKEN_SIZE * sizeof(char));
				strcpy(tokens[token_num++], token);
				token_idx = 0;
			}
		} else {
			token[token_idx++] = ch;
		}
	}

	free(token);
	tokens[token_num] = NULL;
	return tokens;
}

/// @brief Handler for SIGINT.
void sigint_handler()
{
	kill(-foregnd_pgid, SIGTERM);
	printf("\n");
}

/**
 * @brief Frees memory allocated for tokens.
 * @param tokens The tokens array.
 */
void free_tokens(char **tokens)
{
	for (int i = 0; tokens[i] != NULL; i++)
		free(tokens[i]);
	free(tokens);
}

/**
 * @brief Forks the current process, executes a command in the child process,
 *        and sets its group ID to `*pgid` if it is non-zero, else its PID.
 *
 * @param tokens An array of pointers to null terminated strings representing
 *               the command to execute followed by its arguments.
 * @param idx    The index in the `tokens` array where the command is located.
 * @param pgid   Pointer to the group ID. If group ID is 0, it is set to child's
 *               PID. If it is non-zero, it is the process group ID of the
 *               child.
 * @return       On success, returns pid and assigns that to `*pgid` if
 *               `*pgid == 0`. On failure, returns -1. If the command was cd,
 *               returns 0.
 */
pid_t fork_and_exec(char **tokens, int idx, pid_t *pgid)
{
	if (!strcmp(tokens[idx], "cd")) {
		chdir(tokens[idx + 1]);
		return 0;
	}
	pid_t pid = fork();
	switch (pid) {
	case -1:
		perror("Error");
		return -1;
	case 0:
		setpgid(0, *pgid);
		execvp(tokens[idx], tokens + idx);
		exit(EXIT_FAILURE);
	default:
		if (!(*pgid))
			*pgid = pid;
	}
	return pid;
}

int main()
{
	signal(SIGINT, sigint_handler);

	char line[MAX_INPUT_SIZE];
	char **tokens;
	int commands[MAX_NUM_FG_PROC];

	puts("Welcome!");
	while (1) {
		// Release resources of terminated background processes
		waitpid(-1, NULL, WNOHANG);

		bzero(line, sizeof(line));
		printf("$ ");
		scanf("%[^\n]", line);
		// exit on ^D (EOF)
		if (feof(stdin))
			goto exit;
		getchar();

		line[strlen(line)] = '\n';
		tokens = tokenize(line);

		if (!tokens[0]) {
			free_tokens(tokens);
			continue;
		}

		if (!strcmp(tokens[0], "exit")) {
			free_tokens(tokens);
exit:
			if (backgnd_pgid) {
				kill(-backgnd_pgid, SIGKILL);
				wait(NULL);
			}
			puts("exit");
			exit(EXIT_SUCCESS);
		}

		// It is assumed that each input has only one kind of
		// "&", "&&", or "&&&".
		int num_foregnd_proc = 0;
		commands[num_foregnd_proc++] = 0;
		int exec_mode = 0;
		for (int i = 0; tokens[i] != NULL; i++) {
			if (tokens[i][0] == '&') {
				commands[num_foregnd_proc++] = i + 1;
				switch (strlen(tokens[i])) {
				case 1:
					exec_mode = BACKGND_EXEC;
					break;
				case 2:
					exec_mode = FOREGND_SEQ_EXEC;
					break;
				case 3:
					exec_mode = FOREGND_PAR_EXEC;
					break;
				}
				tokens[i] = NULL;
			}
		}

		pid_t child_pids[num_foregnd_proc];
		switch (exec_mode) {
		case BACKGND_EXEC:
			fork_and_exec(tokens, 0, &backgnd_pgid);
			break;
		case FOREGND_SEQ_EXEC:
			for (int i = 0; i < num_foregnd_proc; i++) {
				int idx = commands[i];
				pid_t pid = fork_and_exec(tokens, idx,
				                          &foregnd_pgid);
				if (pid > 0)
					waitpid(pid, NULL, 0);
			}
			break;
		case FOREGND_PAR_EXEC:
			for (int i = 0; i < num_foregnd_proc; i++) {
				int idx = commands[i];
				child_pids[i] = fork_and_exec(tokens, idx,
				                              &foregnd_pgid);
			}
			for (int i = 0; i < num_foregnd_proc; i++) {
				if (child_pids[i] > 0)
					waitpid(child_pids[i], NULL, 0);
			}
			break;
		default:
			fork_and_exec(tokens, 0, &foregnd_pgid);
			wait(NULL);
		}

		free_tokens(tokens);
	}
}
