#include <stdio.h>
#include "libhashtable.h"

extern char **environ;

int main(int argc,char *argv[]) {
 struct hashtable ht;
 int i;
 char *name;
 char *value;
 inittable(&ht);
 for(i=0;environ[i];i++) {
  name=strdup(environ[i]);
  if((value=strchr(name,'=') )){
   *value=0;
   value++;
  }
  //printf("'%s' = '%s'\n",name,value);
  ht_setkey(&ht,name,value);
  free(name);
 }
 printf("PATH='%s'\n",ht_getvalue(&ht,"PATH"));
 return 0;
}
