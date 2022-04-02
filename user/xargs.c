#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

// we must use **argv or *argv[] because the length of inner array is unknown
// use [] will automatically de-reference and is the same as to use *(p + count)
int main(int argc, char *argv[])
{
  char p;
  char buf[512];
  char *param[MAXARG] = {0};
  // argv[n] is just a pointer to string
  for (int i = 1; i <= argc; i++)
    param[i - 1] = argv[i];
  //! if we need to use char *, we must give it enough memory
  param[argc - 1] = malloc(512 * sizeof(char));
  // 'xargs' has occupied one place of argv. SO we can simply replace it to get a larger array.
  // re-use memmove() is deprecated due to it can not provide enough space to store
  // - new parameters and (null)
  // ! we need to noticed that we can not memmove() the address that point to the index of array, we need to move a de-reference object
  int count = 0;
  // actually, we don't need to read() to buffer

  // fd for test
  // int fd[2];
  // pipe(fd);
  // write(fd[1], "haha 1\n 2\n 3\n", 13);
  // close(fd[1]);

  //!! you can not treat stdin(1) as a pipe to write()
  // while (read(fd[0], &p, 1) != 0)
  while (read(0, &p, 1) != 0)
  {
    buf[count++] = p;
    if (p == '\n')
    {
      // ! we must match the index (count - 1) to where the '\n' was judged (p)
      buf[count - 1] = 0;
      strcpy(param[argc - 1], buf);
      param[argc] = 0;
      // for (int i = 0; i <= argc; i++)
      //   printf("test: %s ", param[i]);
      // printf("\n");
      if (fork() == 0)
      {
        exec(param[0], param);
        // if exec() success, following code would not be execuated
        exit(0);
      }
      // use fork() to ensure main process not exit
      // use wait() to ensure print in order
      wait(0);
      count = 0;
    }
  }
  exit(0);
}
