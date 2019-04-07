#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#define __need_timer_t
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <syslog.h>

//epoch's libraries.
#include <irc.h>
#include <hashtable.h>

#define RECURSE_LIMIT 10

//#define free(a) do{printf("freeing %p line:%d\n",(void *)a,__LINE__);if(a) free(a);}while(0);

/*// just in case your system doesn't have strndup
char *strndup(char *s,int l) {
 char *r=strdup(s);
 r[l]=0;
 return r;
}
*/

//might want to change some of these.
#define TSIZE			65535
#define SERVER			"127.0.0.1"
#define PORT			"6667"
#define NICK			"SegFault" //override with argv[1]
#define MYUSER			"segfault"
#define LINES_SENT_LIMIT	1
#define LINELEN			400
#define SEGHOMELEN		1024
#define RAWLOG			"/home/segfault/files/rawlog"
#define LOG			"/home/segfault/files/log"
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

#define PRIVMSG_LINE_LIMIT	0

struct user *myuser;
char pid[6];
char time_str[16];
char mode_magic;
char snooty;
char message_handler_trace;
int start_time;
char *redo;
int redirect_to_fd;
int line_limit;
int debug;
timer_t timer;
int lines_sent;
unsigned long oldtime;
int maxtails;
int currentmaxtails;
char seghome[SEGHOMELEN];

struct hashtable alias;
struct hashtable builtin;

union hack {
 void (*func)(int,...);
 void *data;
};

void (*func)(int fd,...);

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

int message_handler(int fd,char *from,struct user *user,char *msg,int redones);
void c_leetuntail(int fd,char *from,char *line,...);

//this function isn't with the rest of them because... meh.
char *tailmode_to_txt(int mode) {
 char *modes="recmbsnf";
 int i,j=0;
 char *m=malloc(strlen(modes));
 for(i=0;i<strlen(modes);i++)
  if(mode & 1<<i)
   m[j++]=modes[i];
 m[j]=0;
 return m;
}

void mywrite(int fd,char *b) {
 int r;
 if(!b || fd <0) return;
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

void privmsg(int fd,char *who,char *msg) {
 int i=0;
 char *chunk,*hrm;
 int sz;
 int cs;
 int count=0;
 if(!who || !msg) return;
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
  count++;
  free(hrm);
  free(chunk);
  if(count > PRIVMSG_LINE_LIMIT) break;
 }
}

char *esca(char *s,char *r) {
 char *t;
 int l=0;
 char *u;
 for(t=s;*t;t++) if(strchr(r,*t)) l++;
 l+=strlen(s)+1;
 t=malloc(l);
 for(u=t;*s;s++) {
  if(strchr(r,*s)) {
   *u='\\';
   u++;
  }
  *u=*s;
  u++;
 }
 *u=0;//null it off
 return t;
}

char *escahack(char *s) {//for single quotes
 char *t;
 int l=0;
 char *u;
 for(t=s;*t;t++) if(*t == '\'') l+=3;
 l+=strlen(s)+1;
 t=malloc(l);
 for(u=t;*s;s++) {
  if(*s == '\'') {
   *u='\'';
   u++;
   *u='\\';
   u++;
   *u='\'';
   u++;
  }
  *u=*s;
  u++;
 }
 *u=0;//null it off
 return t;
}

//try to shorten this up sometime...
char *format_magic(int fd,char *from,struct user *user,char *orig_fmt,char *arg) {
 char *magic[256];
 int i=0,j=1,sz=0,c=1,d=0;
 char *output,*fmt,*argCopy;
 char *plzhold;
 char **args,**notargs;
 char *argN[10],randC[10][2]={"0","1","2","3","4","5","6","7","8","9"};
 snprintf(time_str,sizeof(time_str)-1,"%d",time(0));
 for(i=0;i<256;i++) {
  magic[i]=0;
 }
 if(!arg) arg="";//does anything depend on this weird thing?
 if(!(argCopy=strdup(arg))) return 0;
 argN[0]=argCopy;
 for(j=1;(argCopy=strchr(argCopy,' ')) && j < 10;j++) {
  *argCopy=0;//null it out!
  argCopy++;
  argN[j]=argCopy;
 }
/*
 for(j=1,argN[0]=argCopy;argCopy[i] && j<10;i++) {
  if(argCopy[i] == ' ') {
   argN[j]=argCopy+i;
   argN[j][0]=0;
   argN[j]++;
   j++;
  }
 }
*/
 for(;j<10;j++) {
  argN[j]="(null)";//fill up the rest to prevent null deref.
 }

 magic['r']=-1;//magic!
 magic['$']=-2;//more magic!
 magic['n']=(user->nick?user->nick:"user->nick");
 magic['u']=(user->user?user->user:"user->user");
 magic['h']=(user->host?user->host:"user->host");
 magic['m']=(myuser->nick?myuser->nick:"myuser->nick");
 magic['~']=seghome;
 magic['f']=(from?from:"from");
 magic['p']=pid;
 magic['t']=time_str;
 magic['s']=(arg?arg:"arg");
 magic['A']="\x01";//for ctcp and action
 magic['C']="\x03";//for colors
 magic['0']=argN[0];
 magic['1']=argN[1];
 magic['2']=argN[2];
 magic['3']=argN[3];
 magic['4']=argN[4];
 magic['5']=argN[5];
 magic['6']=argN[6];
 magic['7']=argN[7];
 magic['8']=argN[8];
 magic['9']=argN[9];
 magic['q']=(arg?escahack(arg):"escarg");
 magic['Q']=(arg?esca(arg,"\""):"escarg");
 magic['%']="%";

 if(!orig_fmt) return 0;
 if(!(fmt=strdup(orig_fmt))) return 0;
 for(i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   if(magic[fmt[i]]) c++;
  }
 }
 args=malloc((sizeof(char *)) * (c + 1));
 notargs=malloc((sizeof(char *)) * (c + 2));
 c=0;
 for(j=0,i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   d=1;
   if(magic[fmt[i]] == -1) {
    args[c]=randC[rand()%10];
   } else if(magic[fmt[i]] == -2) {
    if((plzhold=strchr(fmt+i+1,'='))) {
     *plzhold=0;
     args[c]=getenv(fmt+i+1);
     if(!args[c]) { args[c]="ENV VAR NOT FOUND"; }
     *plzhold='=';
     d=(plzhold - (fmt + i - 1));
    } else {
     args[c]="BROKEN ENV VAR REFERENCE";
    }
   } else if(magic[fmt[i]] > 0) {
    args[c]=magic[fmt[i]];
   } else {
    args[c]="INVALID MAGIC";
   }
   fmt[i-1]=0;
   if(!(fmt+j)) exit(68);
   if(!(notargs[c]=strdup(fmt+j))) exit(66);
   if(!args[c]) exit(200+c);
   sz+=strlen(args[c]);
   sz+=strlen(notargs[c]);
   c++;
   j=i+d;
  }
 }
 if(!(fmt+j)) exit(69);
 if(!(notargs[c]=strdup(fmt+j))) exit(67);
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
 free(argN[0]);
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
//feature creature
void extra_handler(int fd) {
 int tmpo,i;
 char tmp[BS+1];
 char *tmp2;
 //if(oldtime == time(0) && lines_sent > LINES_SENT_LIMIT) {//if it is still the same second, skip this function.
 // return;
 //} else {
 // lines_sent=0;
 //}
 //oldtime=time(0);//this might fix it?
 if(redirect_to_fd != -1) {
  fd=redirect_to_fd;
 }
 for(i=0;i<currentmaxtails;i++) {//RIP THIS SHIT OUT SOMETIME
  if(tailf[i].fp) {
   if(feof(tailf[i].fp)) {
    clearerr(tailf[i].fp);
    if(tailf[i].opt & TAILO_CLOSE) {//use for eval
     c_leetuntail(fd,tailf[i].to,tailf[i].file,0);
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
   if(tailf[i].lines != -1) {//if the tail isn't locked due to spam limit.
    if(fgets(tmp,BS-1,tailf[i].fp) == NULL) {//if there isn't anything to read right now...
     if(tailf[i].lines != 0 && (tailf[i].opt & TAILO_ENDMSG)) {
      privmsg(fd,tailf[i].to,"---------- TAILO_ENDMSG border ----------");
     }
     //snprintf(tmp2,sizeof(tmp)-1,"tailf[%d] (%s): errno: %d, ferror: %d",i,tailf[i].file,errno,ferror(tailf[i].fp));
     //privmsg(fd,tailf[i].to,tmp2);
     tailf[i].lines=0;
    }
    else {//if there was something to read.
     tailf[i].lines++;
     if(strchr(tmp,'\r')) *strchr(tmp,'\r')=0;
     if(strchr(tmp,'\n')) *strchr(tmp,'\n')=0;
     if(tailf[i].opt & TAILO_EVAL) {//eval
      if(tailf[i].opt & TAILO_FORMAT) {
       tmp2=format_magic(fd,tailf[i].to,tailf[i].user,tmp,tailf[i].args);//the line read is the format string.
       message_handler(fd,tailf[i].to,tailf[i].user,tmp2,1);
       free(tmp2);
      } else {
       //this will crash.
       tmp2=strdup(tmp);
       message_handler(fd,tailf[i].to,tailf[i].user,tmp2,1);
       //printf("tmp2 in crashing place: %p\n",tmp2);
      }
      //printf("OHAI. WE SURVIVED!\n");
     }
     if(tailf[i].opt & TAILO_RAW) {//raw
      tmp2=malloc(strlen(tmp)+4);
      snprintf(tmp2,strlen(tmp)+3,"%s\r\n",tmp);
      mywrite(fd,tmp2);
      free(tmp2);
     }
     if(tailf[i].opt & TAILO_MSG) {//just msg the lines.
      if(tailf[i].opt & TAILO_FORMAT && tailf[i].args) {
       tmp2=format_magic(fd,tailf[i].to,tailf[i].user,tailf[i].args,tmp);//the args is the format string.
       privmsg(fd,tailf[i].to,tmp2);
      } else {
       privmsg(fd,tailf[i].to,tmp);
      }
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

void file_tail(int fd,char *from,char *file,char *args,int opt,struct user *user) {
 int i;
 int fdd;
 char tmp[256];
 struct stat st;
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
  snprintf(tmp,sizeof(tmp)-1,"file_tail opened file '%s' with fd: %d / %d / %d",file,fdd,currentmaxtails,maxtails);
  privmsg(fd,"#cmd",tmp);
 }
 fstat(fdd,&st); // <-- is this needed?
 /*
 for(j=0;j<maxtails;j++) {
  if(tailf[j].fp && tailf[j].file && tailf[j].inode) {
   if(tailf[j].inode == st.st_ino) {
    if(debug) privmsg(fd,from,"THIS FILE IS ALREADY BEING TAILED ELSEWHERE!");
    //i=j;break;//reuse it. make sure to add free()ing code for this.
    //return;//don't tail files twice
    //;just add another tail for it
 }}}*/
 i=fdd;//hack hack hack. :P //I forgot I was using this.
 if(i >= currentmaxtails) { currentmaxtails=i+1;}
 if(!(tailf[i].fp=fdopen(fdd,"r"))) {
  snprintf(tmp,sizeof(tmp),"file_tail: failed to fdopen(%s)\n",file);
  privmsg(fd,from,tmp);
 } else {
  fcntl(fdd,F_SETFL,O_NONBLOCK);
  if(!(opt & TAILO_BEGIN)) {
   eofp(tailf[i].fp);
  }
  if(!from) exit(73);
  if(tailf[i].to) {
   snprintf(tmp,sizeof(tmp),"tailf[%d].to: %s from: %s",i,tailf[i].to,from);
   privmsg(fd,"#default",tmp);
   free(tailf[i].to); //commenting this out stopped a buffer overflow. >_> weird.
   tailf[i].to=0;
  }
  tailf[i].to=strdup(from);//if this properly free()d before being assigned to?
  if(!tailf[i].to) {
   mywrite(fd,"QUIT :malloc error 3!!!\r\n");
   return;
  }
  if(!file) exit(74);
  tailf[i].file=strdup(file);
  if(!tailf[i].file) {
   mywrite(fd,"QUIT :malloc error 4!!!\r\n");
   return;
  }
  tailf[i].opt=opt;
  tailf[i].inode=st.st_ino;
  if(!(tailf[i].user=malloc(sizeof(struct user)))) exit(__LINE__);
  if(!(tailf[i].user->nick=strdup(user->nick))) exit(__LINE__);
  if(!(tailf[i].user->user=strdup(user->user))) exit(__LINE__);
  if(!(tailf[i].user->host=strdup(user->host))) exit(__LINE__);
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
 snprintf(tmp,sizeof(tmp)-1,"botup: %llu",time(0)-start_time);
 privmsg(fd,from,tmp);
}

void c_leettail(int fd,char *from,char *file,struct user *user,...) {
 int a;
 int b;
 int c;
 int d;
 int n;
 if(!file) {
  privmsg(fd,from,"!leettail NNfilename [format]");
  privmsg(fd,from,"!leettail NNNfilename [format]");
  privmsg(fd,from,"!leettail NN#channel:filename [format]");
  privmsg(fd,from,"!leettail NNN#channel:filename [format]");
  return;
 }
 if(file[0]) {
 if(file[1]) {
 if(file[2]) {
  //
 }else {
  privmsg(fd,from,"usage1: !leettail NN filename");
  privmsg(fd,from,"usage2: !leettail NNN filename");
  return;
 }}}
 a=file[0]-'0';
 b=file[1]-'0';
 c=file[2]-'0';
 if(c >= 0 && c <= 9) {
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
 if(!line) {
  privmsg(fd,from,"usage: !changetail filename target tailmode");
  return;
 }
 if((merp=strchr(line,' '))) {
  *merp=0;
  merp++;
  if((mode=strchr(merp,' '))) {
   *mode=0;
   mode++;
  }
 }
 if((fdd=open(line,O_RDONLY|O_NONBLOCK,0)) == -1) {
  snprintf(tmp,sizeof(tmp)-1,"changetail: %s: (%s) fd:%d",strerror(errno),line,fdd);
  privmsg(fd,"#cmd",tmp);
  return;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"changetail opened file '%s' with fd: %d / %d / %d\n",line,fdd,currentmaxtails,maxtails);
  privmsg(fd,"#cmd",tmp);
 }
 fstat(fdd,&st);
 close(fdd);
 for(i=0;i<currentmaxtails;i++) {
  //if(tailf[i].file == 0) return mywrite(fd,"QUIT :tailf[i].file == 0 in changetail\r\n");
  if(tailf[i].file) {
   if(!strcmp(tailf[i].file,line) || tailf[i].inode == st.st_ino) {
    free(tailf[i].to);
    tailf[i].to=0;
    tailf[i].user=user;//memory leak?
    if(!merp) exit(76);
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
 if(!line) {
  privmsg(fd,from,"usage: !builtin command [address]");
  return;
 }
 if((addr=strchr(line,' '))) {
  *addr=0;
  addr++;
  if(!sscanf(addr,"%08x",&address)) {
   privmsg(fd,from,"sscanf didn't get an address.");
   return;
  }
  snprintf(tmp,sizeof(tmp)-1,"address read for %s: %08x",function,address);
  privmsg(fd,from,tmp);
  ht_setkey(&builtin,function,(void *)address);
 } else {
  address=(unsigned int)ht_getvalue(&builtin,function);
  snprintf(tmp,sizeof(tmp)-1,"builtin %s's address: %08x",function,address);
  privmsg(fd,from,tmp);
 }
 return;
}

void c_builtins(int fd,char *from,char *line,...) {
 char tmp[512];
 struct hitem *hi;
 struct entry *m;
 int i,j=0,k=0;
 if(!line){
  snprintf(tmp,sizeof(tmp),"There are %d builtins in this bot's hash table.",builtin.kl);
  privmsg(fd,from,tmp);
  privmsg(fd,from,"usage: !builtins [search-term]");
  return;
 }
 for(i=0;i<builtin.kl;i++) {
  if(debug) {
   snprintf(tmp,sizeof(tmp)-1,"builtins in bucket: %d",builtin.keys[i]);
   privmsg(fd,from,tmp);
  }
  hi=builtin.bucket[builtin.keys[i]];
  if(hi) {
   for(m=hi->ll;m;m=m->next) {
    if(strcasestr(m->original,line) || *line=='*') {
     snprintf(tmp,sizeof(tmp)-1," %s -> %p",m->original,m->target);
     privmsg(fd,from,tmp);
     j++;
    }
    k++;
   }
  } else {
   privmsg(fd,from,"what the fuck? this bucket isn't set!");
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"found %d of %d in builtins",j,k);
 privmsg(fd,from,tmp);
}

void c_amnesia(int fd,char *from,...) {//forget aliases
 ht_freevalues(&alias);
 ht_destroy(&alias);
 inittable(&alias,TSIZE);
}

void c_lobotomy(int fd,char *from,...) {//forget builtins
 ht_destroy(&builtin);
 inittable(&builtin,TSIZE);
 //don't put this as a builtin by default. :P gotta hack that out.
}

void c_aliases_h(int fd,char *from,char *line,...) {
 char tmp[512];
 struct entry *m;
 struct hitem *hi;
 int i,j=0,k=0;
 if(!line){
  snprintf(tmp,sizeof(tmp)-1,"There are %d aliases in this bot's hash table.",alias.kl);
  privmsg(fd,from,tmp);
  privmsg(fd,from,"usage: !aliases [search-term]");
  return;
 }
 for(i=0;i<alias.kl;i++) {
  hi=alias.bucket[alias.keys[i]];
  if(hi) {
   for(m=hi->ll;m;m=m->next) {
    if(strcasestr(m->target,line) || strcasestr(m->original,line)) {
     snprintf(tmp,sizeof(tmp)-1," %s -> %s",m->original,(char *)m->target);
     privmsg(fd,from,tmp);
     j++;
    }
    k++;
   }
  } else {
   privmsg(fd,"#default","holy shit. epoch found that bug.\n");
  }
 }
 snprintf(tmp,sizeof(tmp)-1,"found %d of %d aliases",j,k);
 privmsg(fd,from,tmp);
}

void c_alias_h(int fd,char *from,char *line,...) {
 char tmps[512];
 if(!line) {
  printf("usage: !alias command [other_command]");
  return;
 }
 char *derp=strchr(line,' ');
//why is this using entry? it should be hitem?
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
 if(!derp) exit(77);
 if(!*derp) exit(78);
 ht_setkey(&alias,line,strdup(derp));
}

void c_kill(int fd,char *from,char *line,...) {
 if(!line) {
  privmsg(fd,from,"usage: !kill signum pid");
  return;
 }
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
 if(!line) {
  privmsg(fd,from,"usage: !leetuntail [target|*] filename");
  return;
 }
 char *frm=line;
 char *file;
 int i;
 if((file=strchr(line,' '))) {
  *file=0;
  file++;
 } else {
  file=line;
  frm=".";
 }
 if(file) {
  for(i=0;i<currentmaxtails;i++) {
   if(tailf[i].fp &&
      !strcmp(tailf[i].file,file) &&
      ((!strcmp(tailf[i].to,from) || *frm=='*') ||
      (!strcmp(tailf[i].to,frm) && *frm=='.'))) {
    eofp(tailf[i].fp);
    if(fclose(tailf[i].fp) == -1) {
     privmsg(fd,from,"well, shit. fclose failed somehow.");
    }
    tailf[i].fp=0;
    free(tailf[i].to);
    tailf[i].to=0;
    free(tailf[i].file);
    tailf[i].file=0;
    return;
   }
  }
 }
}

void c_istaillocked(int fd,char *from,char *file,...) {
 char *msg=0;
 int i;
 if((msg=strchr(file,' '))) {
  *msg=0;
  msg++;
 }
 for(i=0;i<currentmaxtails;i++) {
  if(tailf[i].fp) {
   if(!strcmp(file,tailf[i].file)) {
    if(tailf[i].lines == -1) {
     privmsg(fd,from,msg?msg:"file is locked.");
     return;
    }
   }
  }
 }
}

void c_tailunlock(int fd,char *from,char *file,...) {
 int i;
 if(!file) {
  privmsg(fd,from,"usage: !tailunlock filename");
  return;
 }
 for(i=0;i<currentmaxtails;i++) {
  if(tailf[i].fp) {
   if(!strcmp(file,tailf[i].file)) {
    tailf[i].lines=0;
    return;
   }
  }
 }
 privmsg(fd,from,"file not found in the tail list.");
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
  snprintf(tmp,sizeof(tmp)-1,"append_file: %s: (%s) fd:%d",strerror(errno),file,fdd);
  privmsg(fd,from,tmp);
  return 0;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"append_file opened file '%s' with fd: %d / %d / %d\n",file,fdd,currentmaxtails,maxtails);
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
 fprintf(fp,"%s\n",line);
 fclose(fp);
 return 1;
}

void c_leetappend(int fd,char *from,char *msg,...) {
 unsigned short nl;
 if(!msg) {
  privmsg(fd,from,"usage: !leetappend file EOL-char-dec line-to-put");
  return;
 }
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
 for(i=0;i<currentmaxtails;i++) {
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

char *recording,recording_raw;

void c_record(int fd,char *from,char *line,...) {
 char tmp[512];
 if(!line) {
  privmsg(fd,from,"usage: !record format_string");
  if(recording) {
    snprintf(tmp,sizeof(tmp)-1,"currently recording to: %s with format string: %s",LOG,recording);
    privmsg(fd,from,tmp);
  }
  else privmsg(fd,from,"not recording.");
  return;
 }
 if(*line == '0' && *(line+1) == 0) {
  if(recording) free(recording);
  privmsg(fd,from,"no longer recording IRC.");
  recording=0;
  return;
 }
 else {
  recording=strdup(line);
  if(unlink(LOG) == -1) {
   privmsg(fd,from,"failed to unlink log file!");
   privmsg(fd,from,strerror(errno));
  }
  privmsg(fd,from,"recording IRC.");
 }
 privmsg(fd,from,recording?"1":"0");
}

void c_rawrecord(int fd,char *from,char *line,...) {
 char tmp[512];
 if(!line) {
  privmsg(fd,from,"usage: !rawrecord 0|1");
  if(recording_raw) {
    snprintf(tmp,sizeof(tmp)-1,"currently recording to: %s",RAWLOG);
    privmsg(fd,from,tmp);
  }
  else privmsg(fd,from,"not recording.");
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
 if(!msg) {
  privmsg(fd,from,"usage: don't");
//  privmsg(fd,from,"usage: NNNfilename");
  return;
 }
 char tmp[512];
 if(redirect_to_fd != -1) close(redirect_to_fd);
 redirect_to_fd=open(msg+3,((msg[0]-'0')*100) + ((msg[1]-'0')*10) + (msg[2]-'0'),022);
 if(redirect_to_fd == -1) {
  snprintf(tmp,sizeof(tmp)-1,"leetsetout: %s: (%s) fd:%d",strerror(errno),msg+3,redirect_to_fd);
  privmsg(fd,"#cmd",tmp);
  return;
 }
 if(debug) {
  snprintf(tmp,sizeof(tmp)-1,"leetsetout opened file '%s' with fd: %d / %d / %d\n",msg+3,redirect_to_fd,currentmaxtails,maxtails);
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
  if(msg[0] == 'b') {
   snooty^=1;
   if(snooty) privmsg(fd,from,"I will only listen to you if you address me directly.");
   else privmsg(fd,from,"I will listen to any commands, no need to address me directly.");
  }
  if(msg[0] == 'c') {
   message_handler_trace^=1;
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
   //privmsg(fd,from,"something else!");
  }
 }
}

void c_resetout(int fd,char *from,...) {
 redirect_to_fd=-1;
 privmsg(fd,from,"output reset");
}

void c_raw(int fd,char *from,char *msg,...) {
 char *tmp2;
 if(!msg) {
  privmsg(fd,from,"usage: !raw stuff-to-send-to-server");
  return;
 }
 tmp2=malloc(strlen(msg)+4);
 snprintf(tmp2,strlen(msg)+3,"%s\r\n",msg);
 mywrite(fd,tmp2);
 free(tmp2);
}

void c_say(int fd,char *from,char *msg,...) {
 if(!msg) msg="usage: !say message";
 privmsg(fd,from,msg);
}

void c_nick(int fd,char *from,char *msg,...) {
 if(!msg) {
  privmsg(fd,from,"usage: !nick new-nick-to-try");
  return;
 }
 free(myuser->nick);
 if(!msg) exit(78);
 myuser->nick=strdup(msg);
 irc_nick(fd,myuser->nick);
}

int message_handler(int fd,char *from,struct user *user,char *msg,int redones) {
 struct entry *m;
 union hack lol;
 char lambdad;
 char *command;
 char *oldcommand;
 char *args;
 char tmp[512];
 char *tmp2;
 char to_me;
 int len;
 int sz;
// printf("message_handler: entry: message: '%s' redones: %d\n",msg,redones);
 if(redones && message_handler_trace) {
  snprintf(tmp,sizeof(tmp)-1,"trace: n: %s u: %s h: %s message '%s' redones: '%d'",user->nick,user->user,user->host,msg,redones);
  privmsg(fd,from,tmp);
 }
 if(redirect_to_fd != -1) {
  fd=redirect_to_fd;
 }
 if(recording && redones == 0) {
  debug_time(fd,from,"writing to log...");
  //snprintf(tmp,sizeof(tmp)-1,"<%s> %s",user->nick,msg);
  tmp2=format_magic(fd,from,user,recording,msg);
  append_file(fd,user->nick,LOG,tmp2,'\n');
  free(tmp2);
  debug_time(fd,from,"finished writing to log.");
 }

 //there's a bug here when directing a string to a nick but the * isn't part of the nick.
 len=strchr(msg,'*')?strchr(msg,'*')-msg:strlen(myuser->nick);

 to_me=0;
 if(!strncasecmp(msg,myuser->nick,len)) {
  if(msg[len] == '*') len++;
  if(msg[len] == ',' || msg[len] == ':') {
   if(msg[len+1] == ' ') {
    msg+=len+2;
    //privmsg(fd,from,"addressed directly!");
    to_me=1;
   }
  }
 }
 if(snooty && !to_me && !redones) return 1;//eated

 if(!msg) exit(71);
 oldcommand=strdup(msg);
 if(!oldcommand) exit(72);
 command=oldcommand;
 if(*command == '\x01') {
  command[strlen(command)-1]=0;//remove the end \x01
  *command=';';
 }
 //access control goes here
 if(strcmp(user->host,"127.0.0.1") && //me
    strcmp(myuser->nick,user->nick)
    ) {
    return 1;//I want this to claim eatedness.
 }
// if(!strncmp(command,"s/",2)) {
//  command[1]=' ';
// }
 while(!strncmp(command,"lambda ",7)) {
  command+=7;
  command=format_magic(fd,from,user,command,command);
  lambdad=1;
 }
 if((args=strchr(command,' '))) {
  *args=0;
  args++;
 }
 if((lol.data=ht_getvalue(&builtin,command))) {
  func=lol.func;
  func(fd,from,args,user);
//  if(lambdad) {
//   free(args);
//  }
 }
 else if(redones < RECURSE_LIMIT) {
  debug_time(fd,from,"checking aliases...");
  //command--;// :>
  if((m=ht_getnode(&alias,command))) {
   if(rand()%1000 == 0 && redones == 0) {
     privmsg(fd,from,"I don't want to run that command right now.");
     free(oldcommand);
     return 1;//count this as being handled.
   }
   //sz=(strlen(command)-strlen(m->original)+strlen(m->target)+1);// what is this used for?
//??? why not use args?
   redo=format_magic(fd,from,user,m->target,(command+strlen(m->original)+1));
//   redo=format_magic(fd,from,user,m->target,args);
   message_handler(fd,from,user,redo,redones+1);
   free(redo);
   redo=0;
   free(oldcommand);
   //printf("message_handler: leaving: msg: %s redones: %d\n",msg,redones);
   return 1;
  }
  debug_time(fd,from,"finished checking aliases. not found.");
  redo=0;
  if(debug) {
   snprintf(tmp,sizeof(tmp)-1,"command not found: '%s' with args '%s'",command,args);
   privmsg(fd,from,tmp);
  }
 }
 if(redones >= RECURSE_LIMIT) {
  privmsg(fd,from,"I don't know if I'll ever get out of this alias hole you're telling me to dig. Fuck this.");
 }
 free(oldcommand);
 return 0;//I guess we didn't find anyway. let it fall back to generic handlers.
 //printf("message_handler: leaving: msg: %s redones: %d\n",msg,redones);
}

void line_handler(int fd,char *line) {//this should be built into the libary?
 char tmp[512];
 struct user *user;
 int i;
 if(!(user=malloc(sizeof(struct user)))) exit(__LINE__);
 memset(user,0,sizeof(struct user));
 printf("line: %s\n",line);
 if(recording_raw) {
  append_file(fd,"epoch",RAWLOG,line,'\n');
 }
 char *line2=strdup(line);
// char *line3;
 struct entry *tmp2;
 //line will be mangled by the cutter.
 char **a=line_cutter(fd,line,user);
 if(!user->user && a[0]) { //server message.
//:armitage.hacking.allowed.org 353 asdf = #default :@SegFault @FreeArtMan @foobaz @wall @Lamb3_13 @gizmore @blackh0le
  strcpy(tmp,"!");
  strcat(tmp,a[0]);
  if(ht_getnode(&alias,tmp) == NULL && ht_getnode(&alias,"!###") != NULL) {
   strcpy(tmp,"!###");
//   privmsg(fd,*a[1]=='#'?a[1]:user->nick,a[0]);
  }
  if((tmp2=ht_getnode(&alias,tmp)) != NULL) {
   strcat(tmp," ");
   //int freenick=0,freeuser=0,freehost=0;
   //if(!user->nick) { if(!(user->nick=strdup("$UNDEF_NICK"))) exit(__LINE__); freenick=1;}
   //if(!user->user) { if(!(user->user=strdup("$UNDEF_USER"))) exit(__LINE__); freeuser=1;}
   //if(!user->host) { if(!(user->host=strdup("$UNDEF_HOST"))) exit(__LINE__); freehost=1;}
   strcat(tmp,line2);
   message_handler(fd,"epoch",myuser,tmp,1);
   //if(freenick) free(user->nick);
   //if(freeuser) free(user->user);
   //if(freehost) free(user->host);
  }
 }
 free(line2);
 if(a[0] && a[1] && a[2]) {
  if(!strcmp(a[0],"PRIVMSG") || !strcmp(a[0],"NOTICE")) {
   if(strcmp(user->nick,myuser->nick)) {
    if(message_handler(fd,*a[1]=='#'?a[1]:user->nick,user,a[2],0)) {
     free(user);//we handled this message. don't let it fall into more generic handlers.
     free(a);
     return;
    }
   }
   else {
    if(debug) privmsg(fd,*a[1]=='#'?a[1]:user->nick,"This server has an echo");
   }
  }
 }
 if(a[0] && user->nick && a[1]) {
  strcpy(tmp,";");
  strcat(tmp,a[0]);
  if((ht_getnode(&alias,tmp)) != NULL) {
   for(i=0;a[i];i++) {
    strcat(tmp," ");
    strcat(tmp,a[i]);
   }
   message_handler(fd,"#cmd",user,tmp,1);
  }// else {
   //privmsg(fd,"#cmd","couldn't find alias for:");
   //privmsg(fd,"#cmd",a[0]);
  //}
  if(!strcmp(a[0],"JOIN")) {
   irc_mode(fd,a[1],"+o",user->nick);
  }
//  if(!strcmp(a[0],"KICK") && !strcmp(a[2],"epoch") && strcmp(user->nick,myuser->nick)) {
//   snprintf(tmp,sizeof(tmp)-1,"KILL %s :don't fuck with my bro.\r\n",user->nick);
//   mywrite(fd,tmp);
//  }
  if(!strcmp(a[0],"KICK") && !strcmp(a[2],myuser->nick)) {
   snprintf(tmp,sizeof(tmp)-1,"JOIN %s\r\n",a[1]);
   mywrite(fd,tmp);
//   snprintf(tmp,sizeof(tmp)-1,"KILL %s :don't fuck with me.\r\n",user->nick);
//   mywrite(fd,tmp);
  }
  if(!strcmp(user->nick,myuser->nick) && !strcmp(a[0],"PART") ) {
   snprintf(tmp,sizeof(tmp)-1,"JOIN %s\r\n",a[1]);
   mywrite(fd,tmp);
  }
  if(!strcmp(a[0],"MODE") && mode_magic) {
   if(strcmp(user->nick,myuser->nick)) {
    if(a[2]) {
     if(*a[2] == '-') {//auto-give modes back that are removed in front of segfault.
      *a[2]='+';
      irc_mode(fd,a[1],a[2],a[3]?a[3]:"");
     }
     else if(*a[2] == '+' && a[2][1] == 'b') {//only remove bans.
      *a[2]='-';
      irc_mode(fd,a[1],a[2],a[3]?a[3]:"");
     }
    }
   }
   if(!strcmp(user->nick,myuser->nick)) {//if someone is making me set a mode and I see it.
    if(a[2]) {
     if(*a[2] == '+' && a[2][1] == 'b') {//segfault doesn't ban.
      *a[2]='-';
      irc_mode(fd,a[1],a[2],a[3]?a[3]:"");
     }
    }
   }
  }
  if(!strcmp(a[0],"NICK") && a[1]) {
   if(!strcmp(user->nick,myuser->nick)) {
    free(myuser->nick);
    if(!a[1]) exit(79);
    if(!(myuser->nick=strdup(a[1]))) exit(179);
   }
  }
 }
 free(user);
 free(a);
}

void sigpipe_handler(int sig) {
 syslog(LOG_WARNING,"SegFault received a sigpipe! (ignoring it)");
}

int main(int argc,char *argv[]) {
 signal(SIGSTOP,exit);//prevent pausing
 signal(SIGPIPE,sigpipe_handler);
 int fd;
 srand(time(0) + getpid());
 struct passwd *pwd;
 struct rlimit nofile;
 char *s,*p;
 int c;
 inittable(&builtin,TSIZE);
#define CMDCHR "!"
#define BUILDIN(a,b) ht_setkey(&builtin,CMDCHR a,(void *)((union hack){(void (*)(int,...))b}.data))
 BUILDIN("builtin",c_builtin);
 BUILDIN("builtins",c_builtins);
 BUILDIN("raw",c_raw);
 BUILDIN("leetsetout",c_leetsetout);
 BUILDIN("resetout",c_resetout);
 BUILDIN("botup",c_botup);
 BUILDIN("linelimit",c_linelimit);
 BUILDIN("nick",c_nick);
 BUILDIN("tailunlock",c_tailunlock);
 BUILDIN("istaillocked",c_istaillocked);
 BUILDIN("changetail",c_changetail);
 BUILDIN("tails",c_tails);
 BUILDIN("record",c_record);
 BUILDIN("rawrecord",c_rawrecord);
 BUILDIN("leettail",c_leettail);
 BUILDIN("leetuntail",c_leetuntail);
 BUILDIN("leetappend",c_leetappend);
 BUILDIN("say",c_say);
 BUILDIN("id",c_id);
 BUILDIN("kill",c_kill);
 BUILDIN("alias",c_alias_h);
 BUILDIN("aliases",c_aliases_h);
 BUILDIN("lobotomy",c_lobotomy);
 BUILDIN("amnesia",c_amnesia);
 mode_magic=1;
 snooty=0;
 message_handler_trace=0;
 redirect_to_fd=-1;
 debug=0;
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
  currentmaxtails=4;
  tailf=malloc(sizeof(struct tail) * (maxtails + 1));
 }
 myuser=malloc(sizeof(struct user));
 myuser->nick=strdup(argc>1?argv[1]:NICK);
 myuser->host="I_dunno";//???
 snprintf(pid,6,"%d",getpid());
 printf("starting segfault...\n");
 if(!getuid() || !geteuid()) {
  s=getenv("seguser");
  pwd=getpwnam(s?s:MYUSER);
  if(!pwd) { printf("I'm running with euid or uid of 0 and I can't find myself.\n"); return 0; }
  setgroups(0,0);
  setgid(pwd->pw_gid);
  setuid(pwd->pw_uid);
 } else {
  pwd=getpwuid(getuid());
  printf("going to run segfault as user %s\n",pwd->pw_name);
  if(!pwd) { printf("well, shit. I don't know who I am."); return 0; }
 }
 myuser->user=strdup(pwd->pw_name);
 for(c=0;c<maxtails;c++) tailf[c].fp=0;
 s=getenv("segserver"); s=s?s:SERVER;
 p=getenv("segport"); p=p?p:PORT;
 printf("connecting to: %s port %s\n",s,p);
 fd=serverConnect(getenv("segserver")?getenv("segserver"):SERVER,
                  getenv("segport")?getenv("segport"):PORT);
//               myuser->nick,
//               "segfault segfault segfault :segfault");
 printf("cd %s\n",pwd->pw_dir);
 chdir(getenv("seghome")?getenv("seghome"):pwd->pw_dir);
 getcwd(seghome,SEGHOMELEN);
 prestartup_stuff(fd);
 if(fd == -1) return 0;
 printf("server fd: %d\n",fd);
 return runit(fd,line_handler,extra_handler);
}
