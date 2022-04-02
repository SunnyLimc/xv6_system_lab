#include <kernel/types.h>
#include <user/user.h>

int main(int argc, char *argv[])
{
  int p[2];
  pipe(p);
  if (argc > 1)
  {
    fprintf(2, "usage: pingpong\n");
    exit(1);
  }
  if (fork() == 0) // child
  {
    int puid;
    read(p[0], &puid, sizeof(int *));
    printf("%d: received ping\n", puid);
    int pid = getpid();
    write(p[1], &pid, sizeof(int *));
  }
  else // parent
  {
    int pid = getpid();
    write(p[1], &pid, sizeof(int *));
    int cuid;
    read(p[0], &cuid, sizeof(int *));
    printf("%d: received pong\n", cuid);
  }
  exit(0);
}
