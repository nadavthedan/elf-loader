#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
  if (argc < 2)
    return 1;

  int fd = open(argv[1], O_RDONLY);
  if (fd < 0)
    return 1;

  char buf[1];
  while (read(fd, buf, 1) > 0)
    ;

  close(fd);

  write(1, "file was fully read succesfully!\n", 33);

  return 0;
}
