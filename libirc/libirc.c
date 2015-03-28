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

int main(int argc,char *argv[]) {
 return 0;
}

#define SILLYLIMIT 256

int serverConnect(char *serv,char *port) {
 int rv;
 int fd=0;
 int n=1;
 int try_ipv4=0;
 char buf[SILLYLIMIT];
 struct addrinfo hints, *servinfo, *p=0;
 struct in_addr saddr;
 struct in6_addr saddr6;
 struct hostent *he;
 memset(&hints,0,sizeof hints);
 hints.ai_family=AF_INET;
 hints.ai_socktype=SOCK_STREAM;
 if((fd=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP)) < 0) {
  perror("socket");
  return -1;
 }
/*
 for(try_ipv4=0;try_ipv4 < 2;try_ipv4++) {
  if(!(he=gethostbyname2(
       try_ipv4
       ?inet_aton(serv,&saddr)
         ?inet_ntoa(saddr)
         :serv
       :inet_pton(AF_INET6,serv,&saddr6)
         ?inet_ntop(AF_INET6,&saddr6,buf,SILLYLIMIT)
         :serv
     ,try_ipv4?AF_INET:AF_INET6))) return -1;

  for(;*(he->h_addr_list);he->h_addr_list++) {
   printf("trying to connect to %s:%s attempt #%d\n",serv,port,n);
   n++;
*/
//   if((rv=getaddrinfo(he->h_addr_list,port,&hints,&servinfo)) != 0) {
   if((rv=getaddrinfo(serv,port,&hints,&servinfo)) != 0) {
    fprintf(stderr,"error resolving '%s'.\n",serv);
    return -1;
   }
   for(p=servinfo;p;p=p->ai_next) {
    if(connect(fd,p->ai_addr, p->ai_addrlen) < 0) {
     perror("connect");
     continue;
    } else {
     return fd;
    }
   }
   //printf("trying a differnt address...\n");
  //}
  //printf("trying a different AF...\n");
 //}
 //printf("well, shit. how'd I get here?\n");
 return -1;
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
  //if(!backlogs[i]) return 252;//wtf is this here for? ofc they're not set!
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
    if(extra_handler) extra_handler(fd);
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

struct user {
 char *nick;
 char *user;
 char *host;
};

//this function mangles the input.
//gotta free the returned pointer but not each pointer in the array.
char **line_cutter(int fd,char *line,struct user *user) {
 int i;
 char **a=malloc(16);//heh.
 memset(a,0,sizeof(char *)*16);
 if(!user) return 0;
 user->nick=0;
 user->user=0;
 user->host=0;

 if(strchr(line,'\r')) *strchr(line,'\r')=0;
 if(strchr(line,'\n')) *strchr(line,'\n')=0;
 if(line[0]==':') {
  if((user->nick=strchr(line,':'))) {
   *(user->nick)=0;
   (user->nick)++;
  }
 }
 if(user->nick) {
  if((a[0]=strchr((user->nick),' '))) {
   for(i=0;a[i+1]=strchr(a[i],' ') && i<15;i++) {
    *a[i]=0;
    a[i]++;
    if(a[i][0] == ':') {//we're done.
     a[i]++;
     break;
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
 return a;
}
