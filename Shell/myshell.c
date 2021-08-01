/* MY SHELL :) */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
// for wait
#include <sys/types.h>
#include <sys/wait.h>
// for open
#include <sys/stat.h>
#include <fcntl.h>

extern int errno ;  // to get info about the latest error
struct sigaction original_sigint_handler;  // initialized (with actual values) in prepare()


// helper functions
int str_index_in_arr(char** arr, int arr_len, char* str);
void pipe_process(char** arglist, int pipe_index);
void bury_zombies(int signum);


int prepare(void){
    /* initialization function
     * returns 0 on success, any other return value indicates an error.
     * */

    struct sigaction sa_bury_zombies, sa_ignore;

    /* ---------------------- define SIGCHLD handler in order to bury zombies right away ---------------------- */

    // define full sa_mask - delay handling of signals from sa_mask until sa_bury_zombies.sa_handler finishes,
    sigfillset(&sa_bury_zombies.sa_mask);

    // assign pointer to the handler function
    sa_bury_zombies.sa_handler = bury_zombies;
    // SA_NOCLDSTOP : get the SIGCHLD upon child death ONLY
    // SA_RESTART :  returning from the handler resumes the library function (such as open, read or write)
    sa_bury_zombies.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    // register the handler
    if( 0 != sigaction(SIGCHLD, &sa_bury_zombies, NULL) )
    {
        perror("signal handle registration failed");
        return -1;
    }

    /* ---------------------- define SIGINT handler (ignore signal) ---------------------- */

    // define empty sa_mask (doesn't matter in this case because we do nothing when SIGINT arrives)
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_handler = SIG_IGN;  // SIG_IGN specifies that the signal should be ignored
    // SA_RESTART :  returning from the handler resumes the library function (such as open, read or write)
    sa_ignore.sa_flags = SA_RESTART;
    // register the handler
    if( 0 != sigaction(SIGINT, &sa_ignore, &original_sigint_handler) )
    {
        perror("signal handle registration failed");
        return -1;
    }


    return 0;  // success!
}

int process_arglist(int count, char **arglist){
    /*
     * arglist - a list of char* arguments (words) provided by the user
     * it contains count+1 items, where the last item (arglist[count]) and *only* the last is NULL
     * RETURNS :
     * 1 if no error occurs
     * 0 otherwise
     * */

    int child_exit_code, child_pid, fd, pipe_index;
    int foreground_child = 1;  // boolean that indicates if father should wait() for child

    /* -------------- check background command case (ends with "&") -------------- */
    if (! strcmp(arglist[count - 1], "&")){
        arglist[count - 1] = NULL;
        count--;  // update arglist's length
        foreground_child = 0;
    }

    // create new process for executing the command
    child_pid = fork();

    if (child_pid < 0) {  // fork failed
        perror("fork failed");
        return 0;
    }

    else if (child_pid == 0) {  /* -------------- child -------------- */

        if (foreground_child){
            // reset process's SIGINT handler (foreground child processes should terminate upon SIGINT)
            if( 0 != sigaction(SIGINT, &original_sigint_handler, NULL) )
            {
                perror("signal handle registration failed");
                exit(1);
            }
        }

        /* -------------- check pipe case -------------- */
        pipe_index = str_index_in_arr(arglist, count, "|");
        if (pipe_index != -1){  // if pipe exists in arglist
            pipe_process(arglist, pipe_index);
        }

        else{  // not pipe case

            /* -------------- check output redirecting case -------------- */
            if (count > 1 && (! strcmp(arglist[count - 2], ">"))){  // if it is redirecting to file case
                // redirect output to the command's output file:

                // O_WRONLY = write permissions, O_CREAT = create file if it doesn't exist, O_TRUNC = delete previous content
                // 0777 = everyone can read, write, and execute
                fd = open( arglist[count - 1], O_WRONLY | O_CREAT | O_TRUNC, 0777 );
                if( fd == -1 ) {
                    perror("file open failed");
                    exit(1);
                }

                // create a copy of the file descriptor fd with file descriptor number =  STDOUT_FILENO
                //  file descriptor STDOUT_FILENO silently closed before being reused (from Linux manual page)
                if (dup2(fd, STDOUT_FILENO) == -1){
                    perror("dup2 failed");
                    exit(1);
                }
                if (close(fd) == -1){  // process will write to the file through STDOUT_FILENO fd so fd has no further use
                    perror("close failed");
                    exit(1);
                }

                // remove command's redirecting part
                arglist[count - 2] = NULL;
                count -= 2;
            }

            execvp(arglist[0], arglist);
            perror("execvp failed");  /* never gets here if execvp succeeded */
            exit(1);

            /* NOTE: in redirecting case: there is no need for closing fd because child process exits anyway so
             * the operating system will clean up for us, see: https://stackoverflow.com/questions/8175827/what-happens-if-i-dont-call-fclose-in-a-c-program */
        }

    }

    else { /* -------------- parent -------------- */
        if(foreground_child){
            if(waitpid(child_pid, &child_exit_code, 0 ) == -1){
                if(errno != ECHILD){
                    perror("waitpid failed");
                    exit(1);
                }
            }
        }
    }

    return 1;  // no errors occurred

}


int str_index_in_arr(char** arr, int arr_len, char* str){
    /* returns:
     * -1 if str not in arr
     * else, returns first index i s.t. arr[i] == str
     * */

    int i;
    for(i = 0; i < arr_len; ++i)
    {
        // if arr[i] == str
        if(!strcmp(arr[i], str))
            return i;
    }
    return -1;
}


void pipe_process(char** arglist, int pipe_index){
    /* execute pipe command specified by arglist
     * if error occurs, print error details and exit(1)
     * */
    int child_pid, pipefd[2];

    // create pipe
    if(pipe(pipefd) == -1)
    {
        perror( "pipe failed" );
        exit( 1 );
    }

    // child will run first command and pipe it's output to parent
    child_pid = fork();
    if (child_pid < 0) {  // fork failed
        perror("fork failed");
        exit(1);
    }

    else if (child_pid == 0) {  /* -------------- child (pipe's write end) -------------- */
        // close read side
        if (close(pipefd[0]) == -1){
            perror("close failed");
            exit(1);
        }

        // duplicates pipefd[1] but with fd num = STDOUT_FILENO, so now the process's output is directed to the pipe
        // file descriptor STDOUT_FILENO silently closed before being reused (from Linux manual page)
        if (dup2(pipefd[1], STDOUT_FILENO) == -1){
            perror("dup2 failed");
            exit(1);
        }

        // process will write to pipe through STDOUT_FILENO fd so pipefd[1] has no further use
        if (close(pipefd[1]) == -1){
            perror("close failed");
            exit(1);
        }

        arglist[pipe_index] = NULL;  // first command ends before the pipe

        execvp(arglist[0], arglist);

        perror("execvp failed");  /* never gets here if execvp succeeded */
        exit(1);

        // NOTE: STDOUT_FILENO closed by OS when this process finishes

    }

    else { /* -------------- parent (pipe's read end) -------------- */
        // close write side
        if (close(pipefd[1]) == -1){
            perror("close failed");
            exit(1);
        }

        // duplicates pipefd[0] but with fd num = STDIN_FILENO, so now the process's input is directed from the pipe
        // file descriptor STDIN_FILENO silently closed before being reused (from Linux manual page)
        if (dup2(pipefd[0], STDIN_FILENO) == -1){
            perror("dup2 failed");
            exit(1);
        }

        // process will read from pipe through STDIN_FILENO fd so pipefd[0] has no further use
        if (close(pipefd[0]) == -1){
            perror("close failed");
            exit(1);
        }

        arglist += pipe_index + 1;  // second command starts after the pipe
        execvp(arglist[0], arglist);
        perror("execvp failed");  /* never gets here if execvp succeeded */
        exit(1);

        // * NOTE: STDIN_FILENO closed by OS when this process finishes

        // * NOTE: pid 1 will bury the child when parent finishes,in this way,
        // both parent and child can run their commands simultaneously

    }

}



int finalize(void){
    /* cleanups function
    * returns 0 on success, any other return value indicates an error.
    * */
    return 0;
}


void bury_zombies(int signum){
    /* SIGCHLD handling function
     * This function receives the signal number as its only argument
     * (as required in https://manpages.ubuntu.com/manpages/bionic/man2/sigaction.2.html ) */

    int prev_errno = errno;  // save previous errno and will restore this before returning
    pid_t kidpid;
    int status;

    while ((kidpid = waitpid(-1, &status, WNOHANG)) > 0){
        /* from wait(2) - Linux man page:
         * -1 : wait for any child process
         * WNOHANG : return immediately if no child has exited */

        // fprintf(stderr, "child %d buried... RIP\n", kidpid);
    }
    if (kidpid == -1){  // might be an error
        if(errno != ECHILD){
            perror("waitpid failed");
            exit(1);
        }
    }
    errno = prev_errno;
}


