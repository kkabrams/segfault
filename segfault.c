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
#include "libirc/irc.h" //epoch's libirc. should be included with segfault.

//might want to change some of these.
#define SERVER			"127.0.0.1"
#define PORT			"6667"
#define NICK			"SegFault" //override with argv[0]
#define LINE_LIMIT		line_limit
#define LINES_SENT_LIMIT	1
#define LINELEN			400
#define RAWLOG			"/home/segfault/files/rawlog"
#define LOG			"/home/segfault/files/log"
#define MAXTAILS		400 //just to have it more than the system default.
#define BS 502
#define TSIZE 65536             //size of hashtable. 65k isn't bad, right?
// !c uses 56 for its tail.
// 56 == 32 + 16 + 8 == 0x38 == 0x20+0x10+0x8 == SPAM | BEGIN | MSG
#define TAILO_RAW    (0x1) // r output gets sent directly to server
#define TAILO_EVAL   (0x2) // e interpret the lines read from the tail as if they were messages to segfault
#define TAILO_CLOSE  (0x4) // c close the file at EOF, default is to leave it open.
#define TAILO_MSG    (0x8) // m output gets sent as a PM to the place the tail was made.
#define TAILO_BEGIN  (0x10) //b start the tail at the beginning of the file instead of the end.
#define TAILO_SPAM   (0x20) //s Spam control is enabled for this stream.
#define TAILO_ENDMSG (0x40) //n show a message when the tail reaches the end of a chunk
#define TAILO_Q_EVAL (TAILO_EVAL|TAILO_CLOSE|TAILO_BEGIN) //0x2+0x4+0x10 = 2+4+16  = 22
#define TAILO_Q_COUT (TAILO_SPAM|TAILO_BEGIN|TAILO_MSG)  //0x20+0x10+0x8 = 32+16+8 = 56

//this function isn't with the rest of them because... meh.
char *tailmode_to_txt(int mode) {
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

int start_time;
char *segnick;
char *redo;
int redirect_to_fd;
int line_limit;
int debug;
timer_t timer;
int lines_sent;
unsigned long oldtime;

struct tail {
 FILE *fp;
 char *file;
 char *to;
 char *args;
 char opt;
 unsigned int inode;
 int lines;
} tailf[MAXTAILS];

struct alias {
 char *original;
 char *target;
 struct alias *prev;// doubly linked list.
 struct alias *next;
};

char *shitlist[] = { 0 };

void message_handler(int fd,char *from,char *nick,char *msg,int redones);
void c_untail(int fd,char *from, char *file);


void mywrite(int fd,char *b) {
 int r;
 if(!b) return;
 if(fd<0) return;
 r=write(fd,b,strlen(b));
 if(r == -1) exit(1);
 if(r != strlen(b)) exit(2);
 lines_sent++;
}

void ircmode(int fd,char *channel,char *mode,char *nick) {
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

/*#ifndef strndup
char *strndup(char *s,int l) {
 char *r=strdup(s);
 r[l]=0;
 return r;
}
#endif*/

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
char *format_magic(int fd,char *from,char *nick,char *orig_fmt,char *arg) {
 int i,j,sz=0,c=1;
 char *output,*fmt;
 char **args,**notargs;
 if(!(fmt=strdup(orig_fmt))) return 0;
 for(i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   switch(fmt[i]) {
     case 'u':case 'f':case 's':case 'm'://when adding new format things add here and...
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
     case 'u':case 'f':case 's':case 'm'://here.
      args[c]=((fmt[i]=='u')?nick:
               ((fmt[i]=='f')?from:
                ((fmt[i]=='m')?segnick://and here.
                 arg
              )));
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
     c_untail(fd,tailf[i].to,tailf[i].file);
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
    if(fgets(tmp,BS-1,tailf[i].fp) ) {//for some reason using sizeof(tmp) didn't work. >_>
     tailf[i].lines++;
     mmerp=0;
     if(strchr(tmp,'\r')) *strchr(tmp,'\r')=0;
     if(strchr(tmp,'\n')) *strchr(tmp,'\n')=0;
     if(tailf[i].opt & TAILO_EVAL) {//eval
      if(tailf[i].args) { //only format magic evaled file lines if they have args. //why?
       tmp2=format_magic(fd,tailf[i].to,segnick,tmp,tailf[i].args);
      } else {
       if(!(tmp2=strdup(tmp))) exit(2);
      }
      message_handler(fd,tailf[i].to,segnick,tmp2,0);
      free(tmp2);
     }
     if(tailf[i].opt & TAILO_RAW) {//raw
      tmp2=malloc(strlen(tmp)+3);
      snprintf(tmp2,strlen(tmp)+3,"%s\r\n",tmp);
      mywrite(fd,tmp2);
      free(tmp2);
     }
     if(tailf[i].opt & TAILO_MSG) {//just msg the lines.
      privmsg(fd,tailf[i].to,tmp);
     }
     if(tailf[i].lines >= LINE_LIMIT && (tailf[i].opt & TAILO_SPAM)) {
      tailf[i].lines=-1; //lock it.
      privmsg(fd,tailf[i].to,"--more--");
     }
    } else {
     if(tailf[i].lines != 0 && (tailf[i].opt & TAILO_ENDMSG)) {
      privmsg(fd,tailf[i].to,"---------- TAILO_ENDMSG border ----------");
     }
     tailf[i].lines=0;
    }
   } else {
    //don't PM in here. shows a LOT of shit.
   }
  }
 }
}

void file_tail(int fd,char *from,char *file,char *args,char opt) {
 int i,j;
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
 fdd=open(file,O_RDONLY|O_NONBLOCK,0);//HAVE to open named pipes as nonblocking.
 fstat(fdd,&st);
 for(j=0;j<MAXTAILS;j++) {
  if(tailf[j].fp && tailf[j].file && tailf[j].inode) {
   if(tailf[j].inode == st.st_ino) {
    if(debug) privmsg(fd,from,"THIS FILE IS ALREADY BEING TAILED ELSEWHERE!");
    //i=j;
    //break;//reuse it.
    //return;
   }
  }
 }
 if(!(tailf[i].fp=fdopen(fdd,"r"))) {
  snprintf(tmp,sizeof(tmp),"failed to open file: %s\n",file);
  privmsg(fd,from,tmp);
 } else {
  fcntl(fileno(tailf[i].fp),F_SETFL,O_NONBLOCK);
  if(!(opt & TAILO_BEGIN)) {
   eofp(tailf[i].fp);
  }
  tailf[i].to=malloc(strlen(from)+1);
  if(!tailf[i].to) {
   mywrite(fd,"QUIT :malloc error 3!!!\r\n");
   return;
  }
  strcpy(tailf[i].to,from);
  tailf[i].file=malloc(strlen(file)+1);
  if(!tailf[i].file) {
   mywrite(fd,"QUIT :malloc error 4!!!\r\n");
   return;
  }
  tailf[i].opt=opt;
  tailf[i].inode=st.st_ino;
  tailf[i].args=args?strdup(args):0;
  if(args) {
   if(!tailf[i].args) {
    mywrite(fd,"QUIT :malloc error 5!!! (well, strdup)\r\n");
    return;
   }
  }
  tailf[i].lines=0;
  strcpy(tailf[i].file,file);
 }
}

void c_botup(int fd,char *from) {
 char tmp[256];
 snprintf(tmp,sizeof(tmp)-1,"botup: %lu",time(0)-start_time);
 privmsg(fd,from,tmp);
}

void c_leettail(int fd,char *from,char *file) {
 short a=file[0]-'0';
 short b=file[1]-'0';
 short c=(a*10)+(b);
 char *args;
 if((args=strchr(file,' '))) {
  *args=0;
  args++;
 }
 file_tail(fd,from,file+2,args,c);
}

void c_changetail(int fd,char *from,char *line) {
 char *merp;
 int i;
 //if(line == 0) return mywrite(fd,"QUIT :line == 0 in changetail\r\n");
 //if(from == 0) return mywrite(fd,"QUIT :from == 0 in changetail\r\n");
 if((merp=strchr(line,' '))) {
  *merp=0;
  merp++;
 }
 for(i=0;i<MAXTAILS;i++) {
  //if(tailf[i].file == 0) return mywrite(fd,"QUIT :tailf[i].file == 0 in changetail\r\n");
  if(tailf[i].file) {
   if(!strcmp(tailf[i].file,line)) {
    free(tailf[i].to);
    tailf[i].to=strdup(merp);
    return;
   }
  }
 }
 privmsg(fd,from,"error");
}

void startup_stuff(int fd) {
 mywrite(fd,"OPER g0d WAFFLEIRON\r\n");
 mywrite(fd,"JOIN #cmd\r\n");
 c_leettail(fd,"#cmd","22/home/segfault/scripts/startup");
}

void debug_time(int fd,char *from,char *msg) {
 char tmp[100];
 if(debug) {
  snprintf(tmp,99,"%lu %s",time(0),msg?msg:"(no message)");//time() returns time_t which on BSD is a long.
  privmsg(fd,from,tmp);
 }
}

struct alias *leetgetalias(struct alias *start,char *msg) {
 struct alias *m;
 if(!msg) return NULL;
 if(!start) return NULL;
 for(m=start;m;m=m->next) {//optimize this. hash table?
  if(!strncmp(msg,m->original,strlen(m->original)) && (msg[strlen(m->original)]==' ' || msg[strlen(m->original)] == 0)) {//this allows !c to get called when using !c2 if !c2 is defined after !c. >_>
   return m;
  }
 }
 return NULL;
}

unsigned short hash(char *v) {//maybe use a seeded rand()? :) Thanks FreeArtMan
 unsigned short h=0;
 h=((*v)<<8)+(v?*(v+1):0);
 srand(h);
 return rand();
}

struct hitem {//with a stick
 struct alias *ll;//run through this linked list if it isn't the right one.
};

struct hitem **hashtable;
int htkeys[TSIZE];
int htkl;

void inittable() {
 int i;
 hashtable=malloc(sizeof(char *)*TSIZE);
 if(!hashtable) {
  fprintf(stderr,"malloc error 6 in hash table.\n");
  return;
 }
 for(i=0;i<TSIZE;i++) {
  hashtable[i]=0;
 }
}

struct alias *getkey_h(char *key) {
 unsigned short h=hash(key);
 if(!hashtable[h]) return NULL;
 return hashtable[h]->ll;
}

//this seems too complicated.
int setkey_h(char *key,char *value) {
 unsigned short h=hash(key);
 struct alias *tmp;
 int i;
 for(i=0;i<htkl;i++) {
  if(htkeys[i]==h) break;
 }
 htkeys[i]=h;
 htkl=htkl>i+1?htkl:i+1;
 if(!hashtable[h]) { //empty bucket!
  //add this to the list of used buckets so we can easily
  //use that list later for stuff.
  if(!(hashtable[h]=malloc(sizeof(struct hitem)))) return 1; 
  hashtable[h]->ll=0;
  //we now have a valid hashtable entry and a NULL ll in it.
  //don't bother with the new ll entry yet...
 }
 if((tmp=leetgetalias(hashtable[h]->ll,key)) != NULL) {
  //we found this alias in the ll. now to replace the value
  free(tmp->target);
  if(!(tmp->target=strdup(value))) return 2;
  return 0;
 }
 if(hashtable[h]->ll == NULL) {
  if(!(hashtable[h]->ll=malloc(sizeof(struct alias)))) return 3;
  hashtable[h]->ll->next=0;
  hashtable[h]->ll->prev=0;
  if(!(hashtable[h]->ll->original=strdup(key))) return 4;
  if(!(hashtable[h]->ll->target=strdup(value))) return 5;
 } else {
  //go to the end and add another entry to the ll.
  for(tmp=hashtable[h]->ll;tmp->next;tmp=tmp->next);
  if(!(tmp->next=malloc(sizeof(struct alias)))) return 6;
  tmp->next->prev=tmp;
  tmp=tmp->next;
  if(!(tmp->original=strdup(key))) return 7;
  if(!(tmp->target=strdup(value))) return 8;
  tmp->next=0;
 }
 return 0;
}

void c_aliases_h(int fd,char *from,char *line) {
 char tmp[512];
 struct alias *m;
 int i,j=0,k=0;
 if(!line){
  privmsg(fd,from,"usage: !aliases [search-term]");
  return;
 }
 for(i=0;i<htkl;i++) {
  //snprintf(tmp,sizeof(tmp)-1,"aliases in bucket: %d",htkeys[i]);
  //privmsg(fd,from,tmp);
  for(m=hashtable[htkeys[i]]->ll;m;m=m->next) {
   if(strcasestr(m->target,line) || strcasestr(m->original,line)) {
    snprintf(tmp,sizeof(tmp)-1,"%s -> %s",m->original,m->target);
    privmsg(fd,from,tmp);
    j++;
   }
   k++;
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"found %d of %d aliases",j,k);
 privmsg(fd,from,tmp);
}

//CONVERT //this is the last one that needs to be converted to
// be used with the hashtable
/*
void c_rmalias(int fd,char *from,char *line) {
 struct alias *m;
 for(m=a_start;m;m=m->next) {
  if(!strcmp(line,m->original)) {
   if(m->next == 0) {
    a_end=m->prev;
   }
   if(m->prev == 0) {
    a_start=m->next;
    free(m->target);
    free(m->original);
    free(m);
   }
   else {
    m->prev->next=m->next;
   }
   privmsg(fd,from,"alias deleted");
   return;
  }
 }
 privmsg(fd,from,"alias not found");
 return;
}
*/

struct alias *getalias_h(char *msg) {
 return leetgetalias(getkey_h(msg),msg);
}

void c_alias_h(int fd,char *from,char *line) {
 char *derp=strchr(line,' ');
 struct alias *tmp;
 if(!derp) {
  if((tmp=getalias_h(line)) != NULL) {
   privmsg(fd,from,tmp->target);
  } else {
   privmsg(fd,from,"not an alias.");
  }
  return;
 }
 *derp=0;
 derp++;
 setkey_h(line,derp);
}

//hash table version

void c_kill(int fd,char *from,char *line) {
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

void c_pid(int fd,char *from) {
 char tmp[512];
 snprintf(tmp,sizeof(tmp)-1,"pid: %d",getpid());
 privmsg(fd,from,tmp);
}

void c_id(int fd,char *from) {
 char tmp[512];
 snprintf(tmp,sizeof(tmp)-1,"u:%d g:%d eu:%d eg:%d",getuid(),getgid(),geteuid(),getegid());
 privmsg(fd,from,tmp);
}

void c_leetuntail(int fd,char *from,char *line) {
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
   c_untail(fd,frm,file);
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

void c_tailunlock(int fd,char *from,char *file) {
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

void c_untail(int fd,char *from, char *file) {
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
 fdd=open(file,O_WRONLY|O_NONBLOCK|O_APPEND|O_CREAT,0640);//HAVE to open named pipes as nonblocking.
 if(fdd == -1) {
  snprintf(tmp,sizeof(tmp)-1,"%s: (%s) fd:%d",strerror(errno),file,fdd);
  privmsg(fd,from,tmp);
  return 0;
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

void c_leetappend(int fd,char *from,char *msg) {
 unsigned short nl;
 char *file=msg;
 char *snl=0;
 char *line=0;
 if((snl=strchr(msg,' '))) {
  *snl=0;
  snl++;
  if((line=strchr(snl,' '))) {
   *line=0;
   line++;
  }
 }
 if(!snl || !line) {
  privmsg(fd,from,"usage: !leetappend file EOL-char-dec line-to-put");
  return;
 }
 nl=((snl[0]-'0')*10)+((snl[1]-'0'));
 append_file(fd,from,file,line,nl);
}

void c_tails(int fd,char *from) {
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
   snprintf(tmp,l,"%s [i:%d] @ %ld (%d) --[%s(%02x)]--> %s",tailf[i].file,tailf[i].inode,ftell(tailf[i].fp),tailf[i].lines,x,tailf[i].opt,tailf[i].to);
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

void c_record(int fd,char *from,char *line) {
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

void c_rawrecord(int fd,char *from,char *line) {
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


void c_leetsetout(int fd,char *from,char *msg) {
 if(redirect_to_fd != -1) close(redirect_to_fd);
 redirect_to_fd=open(msg+3,((msg[0]-'0')*100) + ((msg[1]-'0')*10) + (msg[2]-'0'),022);
 if(redirect_to_fd == -1) {
  privmsg(fd,from,"failed to open file as redirect:");
  privmsg(fd,from,msg+3);
 }
}

void c_linelimit(int fd,char *from,char *msg) {
 char tmp[256];
 if(!msg) {
  snprintf(tmp,255,"current spam line limit: %d (debug: %d)",line_limit,debug);
  privmsg(fd,from,tmp);
 }
 else {
  if(atoi(msg) > 1) {
   line_limit=atoi(msg);
   snprintf(tmp,255,"spam line limit set to: %d",line_limit);
   privmsg(fd,from,tmp);
  } else {
   privmsg(fd,from,"please set the limit to > 1... oi. (btw, debug has been flipped)");
   debug^=1;
  }
 }
}

void c_resetout(int fd,char *from) {
 redirect_to_fd=-1;
 privmsg(fd,from,"output reset");
}

void message_handler(int fd,char *from,char *nick,char *msg,int redones) {
 struct alias *m;
 char *tmp2;
 char tmp[512];
 int sz;
 //debug_time(fd,from);
 for(sz=0;shitlist[sz];sz++) {
  if(!strcmp(shitlist[sz],nick)) {
   return;
  }
 }
 if(redirect_to_fd != -1) {
  fd=redirect_to_fd;
 }
 if(recording) {
  debug_time(fd,from,"writing to log...");
  append_file(fd,"raw",LOG,msg,'\n');
  debug_time(fd,from,"finished writing to log.");
 }
 if(!strncmp(msg,segnick,strlen(segnick))) {
  if(msg[strlen(segnick)] == ',' || msg[strlen(segnick)] == ':') {
   if(msg[strlen(segnick)+1] == ' ') {
    msg+=strlen(segnick)+1;
    msg[0]='!';
   }
  }
 }
 if(*msg != '!') {
  return;
 }

 //this section could probably be made a LOT shorter with
 //an array of structs that contain command and function pointer.
 //... meh. it'd just be a LITTLE bit shorter.

 // this still seems horrible though. all those constants in there that are just strlen()s...
 if(!strncmp(msg,"!leetsetout ",12)) {
  c_leetsetout(fd,from,msg+12);
 }
 else if(!strncmp(msg,"!whoareyou",10) && !msg[10]) {
  privmsg(fd,from,segnick);
 }
 else if(!strncmp(msg,"!whoami",7) && !msg[7]) {
  privmsg(fd,from,nick);
 }
 else if(!strncmp(msg,"!whereami",9) && !msg[9]) {
  privmsg(fd,from,from);
 }
 else if(!strncmp(msg,"!resetout",9) && !msg[9]) {
  c_resetout(fd,from);
 }
 else if(!strncmp(msg,"!botup",6) && !msg[6]) {
  c_botup(fd,from);
 }
 else if(!strncmp(msg,"!linelimit",10) && (!msg[10] || msg[10] == ' ')) {
  c_linelimit(fd,from,*(msg+10)?msg+11:0);  
 }
 else if(!strncmp(msg,"!tailunlock ",12)) {
  c_tailunlock(fd,from,msg+12);
 }
 else if(!strncmp(msg,"!changetail ",12)) {
  c_changetail(fd,from,msg+12);
 }
 else if(!strncmp(msg,"!tails",6) && !msg[6]) {
  c_tails(fd,from);
 }
 else if(!strncmp(msg,"!record ",8)) {
  c_record(fd,from,msg+8);
 }
 else if(!strncmp(msg,"!rawrecord ",11)) {
  c_rawrecord(fd,from,msg+11);
 }
 else if(!strncmp(msg,"!leettail ",10)) {
  c_leettail(fd,from,msg+10);
 }
 else if(!strncmp(msg,"!leetuntail ",12)) {
  c_leetuntail(fd,from,msg+12);
 }
 else if(!strncmp(msg,"!leetappend ",12)) {
  c_leetappend(fd,from,msg+12);
 }
 else if(!strncmp(msg,"!untail ",8)) {
  c_untail(fd,from,msg+8);
 }
 else if(!strncmp(msg,"!raw ",5)) {
  tmp2=malloc(strlen(msg)-5+3);
  snprintf(tmp2,strlen(msg)-5+2,"%s\r\n",msg+5);
  mywrite(fd,tmp2);
  free(tmp2);
  //write(fd,msg+5,strlen(msg)-5);
  //write(fd,"\r\n",2);
 }
 else if(!strncmp(msg,"!say ",5)) {
  privmsg(fd,from,msg+5);
 }
 else if(!strncmp(msg,"!id",3) && !msg[3]) {
  c_id(fd,from);
 }
 else if(!strncmp(msg,"!pid",4) && !msg[4]) {
  c_pid(fd,from);
 }
 else if(!strncmp(msg,"!kill ",6)) {
  c_kill(fd,from,msg+6);
 }
 else if(!strncmp(msg,"!alias ",7)) {
  c_alias_h(fd,from,msg+7);
 }
/* else if(!strncmp(msg,"!rmalias ",9)) {
  c_rmalias(fd,from,msg+9); //need to make a hashtable version
 }*/
 else if(!strncmp(msg,"!aliases",8) && (!msg[8] || msg[8] == ' ')) {
  c_aliases_h(fd,from,*(msg+8)?msg+9:0);
 }

 else if(redones < 5) {
  debug_time(fd,from,"checking aliases...");
  if((m=getalias_h(msg)) != NULL) {
   sz=(strlen(msg)-strlen(m->original)+strlen(m->target)+1);
   redo=format_magic(fd,from,nick,m->target,*(msg+strlen(m->original)+1)=='\n'?"":(msg+strlen(m->original)+1));
   message_handler(fd,from,nick,redo,redones+1);
   free(redo);
   redo=0;
   return;
  }
  debug_time(fd,from,"finished checking aliases. not found.");
  redo=0;
  snprintf(tmp,sizeof(tmp),"unknown command: %s",msg);
  privmsg(fd,from,tmp);
 }
 if(redones >=5) {
  privmsg(fd,from,"too much recursion.");
 }
}

void line_handler(int fd,char *line) {//this should be built into the libary?
 char *s=line;
 char *nick=0,*name=0,*host=0;
 char *t=0,*u=0;
 if(strchr(line,'\r')) *strchr(line,'\r')=0;
 if(strchr(line,'\n')) *strchr(line,'\n')=0;
 //:nick!name@host MERP DERP :message
 //strchr doesn't like null pointers. :/ why not just take them and return null?
 //check that I haven't gone past the end of the string? nah. it should take care of itself.
 if(recording_raw) {
  append_file(fd,"epoch",RAWLOG,line,'\n');
 }
 if((nick=strchr(line,':'))) {
  *nick=0;
  nick++;
  if((name=strchr(nick,'!'))) {
   *name=0;
   name++;
   if((host=strchr(name,'@'))) {
    *host=0;
    host++;
    if((s=strchr(host,' '))) {
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
   }
  }
 }
 //printf("<%s!%s@%s> '%s' '%s' '%s'\n",nick,name,host,s,t,u);
 if(s && t && u) {
  if(!strcmp(s,"PRIVMSG")) {
   u++;
   if(*t == '#')//channel.
    message_handler(fd,t,nick,u,0);
   else
    message_handler(fd,nick,nick,u,0);
  }
 }
 if(s && nick && t) {
  if(!strcmp(s,"JOIN")) {
   ircmode(fd,t+1,"+v",nick);//why t+1? it starts with :?
  }
  if(!strcmp(s,"MODE")) {
   if(u) {
    if(*u == '-') {//auto-give modes back that are removed in front of segfault.
     *u='+';
     ircmode(fd,t,u,"");//u contains the nick the mode is being removed from.
    }
   }
  }
//:Ishikawa-!~epoch@localhost NICK :checking
  if(!strcmp(s,"NICK")) {
   if(!strcmp(nick,segnick)) {
    free(segnick);
    segnick=strdup(t+1);
   }
  }
 }
}

int main(int argc,char *argv[]) {
 int fd;
 struct passwd *pwd;
 int c;
 redirect_to_fd=-1;
 debug=0;
 lines_sent=0;
 line_limit=25;
 recording=0;
 recording_raw=0;
 start_time=time(0);
 htkl=0;
 redo=0;
 inittable();
 segnick=strdup(NICK);
 printf("starting segfault...\n");
 if(!getuid() || !geteuid()) {
  pwd=getpwnam("segfault");
  if(!pwd) { printf("I'm running with euid or uid of 0 and I can't find myself."); return 0; }
  setgroups(0,0);
  setgid(pwd->pw_gid);
  setuid(pwd->pw_uid);
 }
 for(c=0;c<MAXTAILS;c++) tailf[c].fp=0;
 fd=ircConnect(SERVER,PORT,argc>1?argv[1]:"SegFault","segfault segfault segfault :segfault");
 startup_stuff(fd);
 return runit(fd,line_handler,extra_handler);
}
