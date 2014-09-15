#include <stdio.h>

int main(int argc,char *argv[]) {
 short in;
 FILE *fp;
 if(argc>1) {
  if(!(fp=fopen(argv[1],"r"))) {
   printf("file not found.\n");
   return 1;
  }
  while(1) {
   if((in=fgetc(fp)) == -1) {
    if(feof(fp)) {
     clearerr(fp);
    } else {
     printf("unknown error from fgetc()");
     return 2;
    }
   } else {
    printf("%c",in);
    fflush(stdout);
   }
  }
 } else {
  printf("usage: tailf filename\n");
 }
}
