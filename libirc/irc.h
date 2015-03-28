int runit(int fd,void (*line_handler)(),void (*extra_handler)()) ;
int ircConnect(char *serv,char *port,char *nick,char *user) ;
int serverConnect(char *serv,char *port) ;
char **line_cutter(int fd,char *line,struct user *user) ;
