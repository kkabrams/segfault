#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <sys/resource.h>

#include "libirc/irc.h" //epoch's libirc. should be included with segfault.
#include "libhashtable/hashtable.h" //epoch's also.

//might want to change some of these.
#define TSIZE			65535
#define SERVER			"127.0.0.1"
#define PORT			"6667"
#define NICK			"SegFault" //override with argv[1]
#define MYUSER			"segfault"
#define LINES_SENT_LIMIT	1
#define LINELEN			400
#define SEGHOMELEN		1024
#define RAWLOG			"./files/rawlog"
#define LOG			"./files/log"
#define MAXTAILS		maxtails //just to have it more than the system default.
#define BS 4096
// !c uses 56 for its tail.
// 56 == 32 + 16 + 8 == 0x38 == 0x20+0x10+0x8 == SPAM | BEGIN | MSG
#define TAILO_RAW    1  // r output gets sent directly to server
#define TAILO_EVAL   2  // e interpret the lines read from the tail as if they were messages to segfault
#define TAILO_CLOSE  4  // c close the file at EOF, default is to leave it open.
#define TAILO_MSG    8  // m output gets sent as a PM to the place the tail was made.
#define TAILO_BEGIN  16 // b start the tail at the beginning of the file instead of the end.
#define TAILO_SPAM   32 // s Spam control is enabled for this stream.
#define TAILO_ENDMSG 64 // n show a message when the tail reaches the end of a chunk
#define TAILO_FORMAT 128// f formatting?
#define TAILO_Q_EVAL (TAILO_EVAL|TAILO_CLOSE|TAILO_BEGIN) //0x2+0x4+0x10 = 2+4+16  = 22
#define TAILO_Q_COUT (TAILO_SPAM|TAILO_BEGIN|TAILO_MSG)  //0x20+0x10+0x8 = 32+16+8 = 56

//this function isn't with the rest of them because... meh.
char *tailmode_to_txt(unsigned short mode) {
 char *modes="recmbsn";
 int i,j=0;
 char *m=malloc(strlen(modes));
 for(i=0;i<strlen(modes);i++) {
  if(mode & 1<<i) {
   m[j]=modes[i];
   j++;
  }
 }
 m[j]=0;
 return m;
}

struct user *myuser;
char locked_down;
char pid[6];
char mode_magic;
char trigger_char;
int start_time;
char *redo;
int redirect_to_fd;
int line_limit;
int debug;
timer_t timer;
int lines_sent;
unsigned long oldtime;
int maxtails;

struct hashtable alias;
struct hashtable builtin;

union hack {
 void (*func)(int,...);
 void *data;
};

#define HACK(a) (void *)((union hack){a}.data)

void (*func)(int fd,...);

struct user {
 char *nick;
 char *name;
 char *host;
};

struct tail {
 FILE *fp;
 char *file;
 char *to;
 char *args;
 struct user *user;
 unsigned short opt;
 unsigned int inode;
 int lines;
} *tailf;

char *shitlist[] = { 0 };

void message_handler(int fd,char *from,struct user *user,char *msg,int redones);
void c_untail(int fd,char *from, char *file,struct user *user,...);

void mywrite(int fd,char *b) {
 int r;
 if(!b) return;
 if(fd<0) return;
 r=write(fd,b,strlen(b));
 if(r == -1) exit(1);
 if(r != strlen(b)) exit(2);
 lines_sent++;
}

void irc_mode(int fd,char *channel,char *mode,char *nick) {
 int sz=5+strlen(channel)+1+strlen(mode)+1+strlen(nick)+3;//"MODE ", " ", " ","\r\n\0"
 char *hrm;
 if(!(hrm=malloc(sz+1))) {
  mywrite(fd,"QUIT :malloc error 1! holy shit!\r\n");
  return;
 }
 snprintf(hrm,sz,"MODE %s %s %s\r\n",channel,mode,nick);
 write(fd,hrm,strlen(hrm));
 free(hrm); 
}

void irc_nick(int fd,char *nick) {
 int sz=5+strlen(nick)+3;//"NICK ","\r\n\0"
 char *hrm;
 if(!(hrm=malloc(sz+1))) {
  mywrite(fd,"QUIT :malloc error 1.5! holy shit!\r\n");
  return;
 }
 snprintf(hrm,sz,"NICK %s\r\n",nick);
 write(fd,hrm,strlen(hrm));
 free(hrm); 
}

/*// just in case your system doesn't have strndup
char *strndup(char *s,int l) {
 char *r=strdup(s);
 r[l]=0;
 return r;
}
*/

void privmsg(int fd,char *who,char *msg) {
 int i=0;
 char *chunk,*hrm;
 int sz;
 int cs;
 if(!who) return;
 if(!msg) return;
 for(i=0;i<strlen(msg);i+=LINELEN) {
  cs=(strlen(msg+i)<=LINELEN)?strlen(msg+i):LINELEN;
  sz=8+strlen(who)+2+cs+3;//"PRIVMSG ", " :", "\r\n\0";
  if(!(hrm=malloc(sz))) {
   mywrite(fd,"QUIT :malloc error 2! holy shit!\r\n");
   return;
  }
  chunk=strndup(msg+i, cs );
  snprintf(hrm,sz+1,"PRIVMSG %s :%s\r\n",who,chunk);
  mywrite(fd,hrm);
  free(hrm);
  free(chunk);
 }
}

//try to shorten this up sometime...
char *format_magic(int fd,char *from,struct user *user,char *orig_fmt,char *arg) {
 int i,j,sz=0,c=1;
 char seghome[SEGHOMELEN];
 char *output,*fmt;
 char **args,**notargs;
 if(!arg) arg="%s";
 if(!(fmt=strdup(orig_fmt))) return 0;
 for(i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   switch(fmt[i]) {
     case '~':case 'p':case 'n':case 'h':case 'u':case 'f':case 's':case 'm':case '%'://when adding new format things add here and...
      c++;
   }
  }
 }
 args=malloc((sizeof(char *)) * (c + 1));
 notargs=malloc((sizeof(char *)) * (c + 2));
 c=0;
 for(j=0,i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   switch(fmt[i]) {
     case '~':case 'p':case 'n':case 'h':case 'u':case 'f':case 's':case 'm':case '%'://here.
      args[c]=((fmt[i]=='u')?user->nick:
               ((fmt[i]=='n')?user->name:
                ((fmt[i]=='~')?getcwd(seghome,SEGHOMELEN):
                 ((fmt[i]=='h')?user->host:
                  ((fmt[i]=='f')?from:
                   ((fmt[i]=='p')?pid:
                    ((fmt[i]=='m')?myuser->nick://and here.
                     ((fmt[i]=='s')?arg:"%"
              ))))))));
      fmt[i-1]=0;
      notargs[c]=strdup(fmt+j);
      sz+=strlen(args[c]);
      sz+=strlen(notargs[c]);
      c++;
      j=i+1;
   }
  }
 }
 notargs[c]=strdup(fmt+j);
 sz+=strlen(notargs[c]);
 output=malloc(sz+1);
 output[0]=0;
 for(i=0;i<c;i++) {
  strcat(output,notargs[i]);
  strcat(output,args[i]);
 }
 strcat(output,notargs[i]);
 output[sz]=0;
 free(fmt);
 return output;
}

//these two functions might be fun to try overloading.
void eofd(int fd) {
 char b;
 if(lseek(fd,0,SEEK_END) == -1) {
  while(read(fd,&b,1) > 0);//this is used on named pipes usually.
 }
}

void eofp(FILE *fp) {
 if(fseek(fp,0,SEEK_END) == -1) {
  while(fgetc(fp) != -1);//this is used on named pipes usually.
 }
 clearerr(fp);
}

//this function got scary. basically handles all the tail magic.
void extra_handler(int fd) {
 int tmpo,i;
 static int mmerp=0;
 char tmp[BS+1];
 char *tmp2;
 if(oldtime == time(0) && lines_sent > LINES_SENT_LIMIT) {//if it is still the same second, skip this function.
  return;
 } else {
  lines_sent=0;
 }
 if(redirect_to_fd != -1) {
  fd=redirect_to_fd;
 }
 for(i=0;i<MAXTAILS;i++) {//why does this loop through ALL tails instead of just however many there are?
  if(tailf[i].fp) {       //I think I had it using a variable but that ended up being messy and doing this
   if(feof(tailf[i].fp)) {//ended up being a HELL of a lot easier... maybe fix it sometime.
    clearerr(tailf[i].fp);
    if(tailf[i].opt & TAILO_CLOSE) {//use for eval
     c_untail(fd,tailf[i].to,tailf[i].file,0);
     return;
    }
   }
   tmpo=ftell(tailf[i].fp);
   if(!(tailf[i].opt & TAILO_BEGIN)) {
    eofp(tailf[i].fp);
   }
   if(ftell(tailf[i].fp) < tmpo) {
    privmsg(fd,tailf[i].to,"tailed file shrank! resetting to NEW eof.");
   } else {
    fseek(tailf[i].fp,tmpo,SEEK_SET);//???
   }
   if(tailf[i].lines != -1) {
    if(fgets(tmp,BS-1,tailf[i].fp) == NULL) {//for some reason using sizeof(tmp) didn't work. >_>
     if(tailf[i].lines != 0 && (tailf[i].opt & TAILO_ENDMSG)) {
      privmsg(fd,tailf[i].to,"---------- TAILO_ENDMSG border ----------");
     }
     //snprintf(tmp2,sizeof(tmp)-1,"tailf[%d] (%s): errno: %d, ferror: %d",i,tailf[i].file,errno,ferror(tailf[i].fp));
     //privmsg(fd,tailf[i].to,tmp2);
     tailf[i].lines=0;
    }
    else {
     tailf[i].lines++;
     mmerp=0;
     if(strchr(tmp,'\r')) *strchr(tmp,'\r')=0;
     if(strchr(tmp,'\n')) *strchr(tmp,'\n')=0;
     if(tailf[i].opt & TAILO_EVAL) {//eval
      if(tailf[i].opt & TAILO_FORMAT) {
       tmp2=format_magic(fd,tailf[i].to,tailf[i].user,tmp,tailf[i].args);
      } else {
       tmp2=strdup(tmp);
      }
      message_handler(fd,tailf[i].to,tailf[i].user,tmp2,0);
      free(tmp2);
     }
     if(tailf[i].opt & TAILO_RAW) {//raw
      tmp2=malloc(strlen(tmp)+4);
      snprintf(tmp2,strlen(tmp)+3,"%s\r\n",tmp);
      mywrite(fd,tmp2);
      free(tmp2);
     }
     if(tailf[i].opt & TAILO_MSG) {//just msg the lines.
      privmsg(fd,tailf[i].to,tmp);
     }
     if(tailf[i].lines >= line_limit && (tailf[i].opt & TAILO_SPAM)) {
      tailf[i].lines=-1; //lock it.
      privmsg(fd,tailf[i].to,"--more--");
     }
    }
   } else {
    //don't PM in here. shows a LOT of shit.
   }
  }
 }
}

void file_tail(int fd,char *from,char *file,char *args,char opt,struct user *user) {
 int i;
 int fdd;
 char tmp[256];
 struct stat st;
 for(i=0;i<MAXTAILS;i++) {
  if(tailf[i].fp == 0) {
   break;
  }
 }
 if(i == MAXTAILS -1) {
  exit(3);
 }
 if(*file == '#') {
  from=file;
  file=strchr(file,':');
  if(file) {*file=0;
   file++;
  }
 }
 if((fdd=open(file,O_RDONLY|O_NONBLOCK,0)) == -1) {
  snprintf(tmp,sizeof(tmp)-1,"file_tail: %s: (%s) fd:%d",strerror(errno),file,fdd);
  privmsg(fd,"#cmd",tmp);
  return;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"file_tail opened file '%s' with fd: %d / %d",file,fdd,maxtails);
  privmsg(fd,"#cmd",tmp);
 }
 fstat(fdd,&st); // <-- is this needed?
 /*for(j=0;j<MAXTAILS;j++) {
  if(tailf[j].fp && tailf[j].file && tailf[j].inode) {
   if(tailf[j].inode == st.st_ino) {
    if(debug) privmsg(fd,from,"THIS FILE IS ALREADY BEING TAILED ELSEWHERE!");
    //i=j;break;//reuse it. make sure to add free()ing code for this.
    //return;//don't tail files twice
    //;just add another tail for it
 }}}*/
 i=fdd;//hack hack hack. :P
 if(!(tailf[i].fp=fdopen(fdd,"r"))) {
  snprintf(tmp,sizeof(tmp),"file_tail: failed to fdopen(%s)\n",file);
  privmsg(fd,from,tmp);
 } else {
  fcntl(fileno(tailf[i].fp),F_SETFL,O_NONBLOCK);
  if(!(opt & TAILO_BEGIN)) {
   eofp(tailf[i].fp);
  }
  tailf[i].to=strdup(from);
  if(!tailf[i].to) {
   mywrite(fd,"QUIT :malloc error 3!!!\r\n");
   return;
  }
  tailf[i].file=strdup(file);
  if(!tailf[i].file) {
   mywrite(fd,"QUIT :malloc error 4!!!\r\n");
   return;
  }
  tailf[i].opt=opt;
  tailf[i].inode=st.st_ino;
  tailf[i].user=malloc(sizeof(struct user));
  tailf[i].user->nick=strdup(user->nick);
  tailf[i].user->name=strdup(user->name);
  tailf[i].user->host=strdup(user->host);
  if(!tailf[i].user) {
   mywrite(fd,"QUIT :malloc error 4.5!!! (a strdup again)\r\n");
   return;
  }
  tailf[i].args=args?strdup(args):0;
  if(args) {
   if(!tailf[i].args) {
    mywrite(fd,"QUIT :malloc error 5!!! (well, strdup)\r\n");
    return;
   }
  }
  tailf[i].lines=0;
 }
}

void c_botup(int fd,char *from,...) {
 char tmp[256];
 snprintf(tmp,sizeof(tmp)-1,"botup: %lu",(unsigned long int)time(0)-start_time);
 privmsg(fd,from,tmp);
}

void c_leettail(int fd,char *from,char *file,struct user *user,...) {
 short a=file[0]-'0';
 short b=file[1]-'0';
 short c=file[2]-'0';
 short d;
 short n;
 if(file[2] >= '0' && file[2] <= '9') {
  d=(a*100)+(b*10)+(c);
  n=3;
 } else {
  d=(a*10)+(b);
  n=2;
 }
 char *args;
 if(file[0]==' ') file++;
 if((args=strchr(file,' '))) {
  *args=0;
  args++;
 }
 file_tail(fd,from,file+n,args,d,user);
}

void c_changetail(int fd,char *from,char *line,struct user *user,...) {
 struct stat st;
 char *merp=0;
 char tmp[512];
 int i;
 int fdd;
 char *mode=0;
 //if(line == 0) return mywrite(fd,"QUIT :line == 0 in changetail\r\n");
 //if(from == 0) return mywrite(fd,"QUIT :from == 0 in changetail\r\n");
 if((merp=strchr(line,' '))) {
  *merp=0;
  merp++;
  if((mode=strchr(merp,' '))) {
   *mode=0;
   mode++;
  }
 }
 if((fdd=open(line,O_RDONLY|O_NONBLOCK,0)) == -1) {
  snprintf(tmp,sizeof(tmp)-1,"%s: (%s) fd:%d",strerror(errno),line,fdd);
  privmsg(fd,"#cmd",tmp); 
  return;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"changetail opened file '%s' with fd: %d / %d\n",line,fdd,maxtails);
  privmsg(fd,"#cmd",tmp);
 }
 fstat(fdd,&st);
 close(fdd);
 for(i=0;i<MAXTAILS;i++) {
  //if(tailf[i].file == 0) return mywrite(fd,"QUIT :tailf[i].file == 0 in changetail\r\n");
  if(tailf[i].file) {
   if(!strcmp(tailf[i].file,line) || tailf[i].inode == st.st_ino) {
    free(tailf[i].to);
    tailf[i].to=strdup(merp);
    if(mode) {
     tailf[i].opt=((mode[0]-'0')*10)+(mode[1]-'0');
    }
    return;
   }
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"changetail: tail (%s) not found",line);
 privmsg(fd,from,tmp);
}

void prestartup_stuff(int fd) {
 int fdd;
 if((fdd=open("./scripts/prestartup",O_RDONLY))) {
  close(fdd);
  c_leettail(fd,"#cmd","22./scripts/prestartup",myuser);
 }
}

void startup_stuff(int fd) {
 c_leettail(fd,"#cmd","150./scripts/startup",myuser);
}

void debug_time(int fd,char *from,char *msg) {
 char tmp[100];
 if(debug) {
  snprintf(tmp,99,"%lu %s",(unsigned long int)time(0),msg?msg:"(no message)");//time() returns time_t which on BSD is a long.
  privmsg(fd,from,tmp);
 }
}

void c_builtin(int fd,char *from,char *line,...) {
 char tmp[512];
 char *function=line;
 char *addr;
 unsigned int address; // lol. will fail on x64
 if(!line) return;
 if((addr=strchr(line,' '))) {
  *addr=0;
  addr++;
  if(!sscanf(addr,"%08x",&address)) {
   privmsg(fd,from,"sscanf didn't get an address.");
   return;
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"address read: %08x",address);
 privmsg(fd,from,tmp);
 ht_setkey(&builtin,function,(void *)address);
 return;
}

void c_builtins(int fd,char *from,char *line,...) {
 char tmp[512];
 struct entry *m;
 int i,j=0,k=0;
 if(!line){
  privmsg(fd,from,"usage: !builtins [search-term]");
  return;
 }
 for(i=0;i<builtin.kl;i++) {
  if(debug) {
   snprintf(tmp,sizeof(tmp)-1,"builtins in bucket: %d",builtin.keys[i]);
   privmsg(fd,from,tmp);
  }
  for(m=builtin.bucket[builtin.keys[i]]->ll;m;m=m->next) {
   if(strcasestr(m->original,line) || *line=='*') {
    snprintf(tmp,sizeof(tmp)-1," %s -> %p",m->original,m->target);
    privmsg(fd,from,tmp);
    j++;
   }
   k++;
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"found %d of %d in builtins",j,k);
 privmsg(fd,from,tmp);
}

void c_aliases_h(int fd,char *from,char *line,...) {
 char tmp[512];
 struct entry *m;
 int i,j=0,k=0;
 if(!line){
  privmsg(fd,from,"usage: !aliases [search-term]");
  return;
 }
 for(i=0;i<alias.kl;i++) {
  //snprintf(tmp,sizeof(tmp)-1,"aliases in bucket: %d",alias->keys[i]);
  //privmsg(fd,from,tmp);
  for(m=alias.bucket[alias.keys[i]]->ll;m;m=m->next) {
   if(strcasestr(m->target,line) || strcasestr(m->original,line)) {
    snprintf(tmp,sizeof(tmp)-1," %s -> %s",m->original,(char *)m->target);
    privmsg(fd,from,tmp);
    j++;
   }
   k++;
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"found %d of %d aliases",j,k);
 privmsg(fd,from,tmp);
}

void c_alias_h(int fd,char *from,char *line,...) {
 char tmps[512];
 char *derp=strchr(line,' ');
 struct entry *tmp;
 if(!derp) {
  if((tmp=ht_getnode(&alias,line)) != NULL) {
   snprintf(tmps,sizeof(tmps)," %s",(char *)tmp->target);
   privmsg(fd,from,tmps);
  } else {
   snprintf(tmps,sizeof(tmps),"'%s' not an alias.",line);
   privmsg(fd,from,tmps);
  }
  return;
 }
 *derp=0;
 derp++;
 if((tmp=ht_getnode(&alias,line))) {
  free(tmp->target);
 }
 ht_setkey(&alias,line,strdup(derp));
}

void c_kill(int fd,char *from,char *line,...) {
 char *csig=line;
 char *cpid=strchr(line,' ');
 int sig,pid;
 if(cpid) {
  *cpid=0;
  cpid++;
 } else {
  privmsg(fd,from,"usage: !kill signum pid");
  return;
 }
 sig=atoi(csig);
 pid=atoi(cpid);
 if(sig && pid) {
  if(kill(pid,sig)) privmsg(fd,from,"kill error");
  else privmsg(fd,from,"signal sent");
 } else {
  privmsg(fd,from,"pid or sig is 0. something is odd.");
 }
}

void c_id(int fd,char *from,...) {
 char tmp[512];
 snprintf(tmp,sizeof(tmp)-1,"u:%d g:%d eu:%d eg:%d",getuid(),getgid(),geteuid(),getegid());
 privmsg(fd,from,tmp);
}

void c_leetuntail(int fd,char *from,char *line,...) {
 char *frm=line;
 char *file=0;
 int frmN=0;
 int i;
 char tmp[512];
 if((file=strchr(line,' '))) {
  *file=0;
  file++;
 }
 if(file) {
  if(*frm == '*') {
   for(i=0;i<MAXTAILS;i++) {
    if(tailf[i].fp && !strcmp(tailf[i].file,file)) {
     //c_untail(fd,tailf[i].to,file);
     eofp(tailf[i].fp);
     if(fclose(tailf[i].fp) == -1) {
      privmsg(fd,from,"well, shit. fclose failed somehow.");
     }
     tailf[i].fp=0;
     free(tailf[i].to);
     free(tailf[i].file);
     return;
    }
   }
   //snprintf(tmp,sizeof(tmp)-1,"%s from %s not being tailed.",file,frm);
   //privmsg(fd,from,tmp);
  } else {
   c_untail(fd,frm,file,0);
  }
 } else {
  frmN=atoi(frm);
  if(frmN < MAXTAILS && tailf[frmN].fp) {
   if(fclose(tailf[frmN].fp) == -1) {
    privmsg(fd,from,"well shit. fclose failed. #2");
   }
   tailf[frmN].fp=0;
   free(tailf[frmN].to);
   free(tailf[frmN].file);
   snprintf(tmp,sizeof(tmp)-1,"untailed file tail #%d.",frmN);
  } else {
   snprintf(tmp,sizeof(tmp)-1,"file tail #%d isn't a valid number.",frmN);
  }
  privmsg(fd,from,tmp);
 }
}

void c_tailunlock(int fd,char *from,char *file,...) {
 int i;
 for(i=0;i<MAXTAILS;i++) {
  if(tailf[i].fp) {
   if(!strcmp(file,tailf[i].file)) {
    tailf[i].lines=0;
    return;
   }
  }
 }
 privmsg(fd,from,"file not found in the tail list.");
}

void c_untail(int fd,char *from, char *file,struct user *user,...) {
 int i;
 for(i=0;i<MAXTAILS;i++) {
  if(tailf[i].fp) {
   if(!strcmp(from,tailf[i].to) && !strcmp(file,tailf[i].file)) {
    if(fclose(tailf[i].fp) == -1) {
     privmsg(fd,from,"fclose failed. #3");
    }
    tailf[i].fp=0;
    free(tailf[i].to);
    free(tailf[i].file);
    //privmsg(fd,from,"tailed file no longer being tailed.");
    return;
   }
  }
 }
 privmsg(fd,from,"I don't know what file you're talking about.");
 privmsg(fd,from,"You have to be in the same channel that the tail was set in.");
}

char append_file(int fd,char *from,char *file,char *line,unsigned short nl) {
 int fdd;
 char tmp[512];
 char derp[2];
 FILE *fp;
 derp[0]=(char)nl;
 derp[1]=0;
 if(line == 0) return mywrite(fd,"QUIT :line == 0 in append_file\r\n"),-1;
 if((fdd=open(file,O_WRONLY|O_NONBLOCK|O_APPEND|O_CREAT,0640)) == -1) {
  snprintf(tmp,sizeof(tmp)-1,"%s: (%s) fd:%d",strerror(errno),file,fdd);
  privmsg(fd,from,tmp);
  return 0;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"append_file opened file '%s' with fd: %d / %d\n",file,fdd,maxtails);
  privmsg(fd,"#cmd",tmp);
 }
 if(!(fp=fdopen(fdd,"a"))) {
  snprintf(tmp,sizeof(tmp)-1,"Couldn't fdopen file (%s) fd:%d for appending.",file,fdd);
  privmsg(fd,from,tmp);
  privmsg(fd,from,strerror(errno));
  return 0;
 }
 fcntl(fileno(fp),F_SETFL,O_NONBLOCK);
 eofp(fp);
/* mywrite(fileno(fp),line);
 mywrite(fileno(fp),"\n");*/
 fprintf(fp,"%s\n",line);
/*
 fcntl(fdd,F_SETFL,O_NONBLOCK);
 eofd(fdd);
 mywrite(fdd,line);
 mywrite(fdd,"\n");
*/
// close(fdd);
 fclose(fp);
 return 1;
}

void c_leetappend(int fd,char *from,char *msg,...) {
 unsigned short nl;
 char *file=msg;
 char *snl=0;
 char *line=0;
 if(msg) {
  if((snl=strchr(msg,' '))) {
   *snl=0;
   snl++;
   if((line=strchr(snl,' '))) {
    *line=0;
    line++;
   }
  }
 }
 if(!snl || !line || !msg) {
  privmsg(fd,from,"usage: !leetappend file EOL-char-dec line-to-put");
  return;
 }
 nl=((snl[0]-'0')*10)+((snl[1]-'0'));
 append_file(fd,from,file,line,nl);
}

void c_tails(int fd,char *from,...) {
 int i;
 int l;
 int at_least_one=0;
 char *tmp,*x;
 //privmsg(fd,from,"filename@filepos --msg|raw-> IRCdestination");
 for(i=0;i<MAXTAILS;i++) {
  if(tailf[i].fp) {
   at_least_one=1;
   l=(strlen(tailf[i].file) + strlen(tailf[i].to) + 50);//??? hack. fix it.
   tmp=malloc(l);
   if(!tmp) {
    mywrite(fd,"QUIT :malloc error 8\r\n");
    return;
   }
   x=tailmode_to_txt(tailf[i].opt);
   snprintf(tmp,l,"%s [i:%d] @ %ld (%d) --[%s(%03u)]--> %s",tailf[i].file,tailf[i].inode,ftell(tailf[i].fp),tailf[i].lines,x,tailf[i].opt,tailf[i].to);
   free(x);
   privmsg(fd,from,tmp);
   free(tmp);
  }
 }
 if(!at_least_one) {
  privmsg(fd,from,"I don't have any tails. :(");
 }
}

char recording,recording_raw;

void c_record(int fd,char *from,char *line,...) {
 if(*line == '0') {
  privmsg(fd,from,"no longer recording IRC.");
  recording=0;
  return;
 }
 if(*line == '1') {
  recording=1;
  unlink(LOG);
  privmsg(fd,from,"recording IRC.");
  return;
 }
 privmsg(fd,from,recording?"1":"0");
}

void c_rawrecord(int fd,char *from,char *line,...) {
 if(!line) {
  printf("that's odd.\n");
  return;
 }
 if(*line == '0') { 
  privmsg(fd,from,"no longer recording raw IRC.");
  recording_raw=0;
  return;
 }
 if(*line == '1') {
  recording_raw=1;
  unlink(RAWLOG);
  privmsg(fd,from,"recording raw IRC.");
  return;
 }
 privmsg(fd,from,recording_raw?"1":"0");
}


void c_leetsetout(int fd,char *from,char *msg,...) {
 char tmp[512];
 if(redirect_to_fd != -1) close(redirect_to_fd);
 redirect_to_fd=open(msg+3,((msg[0]-'0')*100) + ((msg[1]-'0')*10) + (msg[2]-'0'),022);
 if(redirect_to_fd == -1) {
  snprintf(tmp,sizeof(tmp)-1,"%s: (%s) fd:%d",strerror(errno),msg+3,redirect_to_fd);
  privmsg(fd,"#cmd",tmp);
  return;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"leetsetout opened file '%s' with fd: %d / %d\n",msg+3,redirect_to_fd,maxtails);
  privmsg(fd,"#cmd",tmp);
 }
}

void c_linelimit(int fd,char *from,char *msg,...) {
 char tmp[256];
 if(!msg) {
  snprintf(tmp,255,"current spam line limit: %d (debug: %d)",line_limit,debug);
  privmsg(fd,from,tmp);
 }
 else {
  if(msg[0] == 'a') {
   mode_magic^=1;
   privmsg(fd,from,"mode_magic flipped. happy easter!");
  }
  if(msg[0]=='!') {
   if(msg[1]) {
    trigger_char=msg[1];
    privmsg(fd,from,"trigger_char set. more easter!");
   }
  }
  if(atoi(msg) > 0) {
   line_limit=atoi(msg);
   snprintf(tmp,255,"spam line limit set to: %d",line_limit);
   privmsg(fd,from,tmp);
  }
  else if(atoi(msg) < 0) {
   privmsg(fd,from,"hidden feature! negative line limit flips debug bit.");
   debug^=1;
  } else {
   privmsg(fd,from,"something else!");
  }
 }
}

void c_resetout(int fd,char *from,...) {
 redirect_to_fd=-1;
 privmsg(fd,from,"output reset");
}

void c_raw(int fd,char *from,char *msg,...) {
 char *tmp2;
 tmp2=malloc(strlen(msg)+4);
 snprintf(tmp2,strlen(msg)+3,"%s\r\n",msg);
 mywrite(fd,tmp2);
 free(tmp2);
}

void c_say(int fd,char *from,char *msg,...) {
 privmsg(fd,from,msg);
}

void c_nick(int fd,char *from,char *msg,...) {
 free(myuser->nick);
 myuser->nick=strdup(msg);
 irc_nick(fd,myuser->nick);
}

void message_handler(int fd,char *from,struct user *user,char *msg,int redones) {
 struct entry *m;
 union hack lol;
 char *command;
 char *args;
// char *magic;
 char tmp[512];
 int len;
 int sz;
 //debug_time(fd,from);
 if(user->nick) {
  if(strcmp(user->nick,myuser->nick)) {
   if(locked_down) return;
   for(sz=0;shitlist[sz];sz++) {
    if(!strcmp(shitlist[sz],user->nick)) {
     return;
    }
   }
  }
 }
 if(redirect_to_fd != -1) {
  fd=redirect_to_fd;
 }
 if(recording) {
  debug_time(fd,from,"writing to log...");
  snprintf(tmp,sizeof(tmp)-1,"<%s> %s",user->nick,msg);
  append_file(fd,user->nick,LOG,tmp,'\n');
  debug_time(fd,from,"finished writing to log.");
 }
 len=strchr(msg,'*')?strchr(msg,'*')-msg:strlen(myuser->nick);
 if(!strncmp(msg,myuser->nick,len)) {
  if(msg[len] == '*') len++;
  if(msg[len] == ',' || msg[len] == ':') {
   if(msg[len+1] == ' ') {
    msg+=len+1;
    msg[0]=trigger_char;
   }
  }
 }
 //if(*msg == trigger_char) *msg='!';
 //if(*msg != '!') {
 // return;
 //}

 command=strdup(msg);
 if(command[0] == trigger_char) {
  command++;
 } else {
  return;
 }
 //privmsg(fd,from,command);
 if((args=strchr(command,' '))) {
  *args=0;
  args++;
 }
 //if(!strncmp(command,"lambda",6)) {
 // command+=8;
 // if((args=strchr(command,' '))) {
 //  *args=0;
 //  args++;
 // }
 // args=format_magic(fd,from,user,args,":/");
 //}
 if((lol.data=ht_getvalue(&builtin,command))) {
  func=lol.func;
 // privmsg(fd,from,"found it in builtins HT.");
  func(fd,from,args,user);
  //if(args != (command + strlen(command) + 2)) {
  // free(args);
  //}
 }
 else if(redones < 5) {
  debug_time(fd,from,"checking aliases...");
  command--;// :>
  if((m=ht_getnode(&alias,command)) != NULL) {
   sz=(strlen(msg)-strlen(m->original)+strlen(m->target)+1);
   redo=format_magic(fd,from,user,m->target,*(msg+strlen(m->original)+1)=='\n'?"":(msg+strlen(m->original)+1));
   message_handler(fd,from,user,redo,redones+1);
   free(redo);
   redo=0;
   return;
  }
  debug_time(fd,from,"finished checking aliases. not found.");
  redo=0;
  if(debug) {
   snprintf(tmp,sizeof(tmp)-1,"command not found: '%s' with args '%s'",command,args);
   privmsg(fd,from,tmp);
  }
 }
 if(redones >5) {
  privmsg(fd,from,"I don't know if I'll ever get out of this alias hole you're telling me to dig. Fuck this.");
 }
}

void line_handler(int fd,char *line) {//this should be built into the libary?
 char *s=line,*t=0,*u=0;
 struct user *user=malloc(sizeof(struct user));
 user->nick=0;
 user->name=0;
 user->host=0;
 if(strchr(line,'\r')) *strchr(line,'\r')=0;
 if(strchr(line,'\n')) *strchr(line,'\n')=0;
 printf("line: '%s'\n",line);
 //:nick!name@host MERP DERP :message
 //:nick!name@host s t :u
 //:armitage.hacking.allowed.org MERP DERP :message
 //:nickhost s t :u
 //only sub-parse nicknamehost stuff if starts with :
 //strchr doesn't like null pointers. :/ why not just take them and return null?
 //check that I haven't gone past the end of the string? nah. it should take care of itself.
 if(recording_raw) {
  append_file(fd,"epoch",RAWLOG,line,'\n');
 }

 // rewrite this shit.
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
  if(((user->name)=strchr((user->nick),'!'))) {
   *(user->name)=0;
   (user->name)++;
   if(((user->host)=strchr((user->name),'@'))) {
    *(user->host)=0;
    (user->host)++;
   }
  } else {
   user->host=user->nick;
  }
 }
 //all this shit.
 
 printf("<%s!%s@%s> '%s' '%s' '%s'\n",
        user->nick,
        user->name,
        user->host,
        s,t,u);
 if(!user->name && s) { //server message
  if(!strcmp(s,"433")) {//nick already used.
   srand(time(NULL)*getpid());
   myuser->nick[strlen(myuser->nick)-3]=(rand()%10)+'0';
   myuser->nick[strlen(myuser->nick)-2]=(rand()%10)+'0';
   myuser->nick[strlen(myuser->nick)-1]=(rand()%10)+'0';
   irc_nick(fd,myuser->nick);
  }
  if(!strcmp(s,"004")) {//we're connected.
   startup_stuff(fd);
  }
 }
 if(s && t && u) {
  if(!strcmp(s,"PRIVMSG") && strcmp(user->nick,myuser->nick)) {
   if(strcmp(user->nick,myuser->nick)) {
    message_handler(fd,*t=='#'?t:user->nick,user,++u,0);
   }
   else {
    if(debug) privmsg(fd,*t=='#'?t:user->nick,"This server has an echo");
   }
  }
 }
 if(s && user->nick && t) {
  if(!strcmp(s,"JOIN")) {
   irc_mode(fd,t+1,"+o",user->nick);//why t+1? it starts with :?
  }
  if(!strcmp(s,"MODE") && mode_magic) {
   if(u) {
    if(*u == '-') {//auto-give modes back that are removed in front of segfault.
     *u='+';
     irc_mode(fd,t,u,"");//u contains the nick the mode is being removed from.
    }
   }
  }
//:Ishikawa-!~epoch@localhost NICK :checking
  if(!strcmp(s,"NICK")) {
   if(!strcmp(user->nick,myuser->nick)) {
    free(myuser->nick);
    myuser->nick=strdup(t+1);
   }
  }
 }
 free(user);
}

int main(int argc,char *argv[]) {
 int fd;
 struct passwd *pwd;
 struct rlimit nofile;
 char *s,*p;
 int c;
 inittable(&builtin,TSIZE);
#define BUILDIN(a,b) ht_setkey(&builtin,a,(void *)((union hack){(void (*)(int,...))b}.data))
 BUILDIN("builtin",c_builtin);
 BUILDIN("builtins",c_builtins);
 BUILDIN("raw",c_raw);
 BUILDIN("leetsetout",c_leetsetout);
 BUILDIN("resetout",c_resetout);
 BUILDIN("botup",c_botup);
 BUILDIN("linelimit",c_linelimit);
 BUILDIN("nick",c_nick);
 BUILDIN("tailunlock",c_tailunlock);
 BUILDIN("changetail",c_changetail);
 BUILDIN("tails",c_tails);
 BUILDIN("record",c_record);
 BUILDIN("rawrecord",c_rawrecord);
 BUILDIN("leettail",c_leettail);
 BUILDIN("leetuntail",c_leetuntail);
 BUILDIN("leetappend",c_leetappend);
 BUILDIN("untail",c_untail);
 BUILDIN("say",c_say);
 BUILDIN("id",c_id);
 BUILDIN("kill",c_kill);
 BUILDIN("alias",c_alias_h);
 BUILDIN("aliases",c_aliases_h);
 mode_magic=0;
 trigger_char='!';
 redirect_to_fd=-1;
 debug=0;
 locked_down=(argc>2);
 lines_sent=0;
 line_limit=25;
 recording=0;
 recording_raw=0;
 start_time=time(0);
 redo=0;
 inittable(&alias,TSIZE);
 if(getrlimit(RLIMIT_NOFILE,&nofile) == -1) {
  printf("couldn't get max open files limit.\n");
  exit(0);
 } else {
  maxtails=nofile.rlim_max;
  tailf=malloc(sizeof(struct tail) * (maxtails + 1));
 }
 myuser=malloc(sizeof(struct user));
 myuser->nick=strdup(argc>1?argv[1]:NICK);
 myuser->name="I_dunno";
 myuser->host="I_dunno";
 snprintf(pid,6,"%d",getpid());
 printf("starting segfault...\n");
 if(!getuid() || !geteuid()) {
  s=getenv("seguser");
  pwd=getpwnam(s?s:MYUSER);
  if(!pwd) { printf("I'm running with euid or uid of 0 and I can't find myself."); return 0; }
  setgroups(0,0);
  setgid(pwd->pw_gid);
  setuid(pwd->pw_uid);
 } else {
  pwd=getpwuid(getuid());
  printf("going to run segfault as user %s\n",pwd->pw_name);
  if(!pwd) { printf("well, shit. I don't know who I am."); return 0; }
 }
 for(c=0;c<MAXTAILS;c++) tailf[c].fp=0;
 s=getenv("segserver"); s=s?s:SERVER;
 p=getenv("segport"); p=p?p:PORT;
 printf("connecting to: %s port %s\n",s,p);
 fd=serverConnect(getenv("segserver")?getenv("segserver"):SERVER,
                  getenv("segport")?getenv("segport"):PORT);
//               myuser->nick,
//               "segfault segfault segfault :segfault");
 printf("cd %s\n",pwd->pw_dir);
 chdir(getenv("seghome")?getenv("seghome"):pwd->pw_dir);
 prestartup_stuff(fd);
 return runit(fd,line_handler,extra_handler);
}
