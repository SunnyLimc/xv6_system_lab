#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path)
{
  static char buf[DIRSIZ + 1];
  char *p;

  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // return original pointer and wait for discard
  if (strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
  return buf;
}

void recur_dir(char *path, char *m_name)
{
  char buf[512];
  // ensure the length of path is legal
  // dir and file is represented by a file structure
  // the first 1 is for /, and the second is for 0(EOF)
  if (sizeof(path) + 1 + DIRSIZ + 1 > sizeof(buf))
  {
    printf("usage: a long path may result in overflow, '%s' will be bypassed.\n", path);
    return;
  }
  // see kernel/fcntl.h for more
  int fd;
  if ((fd = open(path, 0)) < 0)
  {
    printf("usage: can not open path %s\n");
    return;
  }
  strcpy(buf, path); // path have a long length

  char *p;
  p = buf + strlen(buf);
  *p++ = '/';

  // provide a reliable result rather than a fast procedure, use double scan to solve problem
  struct dirent de; // the structure tha fd store
  struct stat st;   // the structure that fd store

  int count = 0;
  while (read(fd, &de, sizeof(de)) == sizeof(de))
  {
    // bypass 0 by default -> many system reserve 0 to indicate NULL or error
    // nodes are created in sequence and cannot be indentified based on their inode_num.
    if (de.inum == 0 || count++ <= 1)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if (stat(buf, &st) < 0)
      printf("find: can not stat %s\n", buf);
    switch (st.type)
    {
    case T_DIR:
      continue;
      break;
    case T_FILE:
      if (strcmp(fmtname(buf), m_name) == 0)
        printf("%s\n", buf);
      break;
    }
  }
  close(fd);

  count = 0;
  fd = open(path, 0);
  while (read(fd, &de, sizeof(de)) == sizeof(de))
  {
    if (de.inum == 0 || count++ <= 1)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    stat(buf, &st);
    switch (st.type)
    {
    case T_FILE:
      continue;
      break;
    case T_DIR:
      recur_dir(buf, m_name);
      break;
    }
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  // Brief:
  // keep subdir into somewhere
  // find this dir to see if something can be found
  if (argc < 2)
  {
    fprintf(2, "find: usage: find path pattern\n");
    exit(1);
  }
  if (strlen(argv[2]) > DIRSIZ)
  {
    fprintf(2, "find: pattern too long.\n");
    exit(1);
  }
  int fd;
  struct stat st;

  if ((fd = open(argv[0], 0)) < 0)
  {
    fprintf(2, "find: cannot open %s\n", argv[1]);
    exit(1);
  }
  if (fstat(fd, &st) < 0)
  {
    fprintf(2, "find: cannot stat %s\n", argv[1]);
    close(fd);
    exit(1);
  }

  char buf[512];
  memmove(buf, argv[2], strlen(argv[2]));
  memset(buf + strlen(argv[2]), ' ', DIRSIZ - strlen(argv[2]));
  buf[DIRSIZ] = 0;

  recur_dir(argv[1], buf);

  exit(0);
}
