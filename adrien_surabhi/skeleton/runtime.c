/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  pid_t pid;
  struct bgjob_l* next;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);
/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if (n == 1) {
    RunCmdFork(cmd[0], TRUE);
    ReleaseCmdT(&cmd[0]);
  }
  else {
    RunCmdPipe(cmd[0], &cmd[1], n - 1, -1);
    for (i = 0; i < n; i++) {
      ReleaseCmdT(&cmd[i]);
    }
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

void RunCmdBg(commandT* cmd)
{
  int child_pid = fork();

  /* The processes split here */

  if(child_pid < 0) {
    printf("failed to fork\n");
    return;
  }

  if (child_pid) {
    bgjobL* job = (bgjobL*) malloc(sizeof(bgjobL));

    // add to linked list
    job->pid = child_pid;
    job->next = bgjobs;
    bgjobs = job;
  } else {
    execv(cmd->name, cmd->argv);
    exit(2);
  }
}

void RunCmdPipe(commandT* cmd, commandT** rest, int n, int incoming)
{
  int builtin = FALSE;

  if (IsBuiltIn(cmd->argv[0])) {
    builtin = TRUE;
  } else {
    if (!ResolveExternalCmd(cmd)) {
      printf("%s: command not found\n", cmd->argv[0]);
      fflush(stdout);

      if (incoming != -1) {
        close(incoming);
      }

      return;
    }
  }

  // Set up pipe if there are commands left to run
  int fd[2];
  if (n) {
    if (pipe(fd) == -1) {
      printf("failed to create pipe");
    }
  }

  int child_pid = fork();

  /* The processes split here */

  if(child_pid < 0) {
    printf("failed to fork\n");
    return;
  }

  if (child_pid) {
    waitpid(child_pid, NULL, 0);

    // close incoming (if available) now that we're done reading it
    if (incoming != -1) {
      close(incoming);
    }

    // close the write end of fd (if available) now that we're done writing to it
    if (n) {
      close(fd[1]);  
    }
  } else {
    // Map incoming pipe fd to STDIN (if available)
    if (incoming != -1) {
      if (dup2(incoming, 0) == -1) {
        printf("failed to map pipe to STDIN\n");
      }
    }

    // Map STDOUT to outgoing pipe fd (if available)
    if (n) {
      if (dup2(fd[1], 1) == -1) {
        printf("failed to map STDOUT to pipe\n");
      }
    }

    if (builtin) {
      RunBuiltInCmd(cmd);
      exit(0);
    } else {
      execv(cmd->name, cmd->argv);
      exit(2);
    }
  }

  if (n) {
    // run the next process
    RunCmdPipe(rest[0], &rest[1], n - 1, fd[0]); 
  }
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

static void Exec(commandT* cmd, bool forceFork)
{
  if (cmd->bg) {
    RunCmdBg(cmd);
  } else {
    int child_pid = fork();

    /* The processes split here */

    if(child_pid < 0) {
      printf("failed to fork\n");
      return;
    }

    if(child_pid !=0) {
      int status = 0;
      waitpid(child_pid, &status, 0);
      // printf("status: %d\n", WEXITSTATUS(status));
    } else {
      execv(cmd->name, cmd->argv);
      exit(2);
    }
  }
}

static bool IsBuiltIn(char* cmd)
{
  if (
    strcmp(cmd, "cd") == 0
  ) {
    return TRUE;
  }
  return FALSE;     
}


static void RunBuiltInCmd(commandT* cmd)
{
  if (strcmp(cmd->argv[0], "cd") == 0) {
    if(chdir(cmd->argv[1]) == -1) {
      printf("failed to change directory\n");
    }
  }
}

void CheckJobs()
{
  bgjobL* prev = NULL;
  bgjobL* job = bgjobs;
  while (job != NULL) {
    int terminated_pid = waitpid(job->pid, NULL, WNOHANG);
    if (terminated_pid > 0) {
      printf("terminated_pid: %d\n", terminated_pid);

      // Coalesce linked list
      if (prev != NULL) {
        prev->next = job->next;
      } else {
        bgjobs = job->next;
      }

      free(job);
    }
    prev = job;
    job = job->next;
  }
}


commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}
