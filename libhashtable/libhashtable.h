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
unsigned short hash(char *v);//maybe use a seeded rand()? :) Thanks FreeArtMan
void inittable(struct hashtable *ht);
int ht_setkey(struct hashtable *ht,char *key,char *value);
struct entry *ll_getentry(struct entry *start,char *msg);
struct entry *ht_getentry(struct hashtable *ht,char *key);
struct entry *ht_getnode(struct hashtable *ht,char *msg);
char *ht_getvalue(struct hashtable *ht,char *msg);
