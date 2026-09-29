#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

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

/*
 * creates and runs the desired process
 *
 * args: takes in an array of args
 * line: passed in to free from child processes
 * returns the error code
 */
int execute_args(char **args, char *line) {

  

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
    if (execvp(args[0], args) == -1) {
      free(line);
      free(args);
      exit(127);
    }
  }
  return -1;
}

/*
 * main execution loop of the shell
 * no args are needed and it returns nothing
 */
// TODO: Change all functions to use cmds instead of args, change free() too
void sh_loop() {
  while (true) {
    char *line = NULL;
    char **args = NULL;
    int nread;

    // Output the prompt
    // env can be set up here later for custom prompt
    const char *user = getenv("USER");
    const char *hostname = getenv("HOSTNAME");
    printf("\n%s@%s\n> ", user, hostname);

    // handle reading failures
    // nread returns num chars on success
    if ((nread = read_line(&line)) == -1) {
      free(line);
      continue;
    };

    // populating `args` using the line that is read in from stdin
    if ((char **args = parse_line(line)) == NULL) {
      perror("Parsing Error");
      free(line);
      free(args);
      continue;
    }

    // Replace pipes with NULL and return pointers to each separate cmd
    char ***cmds;
    if ((cmds = chop_args(args)) == NULL) {
      perror("Piping Error");
      free(line);
      free(args);
      continue;
    }


    // Skip blank commands
    if (args[0] == NULL) {
      free(line);
      free(args);
      continue;
    } 

    // Run built-in exit command
    if (strcmp(args[0], "exit") == 0) {
      free(line);
      free(args);
      break;
    }

    // TODO: move built-ins to after exec so they can be piped if needed
    // Run built-in cd command
    if (strcmp(args[0], "cd") == 0) {
      if (args[1] == NULL) {
        char *home = getenv("HOME");
        if (home == NULL) {
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
      free(line);
      free(args);
      continue;
    }
    
    // create and run the desired process (fork-exec)
    int estatus;
    estatus = execute_args(args, line);

    switch (estatus) {
      case -1:
        perror("Execution Error");
        break;

      case 127:
        fprintf(stderr, "%s: command not found\n", args[0]); 
        break;
    }
    free(line);
    free(args);
  }  
}

int main() {

  // Initialization Stuff -- config, default execution
  sh_loop();
  // Cleanup -- kill relevant ps
}
