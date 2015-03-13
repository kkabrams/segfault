#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

//#define DEBUG "epoch" //nick or channel to send debug info to.
#define CHUNK 4096

int serverConnect(char *serv,char *port) {
 struct addrinfo hints, *servinfo, *p;
 int rv;
 int fd=0;
 memset(&hints,0,sizeof hints);
 hints.ai_family=AF_INET;
 hints.ai_socktype=SOCK_STREAM;
 if((fd=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP)) < 0) {
  perror("socket");
  return fd;
 }
 if((rv=getaddrinfo(serv,port,&hints,&servinfo)) != 0) {
  fprintf(stderr,"error resolving '%s'.\n",serv);
  return 0;
 }
 for(p=servinfo;p;p=p->ai_next) {
  if (connect(fd,p->ai_addr, p->ai_addrlen) < 0) {
   perror("connect");
   continue;
  }
  break;
 }
 return p?fd:0;
}

int fdlen(int *fds) {
 int i;
 for(i=0;fds[i] != -1;i++);
 return i+1;
}

int runem(int *fds,void (*line_handler)(),void (*extra_handler)()) {
 int j;
 int fdl=fdlen(fds);
 fd_set master;
 fd_set readfs;
 struct timeval timeout;
 int fdmax=0,n,s,i;
 int fd;
 char *backlogs[fdl];
 char *t,*line=0;
 int blsize=CHUNK;
 int bllen=0;
 char buffers[fdl][CHUNK];//THIS IS *NOT* NULL TERMINATED.
 FD_ZERO(&master);
 FD_ZERO(&readfs);
 for(i=0;fds[i] != -1;i++) {
  if(!backlogs[i]) return 252;
  FD_SET(fds[i],&master);
  backlogs[i]=malloc(CHUNK+1);
  memset(backlogs[i],0,CHUNK);
  memset(buffers[i],0,CHUNK);
  fdmax=fds[i]>fdmax?fds[i]:fdmax;
 }
 int done=0;
 while(!done) {
  for(fd=0;fd<=fdmax;fd++) {
   if(FD_ISSET(fd,&master)) {
    extra_handler(fd);
   }
  }
  readfs=master;
  timeout.tv_sec=0;
  timeout.tv_usec=1000;
  if( select(fdmax+1,&readfs,0,0,&timeout) == -1 ) {
   printf("\n!!!It is crashing here!!!\n\n");
   perror("select");
   return 1;
  }
  for(i=0;fds[i] != -1;i++) {
   if(FD_ISSET(fds[i],&readfs)) {
    if((n=recv(fds[i],buffers[i],CHUNK,0)) <= 0) {//read CHUNK bytes
     fprintf(stderr,"recv: %d\n",n);
     perror("recv");
     return 2;
    } else {
     buffers[i][n]=0;//deff right.
     if(bllen+n >= blsize) {//this is probably off...
      blsize+=n;
      t=malloc(blsize);
      if(!t) {
       printf("OH FUCK! MALLOC FAILED!\n");
       exit(253);
      }
      memset(t,0,blsize);//optional?
      memcpy(t,backlogs[i],blsize-n+1);//???
      free(backlogs[i]);
      backlogs[i]=t;
     }
     memcpy(backlogs[i]+bllen,buffers[i],n);
     bllen+=n;
     for(j=0,s=0;j<bllen;j++) {
      if(backlogs[i][j]=='\n') {
       line=malloc(j-s+3);//on linux it crashes without the +1 +3? weird. when did I do that?
       if(!line) {
        printf("ANOTHER malloc error!\n");
        exit(254);
       }
       memcpy(line,backlogs[i]+s,j-s+2);
       line[j-s+1]=0;//gotta null terminate this. line_handler expects it .
       s=j+1;//the character after the newline.
       if(!strncmp(line,"PING",4)) {
        t=malloc(strlen(line));
        strcpy(t,"PONG ");
        strcat(t,line+6);
        write(fds[i],t,strlen(t));
 #ifdef DEBUG
        printf("%s\nPONG %s\n",line,line+6);
        write(fds[i],"PRIVMSG %s :PONG! w00t!\r\n",DEBUG,28);
 #endif
       } else if(!strncmp(line,"ERROR",5)) {
 #ifdef DEBUG
        printf("error: %s\n",line);
 #endif
        return 0;
       } else {
        line_handler(fds[i],line);
       }
       free(line);
      }
     }
     //left shift the backlog so the last thing we got to is at the start
     if(s > bllen) { //if the ending position is after the size of the backlog...
      bllen=0;//fuck shifting. :P
     } else {
      for(j=s;j<=bllen;j++) {//should work.
       backlogs[i][j-s]=backlogs[i][j];
      }
      bllen-=s;
     }
    }
   }
  }
 }
 return 0;
}

//wrap runem to keep runit around :P
int runit(int fd,void (*line_handler)(),void (*extra_handler)()) {
 int fds[2];
 fds[0]=fd;
 fds[1]=-1;
 return runem(fds,line_handler,extra_handler);
}

//not needed?
int ircConnect(char *serv,char *port,char *nick,char *user) {
 char sendstr[1024];
 int fd;
 fd=serverConnect(serv,port);
 if(!fd) {
  return 0;
 }
 snprintf(sendstr,sizeof(sendstr)-1,"NICK %s\r\nUSER %s\r\n",nick,user);
 write(fd,sendstr,strlen(sendstr));
 return fd;
}
