#include <unistd.h>
#include <string.h>

#define mywrite(a,b) write(a,b,strlen(b))

void c_hack(int fd,char *from,char *line,...) {
  mywrite(fd,"PRIVMSG #cmd :lol. this is in a shared library.\r\n");
}
