/** relay_plus.c

    multi process command relay for xv6-riscv with selective worker routing with the following feautures:
    (1) connected via spoke/star architecture where parent will communicate directly with worker
    (2) modes for selecting worker execution input by user
        * :all - process cmd through all workers
        * :first k - process cmd through first k workers only
        * :skip k - process cmd through all workers except worker k
    
    Author: Taylor Bauer
    COMS 3520 - Project 1A
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAXLINE 128
#define MAXWORKERS 3


void strip_newline(char *s) {
  int i = 0;
  while (s[i]) {
    if (s[i] == '\n') {
      s[i] = '\0';
      return;
    }
    i++;
  }
}

// for the worker process to read from the parents, append and write back to the parent
void worker(int id, int read_fd, int write_fd) {
  char buf[MAXLINE];

  while (1) {
    memset(buf, 0, sizeof(buf));

    if (read(read_fd, buf, sizeof(buf)) <= 0) {
      break;
    }

    // exit command check
    if (strcmp(buf, ":exit") == 0 || strcmp(buf, ":EXIT") == 0 ) {
        break;
    }

    // append worker ID
    char out[MAXLINE];
    memset(out, 0, sizeof(out));
    strcpy(out, buf);

    char suffix[8];
    memset(suffix, 0, sizeof(suffix));
    suffix[0] = ' ';
    suffix[1] = '[';
    suffix[2] = 'W';
    suffix[3] = '0' + id;
    suffix[4] = ']';
    suffix[5] = '\0';
    
    int len = strlen(out);
    strcpy(out + len, suffix);

    // write res back to parent
    write(write_fd, out, strlen(out) + 1);
  }

  close(read_fd);
  close(write_fd);
  exit(0);
}

/** for checking if string from user starts with a specific prefix
    and to match the mode commands given
*/
int string_begin(char *s, char *p) {
  while (*p) {
    if (*s != *p)
      return 0;
    s++;
    p++;
  }
  return 1;
}

/** for parsing the mode selected for worker execution
    returns the following vals for each mode
        0 for :all
        1 for :first k
        2 for :skip k
        -1 for an invalid mode
    m - mode, p - the param for k
*/
int worker_parse(char *m, int *p) {
    strip_newline(m);
    
    // check for all mode
    if (strcmp(m, ":all") == 0) {
        return 0;
    }
    // check for first k
    if (string_begin(m, ":first ")) {
        *p = atoi(m + 7);
        return 1;
    }
    // check for skip k
    if (string_begin(m, ":skip ")) {
        *p = atoi(m + 6);
        return 2;
    }
    // not valid mode input
    return -1;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(2, "Usage: relay_plus <num_workers>\n");
    exit(1);
  }

  int n = atoi(argv[1]);
  if (n <= 0 || n > MAXWORKERS) {
    fprintf(2, "Number of workers must be between 1 and %d\n", MAXWORKERS);
    exit(1);
  }

  /** pipeline for spoke/star structure 
        to - pipeline from parent to worker
        from - pipeline from worker to parent
  */
  int to[MAXWORKERS][2];
  int from[MAXWORKERS][2];

  // create the pipes
  for (int i = 0; i < n; i++) {
    pipe(to[i]);
    pipe(from[i]);
}

  for (int i = 0; i < n; i++) {
    int pid = fork();
    if (pid == 0) {
      // close unused worker process pipes
      for (int j = 0; j < n; j++) {
        if (j == i) {
            close(to[i][1]);
            close(from[i][0]);
        } else {
            close(to[j][0]);
            close(to[j][1]);
            close(from[j][0]);
            close(from[j][1]);
        }
      }

      // run worker with id
      worker(i + 1, to[i][0], from[i][1]);
    }
  }

  // Parent
  for (int i = 0; i < n; i++) {
    // close
    close(to[i][0]);
    close(from[i][1]);
  }

  char buf[MAXLINE];
  char m[MAXLINE];

  while (1) {
    printf("Enter command: ");
    memset(buf, 0, sizeof(buf));
    gets(buf, sizeof(buf));
    strip_newline(buf);
    
    // exit cmd check
    if (strcmp(buf, ":exit") == 0 || strcmp(buf, ":EXIT") == 0) {
        // termination to workers
        for (int i = 0; i < n; i++) {
            write(to[i][1], buf, strlen(buf) + 1);
      }
      break;
    }

    // get mode
    printf("Mode (:all | :first k | :skip k): ");
    memset(m, 0, sizeof(m));
    gets(m, sizeof(m));
    
    // parse mode and k
    int p = 0;
    int m_type = worker_parse(m, &p);

    // validate mode/commands
    if (m_type == -1) {
        printf("Error: invalid mode\n");
        continue;
    }
    // valid worker indices
    if (m_type == 1) {
        if (p <= 0 || p > n) {
            printf("Error: invalid worker count\n");
            continue;
      }
    }
    // validate non existent workers skip request indices
    if (m_type == 2) {
        if (p <= 0 || p > n) {
            printf("Error: worker %d does not exist\n", p);
            continue;
        }
    }

    // list workers indices ot be used
    int w[MAXWORKERS];
    // num workers to use
    int num = 0;

    // :all
    if (m_type == 0) {
      for (int i = 0; i < n; i++) {
        w[num++] = i;
      }
    // first :k
    } else if (m_type == 1) {  
      for (int i = 0; i < p; i++) {
        w[num++] = i;
      }
    // :skip k
    } else if (m_type == 2) {
      for (int i = 0; i < n; i++) {
        // skip worker k
        if (i != p - 1) {  
          w[num++] = i;
        }
      }
    }

    // process cmd with chosen workers
    char res[MAXLINE];
    strcpy(res, buf);
    // send cmd to each worker
    for (int i = 0; i < num; i++) {
      int x = w[i];
      // send res to worker & read that res from worker
      write(to[x][1], res, strlen(res) + 1);
      memset(res, 0, sizeof(res));
      read(from[x][0], res, sizeof(res));
    }
    printf("Result: %s\n", res);
  }

  // wait for workers to term
  for (int i = 0; i < n; i++) {
    wait(0);
  }

  // close any other pipes that are open
  for (int i = 0; i < n; i++) {
    close(to[i][1]);
    close(from[i][0]);
  }

  exit(0);
}