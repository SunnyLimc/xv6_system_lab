#include <kernel/types.h>
#include <user/user.h>

int main(int argc, int *argv[])
{
  if (argc > 1)
  {
    fprintf(2, "usage: prime\n");
    exit(1);
  }
  // global states
  int MAX_NUM = 35;

  // states machine
  // l -> read, r[0] -> next read, r[1] -> cur write
  // l -> x, r[0] -> next read, r[1] -> x
  // l -> r[0], pipe(r)
  int l;
  int r[2];
  pipe(r);

  // preprocess
  if (fork() == 0)
  {
    int first_num = 2;
    for (int num = 3; num <= MAX_NUM; num++)
      if (num % first_num != 0)
        write(r[1], &num, sizeof(int));
    exit(0);
  }
  close(r[1]);

  while (1)
  {
    // read first_num
    int first_num = 0;
    if (read(r[0], &first_num, sizeof(int)) == 0)
      break;
    printf("Prime: %d\n", first_num);
    // re-generate states
    // remove old states
    l = dup(r[0]);
    // ensure old r[0] is removed
    close(r[0]);
    pipe(r);
    // create child process
    if (fork() == 0)
    {
      int num = 0;
      while (read(l, &num, sizeof(int)) != 0)
        if (num % first_num != 0)
          write(r[1], &num, sizeof(int));
      exit(0);
    }
    // close the redundant fd in main()
    // only r[0] need to be kept
    close(l);
    close(r[1]);
  }

  exit(0);
}
