/*
 * cush - the customizable shell.
 *
 * Developed by Godmar Back for CS 3214 Summer 2020 
 * Virginia Tech.
 */
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/wait.h>
#include <assert.h>

/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "termstate_management.h"
#include "signal_support.h"
#include "shell-ast.h"
#include "utils.h"

static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
           " -h            print this help\n",
           progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt */
static char *
build_prompt(void)
{
    return strdup("cush> ");
}

enum job_status
{
    FOREGROUND,    /* job is running in foreground.  Only one job can be
                       in the foreground state. */
    BACKGROUND,    /* job is running in background */
    STOPPED,       /* job is stopped via SIGSTOP */
    NEEDSTERMINAL, /* job is stopped because it was a background job
                       and requires exclusive terminal access */
};

struct job
{
    struct list_elem elem;          /* Link element for jobs list. */
    struct ast_pipeline *pipe;      /* The pipeline of commands this job represents */
    int jid;                        /* Job id. */
    enum job_status status;         /* Job status. */
    int num_processes_alive;        /* The number of processes that we know to be alive */
    struct termios saved_tty_state; /* The state of the terminal when this job was 
                                        stopped after having been in foreground */
    int pgid;                       /* Parent Group ID */
    bool isFinished;                /* determines weather the job is finished or not */
    bool wasKilled;                 /* determines weather the job was killed by a kill signal or not*/
    int totalProc;                  /*Number of total processes the job ever had*/
    int pids[10];                   /* pids of processes in this job group */
};

struct history
{
    struct list_elem elem; /* Link element for history list. */
    char *command;         /*History command*/
};


// Global Variable to quit shell
bool quit;
char homeDir[1024];

/* Utility functions for job list management.
 * We use 2 data structures: 
 * (a) an array jid2job to quickly find a job based on its id
 * (b) a linked list to support iteration
 */
#define MAXJOBS (1 << 16)
static struct list job_list;
static struct list history_list;

/*Function Declarations*/
static struct job *jid2job[MAXJOBS];
static void handle_child_status(pid_t pid, int status);
int get_pgid_from_jobId(int id);
bool checkInternalCommand(struct ast_command *cmd);
void runInternalCommand(struct ast_command *cmd);
void saveToHistory(char *cmdline);
void history_list_free(void);
void closePipes(int numPipes, int pipes[]);
void cleanUpJobsList(void);
void runCommand(struct ast_command_line *cmdline);
void runChildProcess(int currCommand, int numCommands, int numPipes, int j, struct job *jb, int pipefds[], char *comd, char **argg);
int strcompare(const char *str1, const char *str2);
char *CpyStringOver(char *s);

/* Return job corresponding to jid */
static struct job *
get_job_from_jid(int jid)
{
    if (jid > 0 && jid < MAXJOBS && jid2job[jid] != NULL)
        return jid2job[jid];

    return NULL;
}

/*Returns a corresponding job pointer to the given pid. The function goes through the 
jobs list and checks the pid array of each job to determine if a match is found.
If no match is found function returns NULL*/
static struct job *
get_job_from_pid(pid_t pid)
{
    struct job *jb;

    for (struct list_elem *e = list_begin(&job_list);
         e != list_end(&job_list);
         e = list_next(e))
    {
        jb = list_entry(e, struct job, elem);
        for (int i = 0; i < jb->totalProc; i++)
        {
            if (jb->pids[i] == pid)
            {
                return jb;
            }
        }
    }
    return NULL;
}

/* Add a new job to the job list */
static struct job *
add_job(struct ast_pipeline *pipe)
{
    struct job *job = malloc(sizeof *job);
    job->pipe = pipe;
    job->num_processes_alive = 0;
    job->status = FOREGROUND;
    job->totalProc = 0;
    job->isFinished = false;
    job->wasKilled = false;
    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++)
    {
        if (jid2job[i] == NULL)
        {
            jid2job[i] = job;
            job->jid = i;
            return job;
        }
    }
    fprintf(stderr, "Maximum number of jobs exceeded\n");
    abort();
    return NULL;
}
/* Delete a job.
 * This should be called only when all processes that were
 * forked for this job are known to have terminated.
 */
static void
delete_job(struct job *job)
{
    int jid = job->jid;
    assert(jid != -1);
    jid2job[jid]->jid = -1;
    jid2job[jid] = NULL;
    ast_pipeline_free(job->pipe);
    free(job);
}

static const char *
get_status(enum job_status status)
{
    switch (status)
    {
    case FOREGROUND:
        return "Foreground";
    case BACKGROUND:
        return "Running";
    case STOPPED:
        return "Stopped";
    case NEEDSTERMINAL:
        return "Stopped (tty)";
    default:
        return "Unknown";
    }
}

/* Print the command line that belongs to one job. */
static void
print_cmdline(struct ast_pipeline *pipeline)
{
    struct list_elem *e = list_begin(&pipeline->commands);
    for (; e != list_end(&pipeline->commands); e = list_next(e))
    {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        if (e != list_begin(&pipeline->commands))
            printf("| ");
        char **p = cmd->argv;
        printf("%s", *p++);
        while (*p)
            printf(" %s", *p++);
    }
}

/* Print a job */
static void
print_job(struct job *job)
{
    printf("[%d]\t%s\t\t(", job->jid, get_status(job->status));
    print_cmdline(job->pipe);
    printf(")\n");
}

/*
 * Suggested SIGCHLD handler.
 *
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * an already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD 
 * signal may be delivered for multiple children that have 
 * exited. All of them need to be reaped.
 */
static void
sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{

    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0)
    {
        handle_child_status(child, status);
    }
}

/* Wait for all processes in this job to complete, or for
 * the job no longer to be in the foreground.
 * You should call this function from a) where you wait for
 * jobs started without the &; and b) where you implement the
 * 'fg' command.
 * 
 * Implement handle_child_status such that it records the 
 * information obtained from waitpid() for pid 'child.'
 *
 * If a process exited, it must find the job to which it
 * belongs and decrement num_processes_alive.
 */
static void
wait_for_job(struct job *job)
{
    assert(signal_is_blocked(SIGCHLD));
    while (job->status == FOREGROUND && job->num_processes_alive > 0)
    {
        int status;
        pid_t child = waitpid(-1, &status, WUNTRACED);
        if (child != -1)
        {
            handle_child_status(child, status);
        }
    }
}

/* 
Handles the job status for the given pid and child status.
     */
static void
handle_child_status(pid_t pid, int status)
{

    assert(signal_is_blocked(SIGCHLD));

    /*Get a pointer to the job we are handling from the given pid*/
    struct job *jb = get_job_from_pid(pid);

    /*returns true if child exited normally*/
    if (WIFEXITED(status))
    { /*If child exited normally decrease the number of processes in the job*/
        jb->num_processes_alive = jb->num_processes_alive - 1;
    }
    /* returns true if child was CTRL Z'ed (Stopped)*/
    else if (WIFSTOPPED(status))
    {
        /*Change the job status to stopped*/
        jb->status = STOPPED;
        /*Save its terminals state*/
        termstate_save(&jb->saved_tty_state);
        /*Print the job and its status*/
        print_job(jb);
    }
    /*returns true if child was CTRL C'ed or was terminated with an error*/
    else if (WIFSIGNALED(status))
    {
        /*Make the number of processes 0 as the job was terminated*/
        jb->num_processes_alive = 0;
        /*Indicate that the job was killed abnormally*/
        jb->wasKilled = true;
        /*Get the exit status of the job with the use of a MACRO*/
        int exit_status = WTERMSIG(status);

        /*Go through different exit codes and print them*/
        if (exit_status == 11)
        {
            printf("segmentation fault\n");
        }
        if (exit_status == 8)
        {
            printf("floating point exception\n");
        }
        if (exit_status == 6)
        {
            printf("aborted\n");
        }
        if (exit_status == 9)
        {
            printf("killed\n");
        }
        if (exit_status == 15)
        {
            printf("terminated\n");
        }
    }
    /*Check to see if a job has finished*/
    if (jb->num_processes_alive == 0)
    {
        /*Indicate that the job has finished*/
        jb->isFinished = true;
        /*If the job was in the background print Done*/
        if (jb->status == BACKGROUND && !jb->wasKilled)
        {
            printf("\n[%d]    Done", jb->jid);
        }
    }
}
/*Replacement function for strcmp()*/
int strcompare(const char *str1, const char *str2)
{
    int s1;
    int s2;
    do
    {
        s1 = *str1++;
        s2 = *str2++;
        if (s1 == 0)
            break;
    } while (s1 == s2);
    return (s1 < s2) ? -1 : (s1 > s2);
}

/*Replacement function for strcpy()*/
char *CpyStringOver(char *s)
{
    // Dont work on null cases
    if (s == NULL)
    {
        return NULL;
    }
    int len = strlen(s);
    /*new string*/
    char *result = calloc(len + 1, sizeof(char));
    int i = 0;
    while (i < len)
    {
        result[i] = s[i];
        i++;
    }

    /*return copied string*/
    return result;
}

/*This functions returns the Parent group id to a corresponding process id. 
If the process id is the parent group id itself it returns that. returns -1 
if no pgid was found*/
int get_pgid_from_jobId(int id)
{
    for (struct list_elem *e = list_begin(&job_list);
         e != list_end(&job_list);
         e = list_next(e))
    {
        struct job *jb = list_entry(e, struct job, elem);

        if (jb->jid == id)
        {
            return jb->pgid;
        }
    }
    return -1;
}

/*
checks if the passed ast_command is an internal command.
Returns true if internal command is found.
Returns false if internal command not found.
*/
bool checkInternalCommand(struct ast_command *cmd)
{
    /*extracts *p*/
    char **p = cmd->argv;

    /*Compares the given command to the determined internal commands*/
    if (strcompare(*p, "jobs") == 0 || strcompare(*p, "fg") == 0 || strcompare(*p, "bg") == 0 || strcompare(*p, "stop") == 0 ||
        strcompare(*p, "kill") == 0 || strcompare(*p, "history") == 0 || strcompare(*p, "exit") == 0 || strcompare(*p, "cd") == 0)
    {
        return true;
    }
    else
        return false;
}

/*
runs the internal command passed to it
*/
void runInternalCommand(struct ast_command *cmd)
{
    char **p = cmd->argv;
    /*Compares then runs job command*/
    if (strcompare(*p, "jobs") == 0)
    {
        /*Loops through the jobs list to print each job*/
        for (struct list_elem *e = list_begin(&job_list);
             e != list_end(&job_list);
             e = list_next(e))
        {
            struct job *jb = list_entry(e, struct job, elem);
            /*Prints each job*/
            print_job(jb);
        }
    }
    /*Compares then runs fg command*/
    else if (strcompare(*p, "fg") == 0)
    {
        char *argg[10];
        int i = 0;
        while (*p)
        {
            argg[i] = *p++;
            i++;
        }
        argg[i] = NULL;
        /*extracts job number from string*/
        int jobId = atoi(argg[1]);
        /*get the pgid to send that group into the foreground*/
        int pgid = get_pgid_from_jobId(jobId);
        /*get a pointer to that specific job*/
        struct job *jb = get_job_from_pid(pgid);
        /*Change the job status to Foregorund*/
        jb->status = FOREGROUND;
        /*Print the commands initially run*/
        print_cmdline(jb->pipe);
        printf("\n");
        fflush(stdout);
        /*Send a sig cont signal to the process group*/
        killpg(pgid, SIGCONT);
        /*Block SigChld*/
        signal_block(17);
        /*give terminal control to job with */
        termstate_give_terminal_to(NULL, pgid);
        /*Wait for the job to complete*/
        wait_for_job(jb);
        /*After job is complete give control back to shell*/
        termstate_give_terminal_back_to_shell();
        /*Unblosk SigCHLD*/
        signal_unblock(17);
    }
    /*Compares then runs bg command*/
    else if (strcompare(*p, "bg") == 0)
    {
        char *argg[10];
        int i = 0;
        while (*p)
        {
            argg[i] = *p++;
            i++;
        }
        argg[i] = NULL;
        /*extracts job number from string*/
        int jobID = atoi(argg[1]);
        /*get the pgid from jobid*/
        int pgid = get_pgid_from_jobId(jobID);
        /*get a pointer to that specific job*/
        struct job *jb = get_job_from_pid(pgid);
        /*Change the job status to Background*/
        jb->status = BACKGROUND;
        /*Send a sig cont signal to the process group*/
        killpg(pgid, SIGCONT);
    }
    /*Compares then runs stop command*/
    else if (strcompare(*p, "stop") == 0)
    {
        char *argg[10];
        int i = 0;
        while (*p)
        {
            argg[i] = *p++;
            i++;
        }
        argg[i] = NULL;
        /*extracts job number from string*/
        int jobId = atoi(argg[1]);
        /*get the pgid from jobid*/
        int pgid = get_pgid_from_jobId(jobId);
        /*Send a sig stop signal to the process group*/
        killpg(pgid, SIGSTOP);
    }
    /*Compares then runs kill command*/
    else if (strcompare(*p, "kill") == 0)
    {
        char *argg[10];
        int i = 0;
        while (*p)
        {
            argg[i] = *p++;
            i++;
        }
        argg[i] = NULL;
        /*extracts job number from string*/
        int jobId = atoi(argg[1]);
        /*get the pgid from jobid*/
        int pgid = get_pgid_from_jobId(jobId);
        /*Send a sig term signal to the process group*/
        killpg(pgid, SIGKILL);
    }
    /*Compares then runs history command*/
    else if (strcompare(*p, "history") == 0)
    {
        //counter
        int i = 1;
        /*Loops thorugh the history list*/
        for (struct list_elem *e = list_begin(&history_list);
             e != list_end(&history_list);
             e = list_next(e))
        {
            struct history *hist = list_entry(e, struct history, elem);
            /*Print each history command*/
            printf("%d  %s\n", i, hist->command);
            i++;
        }
    }
    /*Compares then runs cd command*/
    else if (strcompare(*p, "cd") == 0)
    {
        char *argg[10];
        int i = 0;
        while (*p)
        {
            argg[i] = *p++;
            i++;
        }
        argg[i] = NULL;
         char cwd[1024];

        /*Checks for wrong argument format and prints message*/
        if(argg[1] == NULL || argg[2] != NULL ){
            printf("Wrong format : ");
            printf("cd requires exactly one argument\n");
        }/*Checks to see if the user entered -d as the argg*/
        else if (strcmp(argg[1], "-d") == 0){}
        /*Checks to see if the user entered ~ as the argg*/
        else if (strcmp(argg[1], "~") == 0)
        {
            chdir(homeDir);
        }/*Else changes directory to user entered dir*/
        else if (chdir(argg[1]) == -1) printf("Path not recognized.\n");
       
        /*gets and prints the current directory*/
        getcwd(cwd, sizeof(cwd));
        printf("Current Dir : %s\n",cwd );
    }

    /*Compares then runs exit command*/
    else if (strcompare(*p, "exit") == 0)
    {
        quit = true;
    }
}
/*Saves the given cmdline into the history list*/
void saveToHistory(char *cmdline)
{
    struct history *hist = malloc(sizeof(struct history));
    /*Copys the cmdline to the history struct*/
    hist->command = CpyStringOver(cmdline);
    /*Append the history struct to the history list*/
    list_push_back(&history_list, &hist->elem);
}

/*This function frees the history list*/
void history_list_free()
{
    /*Loop through the history list and deletes each item*/
    for (struct list_elem *e = list_begin(&history_list);
         e != list_end(&history_list);
         e = list_remove(e))
    {
    }
}

/*This function runs all of the commands present in the cmdline*/
void runCommand(struct ast_command_line *cmdline)
{
    /*Loop through each pipeline of commands.*/
    for (struct list_elem *e = list_begin(&cmdline->pipes);
         e != list_end(&cmdline->pipes);
         e = list_next(e))
    {
        /*Initialize pid*/
        pid_t pid = -1;
        /*J is used as a counter for the pipes*/
        int j = 0;

        /*Obtain the current pipe*/
        struct ast_pipeline *pipe1 = list_entry(e, struct ast_pipeline, elem);
        /*Obtain the commands in the pipe*/
        struct list_elem *e2 = list_begin(&pipe1->commands);
        /*Obtain the first command*/
        struct ast_command *cmd = list_entry(e2, struct ast_command, elem);

        /*Check if the command is internal*/
        bool isInternal = checkInternalCommand(cmd);

        /*If the command is internal run the internal command fucntion and break this loop*/
        if (isInternal)
        {
            runInternalCommand(cmd);
            break;
        }
        /*If the command is not internal continue*/

        /*Scince command pipe is not internal the pipe is added to the job list*/
        struct job *jb = add_job(pipe1);
        /*Obtain number of commands*/
        int numCommands = list_size(&pipe1->commands);
        /*Calculate number of pipes*/
        int numPipes = numCommands - 1;
        /*set the current command*/
        int currCommand = 0;

        /* Pipes Declarations Block*/
        int pipefds[2 * numPipes];
        for (int i = 0; i < numPipes; i++)
        {
            if (pipe(pipefds + i * 2) < 0)
            {
                perror("couldn't pipe");
                exit(EXIT_FAILURE);
            }
        }
        /****************************/

        /*File IO Block*/
        /*if output needs to be sent to a file*/
        if (jb->pipe->iored_output != NULL)
        {
            /*if stdout needs to be appended to he file*/
            if (jb->pipe->append_to_output)
            {
                freopen(jb->pipe->iored_output, "a", stdout);
            }
            /*If stdout needs to be written to a file not appended*/
            else
            {
                freopen(jb->pipe->iored_output, "w", stdout);
            }
            /*if stdout also needs to be appended to the file*/
            if (cmd->dup_stderr_to_stdout)
            {
                dup2(1, 2);
            }
        }
        /*If std in needs to be taken from a file*/
        if (jb->pipe->iored_input != NULL)
        {
            freopen(jb->pipe->iored_input, "r", stdin);
        }
        /*****************************/

        /*initialize processGroupID*/
        pid_t processGroupID = -1;

        /*Loop through the pipe to run each command as part of the pipeline*/
        for (struct list_elem *e2 = list_begin(&pipe1->commands);
             e2 != list_end(&pipe1->commands);
             e2 = list_next(e2))
        {
            /*Obtain the ast_command from the pipe*/
            struct ast_command *cmd = list_entry(e2, struct ast_command, elem);
            /*Block SigCHLD*/
            signal_block(17);
            /*Fork to create a parent and child process*/
            pid = fork();

            /*Code Block Which Both Parent and Children execute.*/
            /*extract command and its arguments*/
            char **p = cmd->argv;
            char *comd = *p;
            char *argg[10];
            int i = 0;
            while (*p)
            {
                argg[i] = *p++;
                i++;
            }
            argg[i] = NULL;
            /********************************************************/

            /*Child Code Block*/
            if (pid == 0)
            {
                /*create a new process group if this is the first command in the pipe */
                if (currCommand == 0)
                {
                    setpgid(0, 0);
                }
                /*else for the other commands in the pipe put them in the group of the first command*/
                else
                {
                    setpgid(0, jb->pgid);
                }
                /*Run the current command*/
                runChildProcess(currCommand, numCommands, numPipes, j, jb, pipefds, comd, argg);
            }
            /********************************************************/

            /*Error if not correctly forked*/
            else if (pid < 0)
            {
                perror("error");
                exit(EXIT_FAILURE);
            }

            /*Parent Code Block*/
            else
            {
                /*Sets the Process group id to the first spawned processes pid*/
                if (currCommand == 0)
                {
                    processGroupID = pid;
                    jb->pgid = processGroupID;
                }

                setpgid(pid, processGroupID);
                /*Fills in the pid array in jobs*/
                jb->pids[currCommand] = pid;
                /*increment counter*/
                j += 2;
                /*update the job*/
                jb->num_processes_alive = jb->num_processes_alive + 1;
                jb->totalProc = jb->totalProc + 1;
                /*increment counter*/
                currCommand++;
                /********************************************************/
            }
        }
        /*call function to close all open pipes*/
        closePipes(numPipes, pipefds);

        /*If the current job is not a Background job*/
        if (!jb->pipe->bg_job)
        {
            /*Give control of terminal to the running process group*/
            termstate_give_terminal_to(NULL, processGroupID);
            /*Wait for the job*/
            wait_for_job(jb);
            /*after waiting completed return back terminal controk to the shell*/
            termstate_give_terminal_back_to_shell();
        }
        /*FILE IO reset Block*/
        freopen("/dev/tty", "w", stdout);
        freopen("/dev/tty", "r", stdin);
        dup2(2, 2);
        /*********************/

        /*If the current job is a Background job*/
        if (jb->pipe->bg_job)
        {
            /*Update the job status and print job*/
            jb->status = BACKGROUND;
            printf("[%d] %d\n", jb->jid, pid);
        }
        /*Unblock SigChld*/
        signal_unblock(17);
    }
}

/*This function runs a specific child process*/
void runChildProcess(int currCommand, int numCommands, int numPipes, int j, struct job *jb, int pipefds[], char *comd, char **argg)
{

    /*If this is not the last command*/
    if (currCommand != numCommands - 1)
    {
        /*dup file descripters*/
        if (dup2(pipefds[j + 1], 1) < 0)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
    }

    /*dup file descripters*/
    if (j != 0)
    {
        if (dup2(pipefds[j - 2], 0) < 0)
        {
            perror(" dup2"); ///j-2 0 j+1 1
            exit(EXIT_FAILURE);
        }
    }
    /*call function to close all open pipes*/
    for (int i = 0; i < 2 * numPipes; i++)
    {
        close(pipefds[i]);
    }
    /*Execute the command after all pipes have been sorted*/
    if (execvp(comd, argg) < 0)
    {
        printf("no such file or directory");
        exit(EXIT_FAILURE);
    }
}

/*
Closes the pipe file descriptors by looping through numPipes times.
*/
void closePipes(int numPipes, int pipes[])
{
    for (int i = 0; i < 2 * numPipes; i++)
    {
        close(pipes[i]);
    }
}

/*This functions cleans up the job list by looping through the job list 
and removig any jobs which have been marked a finished*/
void cleanUpJobsList()
{
    /*Loop through the jobs list*/
    for (struct list_elem *e = list_begin(&job_list);
         e != list_end(&job_list);)
    {
        /*get pointer to job*/
        struct job *jb = list_entry(e, struct job, elem);

        /*If the job has been marked finished remove it*/
        if (jb->isFinished)
        {
            e = list_remove(e);
            delete_job(jb);
        }
        /*else iterate to the next job*/
        else
        {
            e = list_next(e);
        }
    }
}

int main(int ac, char *av[])
{
    int opt;
    quit = false;

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0)
    {
        switch (opt)
        {
        case 'h':
            usage(av[0]);
            break;
        }
    }
    
    /*Gets the current directory and saves it as the home*/
    getcwd(homeDir, sizeof(homeDir));

    /*Initialize Lists*/
    list_init(&job_list);
    list_init(&history_list);
    /*set the sigchld handler*/
    signal_set_handler(SIGCHLD, sigchld_handler);
    /*iniitialize terminal*/
    termstate_init();

    /* Loop until quit is switched to true */
    /*enter command "exit" to quit shell*/
    while (!quit)
    {
        /*Clean up jobs list by removing any finished jobs*/
        cleanUpJobsList();
        /* Do not output a prompt unless shell's stdin is a terminal */
        char *prompt = isatty(0) ? build_prompt() : NULL;
        char *cmdline = readline(prompt);
        free(prompt);

        if (cmdline == NULL) /* User typed EOF */
            break;

        struct ast_command_line *cline = ast_parse_command_line(cmdline);
        /*Save cline to history before it is freed.*/
        saveToHistory(cmdline);

        free(cmdline);
        if (cline == NULL) /* Error in command line */
            continue;

        if (list_empty(&cline->pipes))
        { /* User hit enter */
            ast_command_line_free(cline);
            continue;
        }
        else
        {
            runCommand(cline);
        }
    }
    /*This needs to be called before the shell exits.*/
    history_list_free();
    return 0;
}
