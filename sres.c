/*  Copyright 2014, Mehdi Denou, Thomas Cadeau
    mehdi.denou@bull.net
    thomas.cadeau@ext.bull.net

 * sres is a wrapper to Slurm scontrol. It allows any user
 * to make reservation (and to modify/delete it) for himself.
 * The binary need to be own by a system user which must be a Slurm user
 * with "slurm_operator" rights and to have the setuid bit. 
 * 
 * Limitations: 
 * _ Reservation can only be created with user, accounts are not supported
 * _ sres doesn't support reservations made for several users
 * _ sres doesn't support -flag parameters
 *
 * WARNING: 
 * _ sres doesn't limit/check duration and number of ressources
 * */
 
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#define DEBUG 0

/* Find uid given a username */
int useruid (char * user) {

  int ret;
  size_t bufsize;
  char * buf;
  struct passwd pwd;
  struct passwd * result;

    if (DEBUG){
        printf("search for %s\n",user);
    }

    bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == -1){
      bufsize = 16384; 
    }
    
    buf = malloc(bufsize);
    if (buf == NULL) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }

    ret = getpwnam_r(user, &pwd, buf, bufsize, &result);

    if (result == NULL) {
      if (ret == 0) {
        printf("sres: error: user not found, err = %d\n", ret);
        return -1;
      } else {
        errno = ret;
        perror("getpwnam_r()");
        return -1;
      }
    }
    return pwd.pw_uid;
}

/* Find flag in option */
/* -1 if found, 0 if not */
int flagopt(int argc, char* newarg[]){
    int i;
    const char * pattern1 = "fl";
    for ( i = 2; i < argc; i++ ){
      if (strlen(newarg[i])>2 && strncasecmp(pattern1, newarg[i], 2) == 0) {
          return -1;
      }
    }
    return 0;
}

/* Find and check user in option */
/* -1 not found, 0 found and ok, 1 found but not good one, -2 if several users */
int useropt(int argc, char* newarg[]){
    char *user;
    int find =- 1;
    int uid, realuid, i;
    const char * pattern1 = "user=";
    const char * pattern2 = "users=";
    
    /* find "user=" in argv */
    for ( i = 2; i < argc; i++ ){
      if (strlen(newarg[i])>5 && strncasecmp(pattern1, newarg[i], 5) == 0) {
          user = malloc(sizeof(newarg[i]) - 5);
          strncpy(user, newarg[i] + 5, strlen(newarg[i]) - 5);
          find = 0;
          break;
      }
      if (strlen(newarg[i])>6 && strncasecmp(pattern2, newarg[i], 6) == 0) {
          user = malloc(sizeof(newarg[i]) - 6);
          strncpy(user, newarg[i] + 6, strlen(newarg[i]) - 6);
          find = 0;
          break;
      }
    }
    if (find == -1)
        return -1;

    for (i = 0; i < strlen(user); i++){
      if (user[i] == ','){
        free(user);
        return -2;
      }
    }

    uid = getuid();

    /* get uid of the user in reservation */
    realuid = useruid(user);
    free(user);

    if (realuid == -1 ) {
      return 1;
    }
    /* DEBUG */
    if (DEBUG){
      printf("uid = %d, given uid = %d\n", uid, realuid);
    }
    /* Check if the user is requesting a reservation for himself or not  */
    if (uid != realuid){
      return 1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int i, testuid, ret, uid, realuid;
    int find = -1;
    const char * pattern3 = "reservation=";
    const char * pattern4 = "res=";
    char * user;
    char * newarg[(sizeof(char *) * (argc + 1))];
    char * command;
    char * res_id;
    char buff[512];
    FILE * file;
    char commandline[] = "scontrol show res %s |grep Users |awk '{print $1}' 2>&1";

    /* Sanity check */
    if (argc == 1) {
        printf("sres: error: No arguments !\n");
        return EXIT_FAILURE;
    }
   
    if (newarg == NULL) {
        printf("sres: error: Not enough memory\n");
        return EXIT_FAILURE;
    }
    
    newarg[0]="scontrol";
    memcpy(&newarg[1], &argv[1], sizeof(char *) * argc);
    newarg[argc+1] = NULL;
   
    if (strcmp("create",newarg[1]) != 0 && 
        strcmp("update",newarg[1]) != 0 &&
        strcmp("delete",newarg[1]) != 0) {
        printf("sres: error: only create, update or delete operations are allowed\n");
        return EXIT_FAILURE;
    }
    
    if  ((newarg[2] == NULL) ||
        (strncmp("res",newarg[2], 3) != 0 && 
        strncmp("reservation",newarg[2], 11) != 0))
    {
        printf("sres: error: only reservation can be configured with %s\n",argv[0]);
        return EXIT_FAILURE;
    }

    if (flagopt(argc,argv) == -1) {
        printf("sres: error: flags are not supported\n");
        return EXIT_FAILURE;
    }

    testuid = useropt(argc,argv);
    if (testuid == 1){
      printf("sres: error: you cannot create/modify/delete reservation "
             "for other users\n");
      return EXIT_FAILURE;
    }
    if (testuid == -2){
        printf("sres: error: you must indicate only one username: yours\n");
      return EXIT_FAILURE;
    }

    /* In case of deleting or upgrading: check if the reservation belongs
     * to the user or not */
    if (strcmp("delete",newarg[1]) == 0 || strcmp("update",newarg[1]) == 0){
      find =- 1;
      for ( i = 2; i < argc; i++ ){
        if (strlen(newarg[i])>11 && strncasecmp(pattern3, newarg[i], 11) == 0){
          res_id = malloc(sizeof(newarg[i]) - 11);
          strncpy(res_id, newarg[i]+11, strlen(newarg[i]) - 11);
          find = 0;
          break;
        }
        if (strlen(newarg[i])>4 && strncasecmp(pattern4, newarg[i], 4) == 0 ){
          res_id = malloc(sizeof(newarg[i]) - 4);
          strncpy(res_id, newarg[i]+4, strlen(newarg[i]) - 4);
          find = 0;
          break;
        }
      }
      if (find == -1){
          printf("sres: error: you must indicate the reservation with "
                 "reservation= or res=\n");
          return EXIT_FAILURE;
      }

      command = malloc(strlen(res_id) + strlen(commandline));
      sprintf(command, commandline, res_id);
      file = popen(command, "r");
      if (file == NULL){
          printf("sres: error: Fail launching command scontrol show res\n");
          return EXIT_FAILURE;
      }

      /* some works with FILE */
      i = 0;

      while(fgets(buff, sizeof(buff), file) != NULL){
        if (DEBUG){
            printf("%s", buff);
        }
        i++;
        if (strlen(buff) <= 6) {
          printf("sres: error: There is no owner ie:user of the reservation\n");
          return EXIT_FAILURE;
        }
        user = malloc(strlen(buff) - 6);
        strncpy(user, buff+6, strlen(buff) - 7);
      }
      
      if (i < 1) {
        printf("sres: error: The reservation does not exist\n");
        return EXIT_FAILURE;
      }
      
      if (i > 1) {
      /* Should never happen */
        printf("sres: error: Find several reservations\n");
        return EXIT_FAILURE;
      }

      for (i = 0; i < strlen(user); i++){
        if (user[i] == ','){
          printf("sres: error: There is several owners of the reservation\n");
          return EXIT_FAILURE;
        }
      }

      uid = getuid();
      /* get uid of the user in reservation */
      realuid = useruid(user);
      if (realuid == -1 ) {
        return EXIT_FAILURE;
      }

      /* DEBUG */
      if (DEBUG){
        printf("uid = %d, given uid = %d\n", uid, realuid);
      }
      /* Check if the user is requesting a reservation for himself or not  */
      if (uid != realuid){
          printf("sres: error: you cannot create/modify/delete reservation "
                 "for other users\n");
          return EXIT_FAILURE;
      }

      /* free memory */
      free(command);
      free(res_id);
      free(user);
      pclose(file);
    
    } else {
        if (testuid == -1){
          printf("sres: error: You must indicate your user name\n");
          return EXIT_FAILURE;
        }
    }

    /* launch scontrol */
    ret = execvp("scontrol", newarg);
    if (ret != 0) {
      return EXIT_FAILURE;
    }
    else
      return EXIT_SUCCESS;
}
