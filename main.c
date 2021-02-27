// Daniel Yu
// CS344 - Winter 2021
// Assignment 3 - smallsh -(Portfolio Project)

/*
* Sources / Citations:
* 
* (1) - Module 5 - Processes II - Exploration: Signal Handling API
* Link: https://oregonstate.instructure.com/courses/1798831/pages/exploration-signal-handling-api?module_item_id=20163882
* Description: creating signal handlers for SIGCHLD, SIGINT, and SIGTSTP was referenced from the Custom Handler for SIGINT example,
* specifically using sigaction to catch the signals and using a handler function to perform alternative behavior.
* 
* (2) - Assignment 1: Movies (Sample Program)
* Link: https://oregonstate.instructure.com/courses/1798831/assignments/8125992?module_item_id=20163859
* Description: creating a linked list was referenced from the Sample Program, seeing how to use a head node to initiate the list,
* and the use of a tail node to point to the (potentially) next node.
* 
* (3) - Tutorials Point - C library function - getenv()
* Link: https://www.tutorialspoint.com/c_standard_library/c_function_getenv.htm
* Description: how to use the getenv() method to search the environment by HOME to obtain the path value.
* 
* (4) - man7.org
* Link: https://man7.org/linux/man-pages/man2/chdir.2.html
* Description: how to use the chdir() method to change from the current directory to the specified path
* 
* (5) - Piazza (313) - Instructor Answer (TA - Felipe Orrico Scognamiglio)
* Link: https://oregonstate.instructure.com/courses/1798831/external_tools/165861
* Description: took Felipe's suggestion to store the child background process' pid in an array and upon user exit of smallsh,
* use the kill() method to terminate each process that remains.
* 
* (6) - Module 4 - Processes - Exploration: Process API - Executing a New Program
* Link: https://oregonstate.instructure.com/courses/1798831/pages/exploration-process-api-executing-a-new-program?module_item_id=20163875
* Description: from smallsh, the use of creation of a child process with fork() and use of execvp() to execute non-built-in commands
* were referenced from the execl to run ls -al example - specifically using the switch statement to check the child spawnPid from fork()
* (i.e. 0 or 1), exiting upon success or failure, and having the parent use waitpid() to wait for the child to terminate.
* 
* (7) - Module 5 - Processes II - Exploration: Processes and I/O
* Link: https://oregonstate.instructure.com/courses/1798831/pages/exploration-processes-and-i-slash-o?module_item_id=20163883
* Description: smallsh use of the dup2() method for input and output redirection was referenced from the Redirecting both Stdin and Stdout
* example - using open() to select the input or output file, specifying the access mode (i.e. RDONLY, WRONLY), and permissions. In addition,
* the need to respond with an error message in cause of open() and dup2() failure.
* 
* (8) - Module 4 - Processes - Exploration: Process API - Monitoring Child Processes
* Link: https://oregonstate.instructure.com/courses/1798831/pages/exploration-process-api-monitoring-child-processes?module_item_id=20163874
* Description: the usage of WIFEXITED(), WEXITSTATUS(), WIFSIGNALED(), and WTERMSIG() was referenced from the example code, where the 
* the child process status from waitpid() is used as an argument to these macros - and as a result allowing smallsh to know the
* termination status of the child process either by exit() or by a signal.
* 
* (9) - Module 5 - Processes - Exploration: Signals – Concepts and Types
* Link: https://oregonstate.instructure.com/courses/1798831/pages/exploration-signals-concepts-and-types?module_item_id=20163881
* Description: catching child termination with SIGCHLD by the parent process in smallsh was referenced from the SIGCHLD section
* in this Explorer - specifically using WNOHANG in waitpid to allow the parent process to continue working, and not needing to
* wait for the child to terminate.
* 
* (10) - Stack Overflow - from user "Some programmer dude" - valgrind errors
* Link: https://stackoverflow.com/questions/40626924/getting-syscall-param-execveargv-points-to-unaddressable-bytes-in-valgrind
* Description: execvp() gave “Syscall param execvp(argv) points to uninitialised byte(s)” error - the issue was the argument
* arrays needed to have a null terminated string at the end.
* 
* (11) - Stack Overflow - from user "PP" - gcc compilation error
* Link: https://stackoverflow.com/questions/41884685/implicit-declaration-of-function-wait/41884719
* Description: following the example code from Exploration: Process API - Executing a New Program gave compilation errors,
* the error was due to not including the following headers: <sys/types.h> and <sys/wait.h>.
*/

#include <stdio.h>                  // for perror
#include <stdlib.h>                 // for exit
#include <string.h>                 // for strings
#include <unistd.h>                 // for execvp
#include <errno.h>                  // for errors
#include <stdbool.h>                // for boolean data type
#include <fcntl.h>                  // for closing fd handles
#include <signal.h>                 // for signals
#include <sys/types.h>              // for wait / waitpid
#include <sys/wait.h>               // for wait / waitpid

// these global variables are used by signal handlers for SIGCHLD, SIGINT, 
// and SIGTSTP to communicate with the shell

int id_length;                      // track length of each process' pid
int childStatus;                    // track the status of child termination status
int curr_status = 0;                // track process' exit status
int signal_status = 0;              // track process' termination signal status
bool killed_bysignal;               // track if a process is terminated by a signal
bool foreground_mode = false;       // track whether shell is in fg or bg mode


// declare the functions that will be used in smallsh
void printStatus();
void updateStatus(int child_status);
bool checkAmpersand(char* currLine);
void handle_SIGTSTP(int signum);
void SIGCHLD_action(int signum);
void expand_input(char* currLine, char* to_parse);
void kill_processes(pid_t bg_IDs[], int bg_count);
struct command* parseCommand(char* currLine, char* to_parse);
void freeArguments(struct command* currCommand, char* currLine);
int exec_processes(struct command* user_command, int status, bool ampersand, pid_t bg_IDs[], int bg_count);
int compareCommands(struct command* user_command, int status, char* currLine, bool ampersand, struct command* head, pid_t bg_IDs[], int bg_count);

struct command
{
    char* arguments[512];           // contain arguments and input/output filename(s)
    char* args[512];                // contain only the command + arguments (no filenames)
    int stdout_position;            // arguments array's index of the output filename
    int stdin_position;             // arguments array's index of the input filename
    int total_arguments;            // total # of arguments + filenames in the user's input
    bool output_file;               // track whether an output filename for > redirection is present
    bool input_file;                // track whether an input filename for < redirection is present

    struct command* next;           // point to the user's next command (if exists)
};

/*
*  The main() function will continuously keep the shell
*  running while the user has not called exit. A prompt
*  is given after each successful execution of a command
*  to ask the user for a new command, where parseCommand()
*  and compareCommand() will be called for further processing.
*/
int main()
{
    char* currLine = NULL;                          // ptr to the buffer that will store the user's input 
    size_t len = 0;                                 // set len to 0 to allow getline to allocate memory for the buffer
    ssize_t nread;

    char to_parse[2048];                            // stores the user's input to parse if $$ is present
    pid_t bg_IDs[200];                              // store all the bg pids
    int bg_count = 0;                               // an accumulator to count the total # of bg pids
    bool ampersand = false;                         // track if ampersand (&) is in the user's input

    // Reference - see (1)
    // handling the reaping of zombies
    struct sigaction CHLD_action = { 0 };           // initiate sigaction to be empty
    CHLD_action.sa_handler = SIGCHLD_action;        // point to the signal handler function
    sigfillset(&CHLD_action.sa_mask);               // block other signals during handler events
    CHLD_action.sa_flags = SA_RESTART;              // restart getline if interrupted by the signal
    sigaction(SIGCHLD, &CHLD_action, NULL);         

    // signal to catch and ignoring SIGINT (i.e. Ctrl-C)
    struct sigaction SIGINT_action = { 0 };
    SIGINT_action.sa_handler = SIG_IGN;
    SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // signal to catch and ignoring SIGTSTP (i.e. Ctrl-Z)
    struct sigaction SIGTSTP_action = { 0 };
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // we want to create a linked list that consist of the user's commands
    // when the user exits the shell, the memory allocated for the command
    // structs will be free'd - Reference see (2)
    struct command* head = NULL;                    // initiate the head node of the linked list of commands
    struct command* tail = NULL;                    // initiate the tail node for this linked list

    // continuously run the shell until the user decides to exit
    while (1)
    {
        printf(": ");
        fflush(stdout);
        nread = getline(&currLine, &len, stdin);    // get the user's input; use getline to dynamically determine the input size
        ampersand = false;                          // reset ampersand for each user input

        // check if the user's input has &, 
        // when in fg mode, we don't bother to check, simply assume there is no &
        // then, all process will be in fg, and no need for it to be considered for bg
        if (!foreground_mode)       
        {
            ampersand = checkAmpersand(currLine);
        }

        // here we check if the user is entering an empty input (i.e. just pressing enter)
        // or a comment (i.e. starting with #) - if so, do nothing and give the prompt again
        if (strcmp(currLine, "\n") && currLine[0] != '#')
        {
            // remove the null terminator from the buffer in order to parse properly
            currLine[strlen(currLine) - 1] = 0;

            // parse the user's input using parseCommand()
            struct command* currCommand = parseCommand(currLine, to_parse);

            // after parsing, check if the head node is free, if so then this command is the first and last node (for now)
            if (head == NULL)
            {
                head = currCommand;
                tail = currCommand;
            }
            // otherwise, set this command as the latest node (i.e. the tail)
            else
            {
                tail->next = currCommand;
                tail = currCommand;
            }

            // after parsing, move on to check what kind of command this would be
            bg_count = compareCommands(currCommand, curr_status, currLine, ampersand, head, bg_IDs, bg_count);
        }
    }
    // free the memory allocated by getline
    free(currLine);
    return 0;
}

/*
*  The checkAmpersand function takes the user's command
*  and check the last character to see if it is '&' ampersand.
*  Returns true, if so, otherwise returns false.
*/
bool checkAmpersand(char* currLine)
{
    // we check the character before the null termination character to see if it has ampersand (&)
    char last_character = currLine[strlen(currLine) - 2];
    if ('&' == last_character) {    // when the second to last character is &, let the shell know it is true
        return true;
    }
    return false;                   // otherwise, let the shell know there is no & (i.e. false)
}

/*
*  The parseCommand and initiates a struct that will
*  hold the command, arguments, output/input filename, and
*  the total # of arguments.
*/
struct command* parseCommand(char* currLine, char* to_parse)
{
    // check for $$ variable, if present then processing will be done with the input expanded to smallsh's pid
    expand_input(currLine, to_parse);

    // allocate memory for the command struct
    struct command* currCommand = malloc(sizeof(struct command));
    // use for strtok_r
    char* savePtr;

    // 'total_commands' is an accumulator that will track the total # of commands in the user's input
    int total_commands = 0;
    // 'index' tracks the position of < and > characters in the arguments array
    int index = 0;
    // use tokens in the parsing of the user's input
    char* token = strtok_r(to_parse, " ", &savePtr);
    // the 'redirection' variable tracks whether < and > are detected during parsing
    bool redirection = false;

    // initialize output_file and input_file to NULL to avoid valgrind from complaining about using uninitialize values
    currCommand->output_file = NULL;
    currCommand->input_file = NULL;

    // continue to parse the token until reaching a null terminator string
    while (token != NULL)
    {
        // user input contains redirection for output
        if (!strcmp(token, ">"))
        {
            currCommand->stdout_position = index;               // save array position 
            currCommand->output_file = true;                    // toggle true for output redirection
            redirection = true;                                 
            token = strtok_r(NULL, " ", &savePtr);              // move to the next token
        }
        // user input contains redirection for input
        else if (!strcmp(token, "<"))
        {
            currCommand->stdin_position = index;                // save array position 
            currCommand->input_file = true;                     // toggle true for output redirection
            redirection = true;                             
            token = strtok_r(NULL, " ", &savePtr);              // move to the next token
        }
        else
        {
            // while no redirection is detected yet, all input are command arguments, so increment the count
            if (redirection == 0)                               
            {
                total_commands++;                       
            }
            // if the token is not > or <, add this to the arguments array
            currCommand->arguments[index] = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->arguments[index], token);
            token = strtok_r(NULL, " &", &savePtr);
            index++;
        }
    }

    currCommand->total_arguments = index;                       // save the total # of items in the arguments array
    currCommand->arguments[index] = '\0';                       // add null terminator to end the array

    // iterate through the arguments array, add all items except filenames to the args array
    for (int i = 0; i < total_commands; i++)
    {
        currCommand->args[i] = currCommand->arguments[i];
    }

    // add null terminating string after the final argument in the array
    // this allows execvp to know when to stop checking for arguments
    currCommand->args[total_commands] = '\0';

    return currCommand;
}

/*
*  the expand_input function takes the user's command
*  and checks for $$ that is back-to-back. If so, $$
*  variables in the command is substituted with the shell's
*  pid and creates a new string 'to_parse' for smallsh
*  to process.
*/
void expand_input(char* currLine, char* to_parse)
{
    // copy the original user input (string) to a buffer (to_parse)
    strcpy(to_parse, currLine);

    int index = 0;                          // track the index position for the original string
    int count = 0;                          // track the index position for the expanded string
    int total_symbols = 0;

    bool expand = false;                    // false if no $$ variable, otherwise toggled to true

    char new_str[2048];                     // new array to store the expanded string
    char smallsh_id[327680];                // new array to store pid of smallsh

    sprintf(smallsh_id, "%d", getpid());    // convert the smallsh pid from integer form to string

    // visit each character in the original string
    while (index != strlen(currLine))
    {
        // when reaching the first variable $, increment the count
        if (currLine[index] == '$' && total_symbols == 0)
        {
            total_symbols++;
        }
        // when the following character is also $, then we know the string needs to be expanded
        else if (currLine[index] == '$' && total_symbols == 1)
        {
            expand = true;
            total_symbols = 0;
        }
        // when the next character is not the $ variable, then reset the count and continue checking
        else if (currLine[index] != '$')
        {
            total_symbols = 0;
            expand = false;
        }

        // as each character in the original string is visted, also add it to the new string (except for the $ variable)
        if (currLine[index] != '$')
        {
            new_str[count] = currLine[index];
            count++;
        }

        // when two variables ($$) is reached, add the pid to the new string
        if (expand)
        {
            for (int i = 0; i < strlen(smallsh_id); i++)
            {
                new_str[count] = smallsh_id[i];
                count++;
            }
        }
        // increment the index to move to the next character in the original string
        index++;
        // copy the new string to the final buffer
        strcpy(to_parse, new_str);
        // end the final buffer with a null terminating string
        to_parse[count] = '\0';
    }
}

/*
*  The compareCommands function takes a command and checks for
*  built-in commands, specifically exit, cd, and status to be
*  executed by the shell itself. Any other commands are forwarded
*  to the exec_processes() function for child processes to execute.
*/
int compareCommands(struct command* user_command, int status, char* currLine, bool ampersand, struct command* head, pid_t bg_IDs[], int bg_count)
{
    struct command* currCommand = user_command;

    // exit - built-in command
    if (!strcmp(currCommand->arguments[0], "exit"))
    {
        // free the command, argument, and getline buffer
        free(currCommand->arguments[0]);
        free(currLine);
        free(user_command);

        // kill all bg processes that might still be occurring before exiting
        kill_processes(bg_IDs, bg_count);
        exit(0);
    }
    // status - built-in command
    else if (!strcmp(currCommand->arguments[0], "status"))
    {
        // call printStatus() to print either the exit status message or the termination status message
        printStatus(childStatus);
    }
    // cd  - built-in command
    else if (!strcmp(currCommand->arguments[0], "cd"))
    {
        int max_length = 4096;          // declare and initiate the max size for the path
        char path[max_length];          // declare a new array that will store this path

        // when there is no second argument, this implies that the user is only calling 'cd', which means go to HOME
        if (currCommand->arguments[1] == NULL)
        {
            // copy the HOME to the path array
            strcpy(path, getcwd(path, sizeof(path)));
            // use getenv() to search the environment for HOME - Reference - see (3) & (4)
            chdir(getenv("HOME"));
            return 0;
        }
        else
        {
            // count the number of words in the filename (i.e. New Folder is considered 2 words, while NewFolder is only 1)
            int counter = 1;
            while (currCommand->arguments[counter] != NULL)
            {
                counter++;
            }
            // add the first part of the filename
            strcpy(path, currCommand->arguments[1]);

            // when the filename's word count is > 1, we need to add spaces in the path string, otherwise something like 'New Folder'
            // will only be recognized as 'New'
            for (int i = 2; i < counter; i++)
            {
                strcat(path, " ");
                strcat(path, currCommand->arguments[i]);
            }
            // change the current directory to 'path' string
            chdir(path);
            return 0;
        }
    }
    // any thing other than the built-in commands, use execvp() to execute it under a child process
    else
    {
        // call exec_processes() to fork a child and have it execute this command
        bg_count = exec_processes(currCommand, status, ampersand, bg_IDs, bg_count);
    }
    // free the arguments after the process is complete
    freeArguments(currCommand, currLine);

    // return the updated background process count
    return bg_count;
}
/*
*  The kill_processes function takes bg_IDs and bg_count,
*  an array that contains all the bg processes, and their
*  total count. For each process, use SIGTERM to terminate
*  the process (before exiting the shell).
* 
*  Reference - see (5)
*/
void kill_processes(pid_t bg_IDs[], int bg_count)
{
    // iterate through each process in the bg array
    for (int i = 0; i < bg_count; i++)
    {
        // use kill() with the SIGTERM signal to terminate the process
        // we use SIGTERM (instead of SIGKILL) so the parent can catch this termination
        kill(bg_IDs[i], SIGTERM);
    }
}

/*
*  The exec_processes function takes the command and status
*  and fork() a child to execute non-built-in commands
*  (i.e. exit, cd, and status).
* 
*  Reference - see (6) & (7)
*/
int exec_processes(struct command* user_command, int status, bool ampersand, pid_t bg_IDs[], int bg_count)
{
    int childStatus;                // declare a place to store the child's termination status
    pid_t spawnPid = fork();        // use fork() to spawn a child process

    switch (spawnPid) {
    case -1:                        // for whatever reason if fork() fails to spawn a child,
        perror("fork()\n");         // then display the error and exit immediately
        exit(1);                    
        break;
    // Child Process
    case 0:
        // catch SIGTSTP signal for the child, and ignore this request
        // now the child process will now be able to continue
        signal(SIGTSTP, SIG_IGN);

        // Redirection for bg processes
        if (ampersand && !foreground_mode)
        {
            // when the user does not specify redirection for output
            if (!user_command->output_file)
            {
                // then redirect the output to /dev/null
                int output_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (output_fd == -1)
                {
                    printf("Background process failed to open() /dev/null for writing.\n");
                    fflush(stdout);
                    exit(1);
                }
                // use dup2() for changing stdout to /dev/null
                int output_results = dup2(output_fd, STDOUT_FILENO);
                if (output_results == -1)
                {
                    printf("Background process failed to redirect output using dup2().\n");
                    fflush(stdout);
                    exit(2);
                }
                // close the fd once dup2() has finished redirection
                close(output_fd);
            }
            // when the user does not specify the redirection for input
            if (!user_command->input_file)
            {
                // then redirect the input also to /dev/null
                int input_fd = open("/dev/null", O_RDONLY);
                if (input_fd == -1)
                {
                    printf("Background process failed to open() /dev/null for reading.\n");
                    fflush(stdout);
                    exit(1);
                }
                // again, use dup2() for redirecting input to /dev/null as well
                int input_results = dup2(input_fd, STDIN_FILENO);
                if (input_results == -1)
                {
                    printf("Background process failed to redirect input using dup2().\n");
                    fflush(stdout);
                    exit(2);
                }
                close(input_fd);
            }
        }
        // Foreground programs
        else
        {
            // all fg processes response to SIGINT will be reverted to default (SIG_DFL)
            // this allows the child to ignore the SIGINT (action) handler and still terminate
            signal(SIGINT, SIG_DFL);

            // when the user's input contains redirection for both input and output (< and >)
            if (user_command->output_file && user_command->input_file)
            {
                // open the input filename for redirection
                int fd = open(user_command->arguments[user_command->stdin_position], O_RDONLY);
                if (fd == -1)
                {
                    printf("open() failed on \"%s\"\n", user_command->arguments[user_command->stdin_position]);
                    fflush(stdout);
                    exit(1);
                }
                // redirect output to the input filename
                int input_result = dup2(fd, STDIN_FILENO);
                if (input_result == -1)
                {
                    printf("Foreground process failed to redirect input using dup2().\n");
                    fflush(stdout);
                    exit(2);
                }
                // open the output filename for redirection
                fd = open(user_command->arguments[user_command->stdout_position], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1)
                {
                    printf("open() failed on \"%s\"\n", user_command->arguments[user_command->stdin_position]);
                    fflush(stdout);
                    exit(1);
                }
                // redirect output to the output filename
                int output_results = dup2(fd, STDOUT_FILENO);
                if (output_results == -1)
                {
                    printf("Foreground process failed to redirect output using dup2().\n");
                    fflush(stdout);
                    exit(2);
                }
                close(fd);
            }
            // when the user only specifies output direction
            else if (user_command->output_file)
            {
                // open the output file for redirection
                int fd = open(user_command->arguments[user_command->stdout_position], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1)
                {
                    printf("Failed to open %s for output.\n", user_command->arguments[user_command->stdin_position]);
                    fflush(stdout);
                    exit(1);
                }
                // call dup2() to redirect output to the output file
                int output_results = dup2(fd, STDOUT_FILENO);
                if (output_results == -1)
                {
                    printf("Foreground process failed to redirect output using dup2().\n");
                    fflush(stdout);
                    exit(2);
                }
                close(fd);
            }
            // when the user only specifies input direction
            else if (user_command->input_file)
            {
                // open the input file for redirection
                int fd = open(user_command->arguments[user_command->stdin_position], O_RDONLY);
                if (fd == -1)
                {
                    printf("Failed to open %s for input.\n", user_command->arguments[user_command->stdin_position]);
                    fflush(stdout);
                    exit(1);
                }
                // call dup2() to redirect input to the input file
                int input_result = dup2(fd, STDIN_FILENO);
                if (input_result == -1)
                {
                    printf("Foreground process failed to redirect input using dup2().\n");
                    fflush(stdout);
                    exit(2);
                }
                close(fd);
            }
        }
        // once redirection is completed (if any), then have the child process use execvp() to
        // perform the command, and include any arguments to the command (if any)
        execvp(user_command->args[0], user_command->args);
        perror("execl");
        exit(errno);

        // Parent Process
    default:
        // when the command is specified by the user (i.e. have & at the end), 
        // we need to provide the bg pid to stdout
        if (ampersand)
        {
            printf("The background pid is: %d\n\n", spawnPid);
            fflush(stdout);

            // convert the child pid (i.e. spawnPid) from integer to string
            char str_length[6];
            sprintf(str_length, "%d", spawnPid);
            id_length = strlen(str_length);
            // the parent will be notified of the child's termination with waitpid(), but
            // with WNOHANG, the parent will not sleep, but rather return control back to smallsh
            spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);
            // for each bg process, add the pid to save in an array, and keep track of the total # of bg processes
            bg_IDs[bg_count] = spawnPid;
            bg_count++;
        }
        else
        {
            // when the process is fg (i.e. does not have &), then the parent must wait
            // for the child to finish its task - this is done by setting waitpid()'s third
            // argument to 0.
            spawnPid = waitpid(spawnPid, &childStatus, 0);

            // when the fg process is terminated by a signal (i.e. by ctrl-z), we need to let the user know 
            if (WIFSIGNALED(childStatus))
            {
                printf("terminated by signal %d\n", WTERMSIG(childStatus));
            }
        }
        break;
    }
    // reset the detection for > and < for the next process.
    user_command->output_file = false;
    user_command->input_file = false;

    // update the status of the child process
    updateStatus(childStatus);
    return bg_count;
}

/*
*  The freeArguments function takes a command and free
*  the dynamically allocated memory by malloc or calloc
*  for each argument in the command structs.
*/
void freeArguments(struct command* currCommand, char* currLine)
{
    // iterate through each argument in the current command's struct
    for (int i = 0; i < currCommand->total_arguments; i++)
    {
        // when the value is not NULL, free the argument
        if (currCommand->arguments[i] != NULL)
        {
            free(currCommand->arguments[i]);
        }
    }
}

/*
*  The updateStatus function takes the status of a child
*  process and update its exit status, signal status, and whether it is
*  killed by a signal.
* 
*  Reference - see (8)
*/
void updateStatus(int child_status)
{
    // check if the process terminated normally (i.e. by exit())
    if (WIFEXITED(child_status) != 0)
    {
        // set status to 0 - meaning the process exited without error
        if (WEXITSTATUS(child_status) == 0)
        {
            curr_status = 0;
        }
        // otherwise, set to 1 - meaning the process exited with an error
        else
        {
            curr_status = 1;
        }
        // allow the shell to know this process was not terminated by a signal
        killed_bysignal = false;
    }

    // when WIFSIGNALED is true (i.e. > 0), this means the process was terminated by a signal
    if (WIFSIGNALED(child_status) != 0)
    {
        // save the signal # that terminated this process
        signal_status = WTERMSIG(child_status);
        // let the shell know that the process was terminated by a signal
        killed_bysignal = true;
    }
}

/*
*  The printStatus function prints the exit status value
*  if a proccess is terminated normally, or prints the
*  signal value if it is terminated by a signal.
*/
void printStatus()
{
    // when killed_bysignal is false, this means it was terminated by exit()
    if (!killed_bysignal)
    {
        printf("exit value %d\n", curr_status);
        fflush(stdout);
    }
    // otherwise, if killed_bysignal is true, this means that it was terminated by a signal
    if (killed_bysignal)
    {
        // let the user know the signal that killed this process
        printf("terminated by signal %d\n", signal_status);
    }
}

/*
*  The SIGCHLD_action function is the signal handler
*  function for the SIGCHLD signal, printing to
*  stdout the bg pid when the process is done,
*  and either its exit status number or termination
*  by which signal.
* 
*  Reference - see (9)
*/
void SIGCHLD_action(int signum)
{
    pid_t child_id;
    // catch any child (-1) that may be terminated, and do not wait for it
    child_id = waitpid(-1, &childStatus, WNOHANG);

    if (child_id > 0)
    {
        // create the result message display in stdout
        int prompt_size = 3;                // size of the prompt
        char* prompt = ": ";                // the prompt string itself
        char message[25 + id_length];       // the fixed message length + varying pid length
        char number[id_length];             // array that will store the pid
        strcpy(message, "Background pid ");

        // NOTE: need to fix changing the id_counter
        sprintf(number, "%d", child_id);    // convert the child's pid from integer to string
        strcat(message, number);
        strcat(message, " is done: ");      // concatenate all the pieces

        updateStatus(childStatus);

        // display the created message and termination status to the user
        write(STDOUT_FILENO, message, 25 + id_length);
        printStatus(childStatus);

        // for processes terminated by a signal, the shell is missing a prompt message 
        // we manually write a new prompt to the user
        if (WIFSIGNALED(childStatus) == 0)
        {
            write(STDOUT_FILENO, prompt, prompt_size);
        }
    }
}

/*
*  the handle_SIGTSTP function is the signal handler
*  for the SIGTSTP signal, giving a message that the shell
*  is entering fg-only mode if not already activate, other
*  -wise, giving a message that the shell is exiting fg-only mode.
*/
void handle_SIGTSTP(int signum) {

    // check if ctrl-z is catched (i.e. toggled) for the first time
    if (!foreground_mode)
    {
        // turn on fg mode and display a message of its activation
        foreground_mode = true;
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
    }
    // check if ctrl-z is catched (i.e. toggled) for the second time
    else
    {
        // toggle fg mode back to off and display a message of its de-activation
        foreground_mode = false;
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
    }
    // display a prompt to the user again
    int prompt_size = 3;
    char* prompt = ": ";
    write(STDOUT_FILENO, prompt, prompt_size);
}
