#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hashtable.h"

/*
struct entry {//linked list node.
 char *original;
 char *target;
 struct entry *prev;// doubly linked list. why?
 struct entry *next;
};

struct hitem {//dunno why I don't just have this as a linked list.
 struct entry *ll;
};

struct hashtable {
 int kl;//number of keys in the table
 struct hitem **bucket;
 int *keys;
};
*/

unsigned short hash(char *v) {//maybe use a seeded rand()? :) Thanks FreeArtMan
 unsigned short h=0;
 h=((*v)<<8)+(v?*(v+1):0);
 srand(h);
 return rand();
}

void inittable(struct hashtable *ht,int tsize) {
 int i;
 ht->bucket=malloc(sizeof(char *)*tsize);
 ht->kl=0;
 ht->keys=malloc(sizeof(int *)*tsize);
 if(!ht) {
  fprintf(stderr,"malloc error 6 in hash table.\n");
  return;
 }
 for(i=0;i<tsize;i++) {
  ht->bucket[i]=0;
 }
}

//this seems too complicated.
int ht_setkey(struct hashtable *ht,char *key,char *value) {
 unsigned short h=hash(key);
 struct entry *tmp;
 int i;
 for(i=0;i<ht->kl;i++) {
  if(ht->keys[i]==h) break;
 }
 ht->keys[i]=h;
 ht->kl=(ht->kl)>i+1?ht->kl:i+1;
 if(!ht->bucket[h]) { //empty bucket!
  //add this to the list of used buckets so we can easily
  //use that list later for stuff.
  if(!(ht->bucket[h]=malloc(sizeof(struct hitem)))) return 1; 
  ht->bucket[h]->ll=0;
  //we now have a valid hashtable entry and a NULL ll in it.
  //don't bother with the new ll entry yet...
 }
 if((tmp=ll_getentry(ht->bucket[h]->ll,key)) != NULL) {
  //we found this alias in the ll. now to replace the value
  free(tmp->target);
  if(!(tmp->target=strdup(value))) return 2;
  return 0;
 }
 if(ht->bucket[h]->ll == NULL) {
  if(!(ht->bucket[h]->ll=malloc(sizeof(struct entry)))) return 3;
  ht->bucket[h]->ll->next=0;
  ht->bucket[h]->ll->prev=0;
  if(!(ht->bucket[h]->ll->original=strdup(key))) return 4;
  if(!(ht->bucket[h]->ll->target=strdup(value))) return 5;
 } else {
  //go to the end and add another entry to the ll.
  for(tmp=ht->bucket[h]->ll;tmp->next;tmp=tmp->next);
  if(!(tmp->next=malloc(sizeof(struct entry)))) return 6;
  tmp->next->prev=tmp;
  tmp=tmp->next;
  if(!(tmp->original=strdup(key))) return 7;
  if(!(tmp->target=strdup(value))) return 8;
  tmp->next=0;
 }
 return 0;
}

struct entry *ll_getentry(struct entry *start,char *msg) {
 struct entry *m;
 if(!msg) return NULL;
 if(!start) return NULL;
 for(m=start;m;m=m->next) {
  if(!strncmp(msg,m->original,strlen(m->original)) && (msg[strlen(m->original)]==' ' || msg[strlen(m->original)] == 0)) {//this allows !c to get called when using !c2 if !c2 is defined after !c. >_>
   return m;
  }
 }
 return NULL;
}

struct entry *ht_getentry(struct hashtable *ht,char *key) {
 unsigned short h=hash(key);
 if(!ht->bucket[h]) return NULL;
 return ht->bucket[h]->ll;
}

struct entry *ht_getnode(struct hashtable *ht,char *msg) {
 return ll_getentry(ht_getentry(ht,msg),msg);
}

//you'll probably want to use me.
char *ht_getvalue(struct hashtable *ht,char *msg) {
 struct entry *tmp=ll_getentry(ht_getentry(ht,msg),msg);
 return tmp?tmp->target:0;
}
