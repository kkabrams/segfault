#include <stdio.h>
#include "hashtable.h"

extern char **environ;

int main(int argc,char *argv[]) {
 struct hashtable ht;
 int i;
 char *name;
 char *value;
 inittable(&ht,65535);
 for(i=0;environ[i];i++) {
  name=strdup(environ[i]);
  if((value=strchr(name,'=') )){
   *value=0;
   value++;
  }
  ht_setkey(&ht,name,value);
  free(name);
 }
 printf("PATH='%s'\n",ht_getvalue(&ht,"PATH"));
 return 0;
}
