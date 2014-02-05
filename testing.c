#include <stdio.h>

char *format_magic(char *from,char *nick,char *fmt,char *arg);

int main(int argc,char *argv[]) {
 char *o=format_magic("#default","epoch",argv[1],argv[2]);
 printf("%s\n",o);
 return 0;
}

char *format_magic(char *from,char *nick,char *fmt,char *arg) {
 int i;
 int j;
 int c=1;
 for(i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   switch(fmt[i]) {
     case 0:
      printf("error! last character is a '%'!!!\n");
      exit(1);
     case 'u':
     case 'f':
     case 's':
      c++;
   }
  }
 }
 char **args=malloc((sizeof(char *)) * (c + 1));
 c=0;
 for(i=0;fmt[i];i++) {
  if(fmt[i] == '%') {
   i++;
   switch(fmt[i]) {
     case 0:
      printf("error! last character is a '%'!!!\n");
      exit(1);
     case 'u':
      args[c]=nick;
      fmt[i]='s';
      c++;
      break;
     case 'f':
      args[c]=from;
      fmt[i]='s';
      c++;
      break;
     case 's':
      args[c]=arg;
      c++;
      break;
   }
  }
 }
// args[c]=0;
// for(i=0;i<c;i++) {
//  printf("args[%d]=%s\n",i,args[i]);
// }
// printf("format string: %s\nc: %d\n",fmt,c);
 int sz=vprintf(fmt,args)+1;
// printf("\nsize: %d\n",sz);
 char *output=malloc(sz);
 vsnprintf(output,sz,fmt,args);
 return output;
}
