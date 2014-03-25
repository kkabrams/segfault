#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

//#define DEBUG "epoch" //nick or channel to send debug info to.
#define CHUNK 16

static int serverConnect(char *serv,char *port) {
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

int runit(int fd,void (*line_handler)(),void (*extra_handler)()) {
 FILE *fp;
 fd_set master;
 fd_set readfs;
 struct timeval timeout;
 int fdmax,n,s,i;
 char *backlog=malloc(CHUNK+1);
 char *t,*line=0;
 int blsize=CHUNK;
 int bllen=0;
 char buffer[CHUNK];//THIS IS *NOT* NULL TERMINATED.
 if(!backlog) return 252;
 FD_ZERO(&master);
 FD_ZERO(&readfs);
 FD_SET(fd,&master);
 fdmax=fd;
 fp=fdopen(fd,"rw");
 memset(backlog,0,CHUNK);
 memset(buffer,0,CHUNK);
 if(fd) {
  int done=0;
  while(!done) {
   extra_handler(fd);
   readfs=master;
   timeout.tv_sec=0;
   timeout.tv_usec=1000;
   if( select(fdmax+1,&readfs,0,0,&timeout) == -1 ) {
    printf("\n!!!It is crashing here!!!\n\n");
    perror("select");
    return 1;
   }
   if(FD_ISSET(fd,&readfs)) {
    if((n=recv(fd,buffer,CHUNK,0)) <= 0) {//read CHUNK bytes
     fprintf(stderr,"recv: %d\n",n);
     perror("recv");
     return 2;
    } else {
     buffer[n]=0;//deff right.
     if(bllen+n >= blsize) {//this is probably off...
      blsize+=n;
      t=malloc(blsize);
      if(!t) {
       printf("OH FUCK! MALLOC FAILED!\n");
       exit(253);
      }
      memset(t,0,blsize);//optional?
      memcpy(t,backlog,blsize-n+1);//???
      free(backlog);
      backlog=t;
     }
     memcpy(backlog+bllen,buffer,n);
     bllen+=n;
     for(i=0,s=0;i<bllen;i++) {
      if(backlog[i]=='\n') {
       line=malloc(i-s+3);//on linux it crashes without the +1 +3? weird. when did I do that?
       if(!line) {
        printf("ANOTHER malloc error!\n");
        exit(254);
       }
       memcpy(line,backlog+s,i-s+2);
       line[i-s+1]=0;//gotta null terminate this. line_handler expects it.
       s=i+1;//the character after the newline.
       if(!strncmp(line,"PING",4)) {
        t=malloc(strlen(line));
        strcpy(t,"PONG ");
        strcat(t,line+6);
	write(fd,t,strlen(t));
        //fprintf(fp,"PONG %s",line+6);//a whole FILE * and fdopen JUST for this??? oy...
        //fflush(fp);
#ifdef DEBUG
        printf("%s\nPONG %s\n",line,line+6);
        write(fd,"PRIVMSG %s :PONG! w00t!\r\n",DEBUG,28);
#endif
       } else if(!strncmp(line,"ERROR",5)) {
#ifdef DEBUG
        printf("error: %s\n",line);
#endif
        return 0;
       } else {
        line_handler(fd,line);
       }
       free(line);
      }
     }
     //left shift the backlog so the last thing we got to is at the start
     if(s > bllen) { //if the ending position is after the size of the backlog...
      bllen=0;//fuck shifting. :P
     } else {
      for(i=s;i<=bllen;i++) {//should work.
       backlog[i-s]=backlog[i];
      }
      bllen-=s;
     }
    }
   }
  }
 }
 return 0;
}

//:hack.thebackupbox.net 433 * sysbot :Nickname is already in use.
//Need to have it check for this.
//and try a nick?
//or something...
//I don't want to add any string parsing to this function. :/
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

