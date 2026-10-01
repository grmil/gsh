#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define READ 0
#define WRITE 1
#define HISTORY_MAX 100

static char *history[HISTORY_MAX];
static int history_curr = 0;

void child_clean(char *line, char **args, char ***cmds, char **clean_cmd, char *line_copy) {
  free(line);
  free(args);
  free(cmds);
  free(clean_cmd);
  free(line_copy);
  for (int i = 0; i < HISTORY_MAX; ++i) {
    free(history[i]);
  }
}

void history_add(const char *line) {
  if (line == NULL) return;

  char *copy = strdup(line);
  if (copy == NULL) {
    perror("strdup");
    return;
  }

  // Trim trailing whitespace specifically the newline
  size_t len = strlen(copy);
  while (len > 0 && isspace((unsigned char)copy[len - 1])) {
    len--;
  }
  copy[len] = '\0';

  // Don't store blank lines
  if (len == 0) {
    free(copy);
    return;
  }

  free(history[history_curr]);
  history[history_curr] = copy;
  history_curr = (history_curr + 1) % HISTORY_MAX;
}

void history_cmd() {
  for (int i = 0; i < HISTORY_MAX; i++) {
    int index = (history_curr + i) % HISTORY_MAX;
    if (history[index] == NULL)
      continue;
    printf("[%d] %s\n", HISTORY_MAX - i, history[index]); 
  }
}

char *history_get(int val) {
  if (val < 1 || val > HISTORY_MAX) return NULL;

  int index = (history_curr + HISTORY_MAX - val) % HISTORY_MAX;
  return history[index];
}

/*
 * reads in a dynamically allocated line from stdin
 *
 * line: where to store the string read in
 * returns the number of characters read in or -1 if an error
 */
int read_line(char **line) {
    *line = NULL;
    size_t size = 0;
    ssize_t nread;

    // getline allocates reallocates instead of using a set size
    if ((nread = getline(line, &size, stdin)) == -1) {
      return -1;
    }

    return (int) nread;
  }

/*
 * separates args in a string by space chars
 *
 * line: string that is read in from stdin
 * returns a pointer to a character array
 */
char **parse_line(char *line) {
    size_t size = 8;
    char **args = malloc(size * sizeof(char *));
    if (args == NULL) return NULL;

    size_t i = 0;
    char *token = strtok(line, " \t\n");
    while (token != NULL) {
        // Using i+1 here prevents having to realloc for the NULL character
        if (i + 1 >= size) {
            size *= 2;
            char **tmp = realloc(args, size * sizeof(char *));
            if (tmp == NULL) { free(args); return NULL; }
            args = tmp;
        }
        args[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
    return args;
}

char ***chop_args(char **args) {
  int num_cmds = 1;
  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "|") == 0) {
      num_cmds++;
    }
  }

  // +1 slot for a NULL terminator so the caller can find the count
  char ***cmds = malloc((num_cmds + 1) * sizeof(char **));
  if (cmds == NULL) {
    return NULL;
  }

  int curr = 0;
  cmds[curr++] = &args[0];
  for (int k = 0; args[k] != NULL; k++) {
    // Replace pipes with NULL and point to the cmd
    if (strcmp(args[k], "|") == 0) {
      args[k] = NULL;
      cmds[curr++] = &args[k + 1];
    }
  }
  cmds[curr] = NULL;

  return cmds;
}

// This func is meant to be run inside a child function setup redirects
char** parse_redirects(char** cmd) {
  char *infile = NULL;
  char *outfile = NULL;
  int force = 0;

  // Allocate the size for all tokens plus a NULL so we always have enough
  int n = 0;
  while (cmd[n] != NULL) n++;
  char **clean_cmd = malloc((n + 1) * sizeof(char *));
  if (clean_cmd == NULL) {
    perror("malloc");
    return NULL;
  }

  int argc = 0;
  int seen_redirect = 0;

  int i = 0;
  while (cmd[i] != NULL) {
    if (strcmp(cmd[i], "<") == 0) {
      if (cmd[i + 1] == NULL) {
        fprintf(stderr, "syntax error: missing filename after <\n");
        free(clean_cmd);
        return NULL;
      }
      infile = cmd[i + 1];
      seen_redirect = 1;
      i++;

    } else if (strcmp(cmd[i], ">") == 0) {
      if (cmd[i + 1] == NULL) {
        fprintf(stderr, "syntax error: missing filename after >\n");
        free(clean_cmd);
        return NULL;
      }
      outfile = cmd[i + 1];
      seen_redirect = 1;
      i++;

    } else if (strcmp(cmd[i], ">!") == 0) {
      if (cmd[i + 1] == NULL) {
        fprintf(stderr, "syntax error: missing filename after >!\n");
        free(clean_cmd);
        return NULL;
      }
      outfile = cmd[i + 1];
      force = 1;
      seen_redirect = 1;
      i++;

    } else if (!seen_redirect) {
      clean_cmd[argc++] = cmd[i];
    }
    i++;
  }
  clean_cmd[argc] = NULL;

  if (argc == 0) {
    fprintf(stderr, "syntax error: missing command\n");
    free(clean_cmd);
    return NULL;
  }

  if (infile != NULL) {
    int fd = open(infile, O_RDONLY);
    if (fd < 0) {
      perror(infile);
      free(clean_cmd);
      return NULL;
    }

    dup2(fd, STDIN_FILENO);
    close(fd);
  }

  if (outfile != NULL) {
    if (force == 0) {
      if (access(outfile, F_OK) == 0) {
        fprintf(stderr, "%s: file exists, use `>!' instead\n", outfile);
        free(clean_cmd);
        return NULL;
      }
    }
    int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      perror(outfile);
      free(clean_cmd);
      return NULL;
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
  }

  return clean_cmd;
}

int execute_single(char **args, char *line, char ***cmds, char *line_copy) {
  pid_t pid;

  // create child process
  pid = fork();

  if (pid < 0) {
    return -1;

  } else if (pid > 0) {
    // parent execution
    int wstatus;
    waitpid(pid, &wstatus, 0);

    // macros to decode wstatus
    if (WIFEXITED(wstatus)) {
      return WEXITSTATUS(wstatus);
    }

  } else if (pid == 0) {
    // child execution
    char **clean_cmd = parse_redirects(args);
    if (clean_cmd == NULL) exit(1);

    if (strcmp(clean_cmd[0], "history") == 0) {
      history_cmd();
      child_clean(line, args, cmds, clean_cmd, line_copy);
      exit(0);

    } else if (execvp(clean_cmd[0], clean_cmd) == -1) {
      child_clean(line, args, cmds, clean_cmd, line_copy);
      exit(127);
    }
  }
  return -1;
}

int execute_pipe(char ***cmds, char **args, char *line, int n, char *line_copy) {
  int prev = -1;

  if (n == 0) return -1;
  pid_t pids[n];

  // Go through each of the cmds available
  for (int i = 0; i < n; ++i) {
    int fd[2] = {-1, -1};

    if (i < n - 1 && pipe(fd) < 0) {
      perror("Pipe Error");
      return -1;
    }

    pid_t pid;
    if ((pid = fork()) < 0) {
      perror("Fork Failed");
      return -1;
    }

    // Set the curr process to read from previous pipe if possible
    if (pid == 0) {
      if (prev != -1) {
        dup2(prev, STDIN_FILENO);
        close(prev);
      }

      // Set the curr process to write to the next pipe if possible
      if (i < n - 1) {
        dup2(fd[WRITE], STDOUT_FILENO);
        close(fd[READ]);
        close(fd[WRITE]);
      }

      // Execute the child process
      char **clean_cmd = parse_redirects(cmds[i]);
      if (clean_cmd == NULL) _exit(1);

      if (strcmp(clean_cmd[0], "history") == 0) {
        history_cmd();
        child_clean(line, args, cmds, clean_cmd, line_copy);
        exit(0);

      } else if (execvp(clean_cmd[0], clean_cmd) == -1) {
        child_clean(line, args, cmds, clean_cmd, line_copy);
        perror("Exec failed");
        exit(127);
      }
    }

    // Parent process
    pids[i] = pid;
    if (prev != -1) {
      close(prev);
    }

    if (i < n - 1) {
      close(fd[WRITE]);
      prev = fd[READ]; // Save the current read end of pipe for the next
                       // process to use
    }

  }

  // Wait for all children to finish, in no particular order
  int status = 0;
  for (int i = 0; i < n; ++i) {
    int s;
    waitpid(pids[i], &s, 0);
    if (!WIFEXITED(s) || WEXITSTATUS(s) != 0) status = 1;
  }
  return status;
}

/*
 * main execution loop of the shell
 * no args are needed and it returns nothing
 */
// TODO: Change all functions to use cmds instead of args, change free() too
void sh_loop() {
  while (1) {
    // Output the prompt
    // env can be set up here later for custom prompt
    const char *user = getenv("USER");
    const char *hostname = getenv("HOSTNAME");
    printf("\n%s@%s\n> ", user, hostname);

    // handle reading failures
    // nread returns num chars on success
    int nread;
    char *line = NULL;
    if ((nread = read_line(&line)) == -1) {
      free(line);
      break;
    };
    
    // Rerun cmds from history using the ! operator and an integer
    if (line[0] == '!') {
      char *prev_cmd = history_get(atoi(line + 1) + 1);
      if (prev_cmd == NULL) {
        fprintf(stderr, "history: no such entry\n");
        free(line);
        continue;
      }

      free(line);
      line = strdup(prev_cmd);
      if (line == NULL) {
        perror("strdup");
        continue;
      }
    }

    // Keep a copy of the line for saving to history
    char *line_copy = strdup(line);
    if (line_copy == NULL) {
      free(line);
      perror("strdup");
      continue;
    }

    // populating `args` using the line that is read in from stdin
    char **args = NULL;
    if ((args = parse_line(line)) == NULL) {
      perror("Parsing Error");
      free(line);
      free(args);
      free(line_copy);
      continue;
    }

    // Replace pipes with NULL and return pointers to each separate cmd
    char ***cmds;
    if ((cmds = chop_args(args)) == NULL) {
      perror("Piping Error");
      free(args);
      free(line);
      free(line_copy);
      continue;
    }

    // Skip blank commands
    if (args[0] == NULL) {
      free(cmds);
      free(line);
      free(args);
      free(line_copy);
      continue;
    } 
    // TODO: do this but for each item in cmds

    // Run built-in exit command
    if (strcmp(args[0], "exit") == 0) {
      free(cmds);
      free(line);
      free(args);
      history_add(line_copy);
      free(line_copy);
      break;
    }

    // Run built-in cd command
    if (strcmp(args[0], "cd") == 0) {
      if (args[1] == NULL) {
        char *home = getenv("HOME");
        if (home == NULL) {
          free(cmds);
          free(line);
          free(args);
          free(line_copy);
          fprintf(stderr, "cd: HOME not set\n");
          continue;
        }
        if (chdir(home) != 0) {
          perror("cd");
        }
      } else {
        if (chdir(args[1]) != 0) {
          perror("cd");
        }
      }
      free(cmds);
      free(line);
      free(args);
      history_add(line_copy);
      free(line_copy);
      continue;
    }
    
    // create and run the desired process (fork-exec)
    
    int n = 0;
    while (cmds[n] != NULL) n++;

    // Use a single or piped setup
    int estatus;
    if (n == 1) {
      estatus = execute_single(args, line, cmds, line_copy);
    } else {
      estatus = execute_pipe(cmds, args, line, n, line_copy);
    }

    switch (estatus) {
      case -1:
        perror("Execution Error");
        break;

      case 127:
        fprintf(stderr, "%s: command not found\n", args[0]); 
        break;
    }

    // Add to history only on a complete success
    if (estatus == 0) {
      history_add(line_copy);
    }

    free(line_copy);
    free(line);
    free(args);
    free(cmds);
  }  
}

int main() {
  // Put Initialization Stuff Here -- config, default execution
  sh_loop();
  for (int i = 0; i < HISTORY_MAX; ++i) {
    free(history[i]);
  }
}
