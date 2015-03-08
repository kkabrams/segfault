#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <irc.h>

#define mywrite(a,b) write(a,b,strlen(b))

int *fds;
char **chans;

void extra_handler(int fd) {
 return;
}

struct user {
 char *nick;
 char *user;
 char *host;
};

void privmsg_others(int fd,char *msg) {
 int i;
 char tmp[512];
 for(i=0;fds[i] != -1;i++) {
  if(fds[i] != fd) {
   snprintf(tmp,sizeof(tmp)-1,"PRIVMSG %s :%s\r\n",chans[fdtoi(fds[i])],msg);
   write(fds[i],tmp,strlen(tmp));
  }
 }
}

void message_handler(int fd,char *from,struct user *user,char *line) {
 int i;
 char tmp[512];
 if(!strcmp(from,chans[fdtoi(fd)])) {//don't want to be forwarding PMs. :P
  snprintf(tmp,sizeof(tmp)-1,"<%s> %s",user->nick,line);
  privmsg_others(fd,tmp);
 }
}

void line_handler(int fd,char *line) {//this should be built into the libary?
 char *s=line,*t=0,*u=0;
 char tmp[512];
 struct user *user=malloc(sizeof(struct user));
 user->nick=0;
 user->user=0;
 user->host=0;
 if(strchr(line,'\r')) *strchr(line,'\r')=0;
 if(strchr(line,'\n')) *strchr(line,'\n')=0;
 printf("line: '%s'\n",line);
 if(line[0]==':') {
  if((user->nick=strchr(line,':'))) {
   *(user->nick)=0;
   (user->nick)++;
  }
 }
 if(user->nick) {
  if((s=strchr((user->nick),' '))) {
   *s=0;
   s++;
   if((t=strchr(s,' '))) {
    *t=0;
    t++;
    if((u=strchr(t,' '))) {//:
     *u=0;
     u++;
    }
   }
  }
  if(((user->user)=strchr((user->nick),'!'))) {
   *(user->user)=0;
   (user->user)++;
   if(((user->host)=strchr((user->user),'@'))) {
    *(user->host)=0;
    (user->host)++;
   }
  } else {
   user->host=user->nick;
  }
 }
 if(!user->user && s) {
  if(!strcmp(s,"004")) {
   snprintf(tmp,sizeof(tmp)-1,"JOIN %s\r\n",chans[fdtoi(fd)]);
   mywrite(fd,tmp);
  }
 }
 if(s && t && u) {
  if(!strcmp(s,"PRIVMSG")) {
   message_handler(fd,*t=='#'?t:user->nick,user,++u);
  }
 }
 if(s && user->nick && t) {
  if(!strcmp(s,"JOIN")) {
   snprintf(tmp,sizeof(tmp)-1,"%cACTION %s has joined %s%c",1,user->nick,t+(*t==':'),1);
   privmsg_others(fd,tmp);
   //send a join message to the other end.
  }
 }
 free(user);
}

int fdtoi(int fd) {
 int i;
 for(i=0;fds[i] != -1;i++) {
  if(fds[i] == fd) return i;
 }
 return -1;
}

int main(int argc,char *argv[]) {
 fds=malloc(sizeof(int) * (argc+3) / 3);
 chans=malloc(sizeof(char *) * (argc+3) / 3);
 int i=0;
 printf("%d\n",argc);
 for(i=0;((i*3)+3)<argc;i++) {
  printf("%d server: %s port: %s channel: %s\n",i,argv[(i*3)+1],argv[(i*3)+2],argv[(i*3)+3]);
  fds[i]=serverConnect(argv[(i*3)+1],argv[(i*3)+2]);
  chans[i]=strdup(argv[(i*3)+3]);
  mywrite(fds[i],"NICK link8239\r\nUSER a b c :d\r\n");
 }
 fds[i]=-1;
 //heh. you can write your own code for picking a different nick per server. fuck you.
 runem(fds,line_handler,extra_handler);
}
