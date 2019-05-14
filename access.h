#ifndef _SEGFAULT_ACCESS_H_
#define _SEGFAULT_ACCESS_H_

//include this before. somehow I don't have sanity in irc.h I guess
//#include <irc.h>

char isallowed(char *from,struct user *user,struct user *myuser,char *line) {
 if(strcmp(user->host,"127.0.0.1") && //me
    strcmp(user->host,"localhost") && //me
    strcmp(myuser->nick,user->nick)//let the bot command itself.
    ) {
  return 0;
 }
 return 1;
}

#endif
