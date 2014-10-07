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
#include "interpreter.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

#define STDOUT 1 
#define STDIN 0 

typedef struct bgjob_l {
  pid_t pid;
  int jid;
  char* cmdline;
  int status;
  struct bgjob_l* next;
} bgjobL;

/* the pids of the background processes */
bgjobL *bgjobs = NULL;

typedef struct alias_l {
  char* name;
  char* cmdline;
  struct alias_l* next;
} aliasL;

aliasL* aliases = NULL;

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

// add to linked list
static bgjobL* AddJob(int child_pid, char* cmdline, int status) {
  bgjobL* job = (bgjobL*) malloc(sizeof(bgjobL));

  job->pid = child_pid;
  job->cmdline = strdup(cmdline);
  job->status = status;
  bgjobL* node = bgjobs;
  bgjobL* found = NULL;
  int jid = 1;
  int difference;

  if (node != NULL && node->jid > 1) {
    // indicate the loop should not run
    // found = NULL indicates that it should be placed at the beginning
    node = NULL;
  }

  // find the first unused positive integer
  while (node != NULL) {
    difference = 0;
    if (node->next != NULL) {
      difference = node->next->jid - node->jid;
    } else {
      // indicate that there is space
      difference = 2;
    }
    if (difference > 1) {
      // use the next open jid
      jid = node->jid + 1;
      found = node;
      break;
    }
    node = node->next;
  }

  job->jid = jid;

  if (found == NULL) {
    // beginning of list
    job->next = bgjobs;
    bgjobs = job;
  } else {
    // after found element
    job->next = found->next;
    found->next = job;
  }

  return job;
}

void RunCmdBg(commandT* cmd)
{
  int child_pid = fork();

  /* The processes split here */

  if(child_pid < 0) {
    printf("failed to fork\n");
    fflush(stdout);
    return;
  }

  if (child_pid) {
    AddJob(child_pid, cmd->cmdline, 0);
  } else {
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);

    sigprocmask(SIG_BLOCK, &mask, NULL);

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
    if (pipe(fd) < 0) {
      printf("failed to create pipe\n");
      fflush(stdout);
    }
  }

  int child_pid = fork();

  /* The processes split here */

  if(child_pid < 0) {
    printf("failed to fork\n");
    fflush(stdout);
    return;
  }

  if (child_pid) {
    // close incoming (if available) now that we're done reading it
    if (incoming != -1) {
      close(incoming);
    }

    // close the write end of fd (if available) now that we're done writing to it
    if (n) {
      close(fd[1]);  
    }

    waitpid(child_pid, NULL, 0);
  } else {
    // Map incoming pipe fd to STDIN (if available)
    if (incoming != -1) {
      if (dup2(incoming, STDIN) < 0) {
        printf("failed to map pipe to STDIN\n");
        fflush(stdout);
        exit(2);
      }
      close(incoming);
    }

    // Map STDOUT to outgoing pipe fd (if available)
    if (n) {
      if (dup2(fd[1], STDOUT) < 0) {
        printf("failed to map STDOUT to pipe\n");
        fflush(stdout);
        exit(2);
      }
      close(fd[1]);
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
  int out;
  if(strcmp(cmd->name, ">") == 0) {
    out = open(cmd->name, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
    dup2(out,STDOUT); 
    close(out); 
    execv(cmd->name, cmd->argv);
  }
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
    int in; 
    if (strcmp(cmd->name, "<") == 0) {
      in = open(cmd->name, O_RDONLY); 
      dup2(in, STDIN); 
      close(in);
      execv(cmd->name, cmd->argv);
    }
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
      fflush(stdout);
      return;
    }

    if(child_pid !=0) {
      int status = 0;
      waitpid(child_pid, &status, WUNTRACED);
      if (WIFSTOPPED(status)) {
        bgjobL* job = AddJob(child_pid, cmd->cmdline, 2);
        printf("[%d]   Stopped                 %s\n", job->jid, job->cmdline);
        fflush(stdout);
      }
    } else {
      if (cmd->is_redirect_in) {
        int in = open(cmd->redirect_in, O_RDONLY);

        if (in < 0) {
          printf("failed to open %s for reading", cmd->redirect_in);
          fflush(stdout);
          exit(2);
        }

        dup2(in, STDIN);
        close(in);
      }

      if (cmd->is_redirect_out) {
        int out = open(cmd->redirect_out, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);

        if (out < 0) {
          printf("failed to open %s for writing", cmd->redirect_out);
          fflush(stdout);
          exit(2);
        }

        dup2(out, STDOUT);
        close(out);
      }

      execv(cmd->name, cmd->argv);
      exit(2);
    }
  }
}

static bool IsBuiltIn(char* cmd)
{
  /* Check if the command should be dispatched by alias */

  aliasL* node = aliases;
  while (node != NULL) {
    if (strcmp(cmd, node->name) == 0) {
      return TRUE;
    }
    node = node->next;
  }

  /* Check against builtin command names */

  if (
    strcmp(cmd, "cd") == 0 ||
    strcmp(cmd, "jobs") == 0 ||
    strcmp(cmd, "fg") == 0 ||
    strcmp(cmd, "bg") == 0 ||
    strcmp(cmd, "alias") == 0 ||
    strcmp(cmd, "unalias") == 0
  ) {
    return TRUE;
  }

  return FALSE;
}

/* Handles coalescing if a termination is caught. */

static void CheckJobTermination(bgjobL** prev, bgjobL** node)
{
  int status;
  int terminated_pid = waitpid((*node)->pid, &status, WNOHANG);
  if (terminated_pid > 0) {
    if (*prev != NULL) {
      (*prev)->next = (*node)->next;
    } else {
      bgjobs = (*node)->next;
    }
    (*node)->status = 1;
  }
}

/* Handles freeing if the status is set to 1 (terminated). */

static void CleanupJob(bgjobL* node)
{
  if (node->status == 1) {
    free(node->cmdline);
    free(node);
  }
}

static void RunBuiltInCmd(commandT* cmd)
{
  /* If applicable, reformat command with the aliases substituted */

  aliasL* node = aliases;
  while (node != NULL) {
    if (strcmp(cmd->argv[0], node->name) == 0) {
      char** argv = (char**) malloc(sizeof(char*) * cmd->argc); // store new argv for concatenating into a new command

      /* populate argv with aliased replacements as long as the alias's command is trailed by a space */

      int argi;
      int aliasNext = TRUE;
      size_t length = 0;

      for (argi = 0; argi < cmd->argc; argi++) {
        node = NULL;

        if (aliasNext) {
          node = aliases;
          while (node != NULL) {
            if (strcmp(cmd->argv[argi], node->name) == 0) {
              break;
            }
            node = node->next;
          }
        }

        if (node != NULL) {
          argv[argi] = strdup(node->cmdline);
          length += strlen(argv[argi]) + 1;
          if (node->cmdline[strlen(node->cmdline) - 1] != ' ') {
            aliasNext = FALSE;
          }
        } else {
          argv[argi] = strdup(cmd->argv[argi]);
          length += strlen(argv[argi]) + 1;
          aliasNext = FALSE;
        }
      }

      /* create new command string */

      char* cmdline = (char*) malloc(sizeof(char) * length);
      strcpy(cmdline, "");

      for (argi = 0; argi < cmd->argc; argi++) {
        strcat(cmdline, argv[argi]);
        strcat(cmdline, " ");
      }

      /* handle as any other command */

      Interpret(cmdline);

      return; // return to shell
    }
    node = node->next;
  }

  /* Builtin commands implemented here */

  if (strcmp(cmd->argv[0], "cd") == 0) {
    char* path;

    if (cmd->argc > 1) {
      path = cmd->argv[1];
    } else {
      path = getenv("HOME");
    }

    if (chdir(path) < 0) {
      printf("failed to change directory\n");
      fflush(stdout);
    }
  } else if (strcmp(cmd->argv[0], "jobs") == 0) {
    bgjobL* prev = NULL;
    bgjobL* node = bgjobs;
    while (node != NULL) {
      CheckJobTermination(&prev, &node);

      switch (node->status) {
      case 0:
        printf("[%d]   Running                 %s\n", node->jid, node->cmdline);
        break;
      case 1:
        printf("[%d]   Done                    %s\n", node->jid, node->cmdline);
        break;
      case 2:
        printf("[%d]   Stopped                 %s\n", node->jid, node->cmdline);
        break;
      }
      fflush(stdout);

      CleanupJob(node);

      prev = node;
      node = node->next;
    }
  } else if (strcmp(cmd->argv[0], "fg") == 0) {

  } else if (strcmp(cmd->argv[0], "bg") == 0) {

  } else if (strcmp(cmd->argv[0], "alias") == 0) {
    if (cmd->argc == 1) {
      /* Print Aliases */

      aliasL* node = aliases;
      while (node != NULL) {
        printf("alias %s='%s'\n", node->name, node->cmdline);
        node = node->next;
      }
    } else {
      /* Make Alias */

      size_t h = 0;
      while (cmd->cmdline[h] != 'a') { h++; }

      // assume that lias comes after 'a'
      h += 5;

      while (cmd->cmdline[h] == ' ') { h++; }

      size_t i = 0;

      while (cmd->cmdline[h + i] != '=') { i++; }

      char* name = (char*) malloc(sizeof(char) * (i + 1));
      strncpy(name, &(cmd->cmdline)[h], i);
      name[i] = '\0';

      while (cmd->cmdline[h + i] != '\'') { i++; }

      i++;

      size_t j = 0;

      while (cmd->cmdline[h + i + j] != '\'') { j++; }

      char* cmdline = (char*) malloc(sizeof(char) * (j + 1));
      strncpy(cmdline, &(cmd->cmdline)[h + i], j);
      cmdline[j] = '\0';

      aliasL* alias = (aliasL*) malloc(sizeof(aliasL));
      alias->name = name;
      alias->cmdline = cmdline;
      alias->next = aliases;
      aliases = alias;
    }
  } else if (strcmp(cmd->argv[0], "unalias") == 0) {
    aliasL* prev = NULL;
    aliasL* node = aliases;
    while (node != NULL) {
      if (strcmp(node->name, cmd->argv[1]) == 0) {
        if (prev != NULL) {
          prev->next = node->next;
        } else {
          aliases = node->next;
        }

        free(node->name);
        free(node->cmdline);
        free(node);

        node = NULL;
      } else {
        prev = node;
        node = node->next;
      }
    }
  }
}

void CheckJobs()
{
  bgjobL* prev = NULL;
  bgjobL* node = bgjobs;
  while (node != NULL) {
    CheckJobTermination(&prev, &node);

    if (node->status == 1) {
      printf("[%d]   Done                    %s\n", node->jid, node->cmdline);
      fflush(stdout);
    }

    CleanupJob(node);

    prev = node;
    node = node->next;
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

// void StopFgProc() {
//   printf("here\n");
//   int pid = waitpid(-1, NULL, WUNTRACED | WNOHANG);
//   printf("caught SIGTSTP for %d in %d\n", pid, getpid());
//   fflush(stdout);
//   AddJob(pid, "temp");
// }
