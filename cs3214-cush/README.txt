Student Information
-------------------
Ahmad Rasool Malik 9060-78224

How to execute the shell
------------------------
One the shell is started it functions similar to a normal bash shell.
This shell consists of 8 built in commands which are:
1 jobs
2 fg  <job id>
3 bg  <job id>
4 stop  <job id>
5 kill  <job id>
6 history
8 TBD
7 exit

jobs can be run to display a list of runnign jobs.
fg <job id> can be run with a job id to bring a job into the foregound.
bg <job id> can be run with a job id to start a job running the background.
stop <job id> can be run with a job id to stop a running job in the background.  
kill <job id> can be run with a job id to kill a job in the background.
history can be run to display a list of commands you entered earlier in the shell.
cd <path> can be used to change the current directory.
exit can be run to quit the shell

Apart from running the built ins any external commands can be run with pipes and IO
enabled. 

Piping and and IO redirection works the same way as in the normal bash shell
For example:

"a | b"     : a's stdout gets piped to b's stdin.
"a | b | c" : a's stdout gets piped to b's stdin and b's stdout to c's stdin.
"a > b"     : stdout of a will be written to b. b will be written to from the start of the file.
"a >> b"     : stdout of a will be written to b. b will be appended to.
"a >& b"     : stdout and stderr of a will be written to b. 

Also any combination of pipes and IO redirection can be used.

Use of ^Z, ^C
^Z can be used to stop a running process in the foreground or stop the shell itself.
^C can be used to terminate a running process in the foreground or terminate the shell itself.

Important Notes
---------------
- jobs can some times take two trys to be updated correctly.
- cd -d can be used to see current directory

Description of Base Functionality
---------------------------------
Built In Commands:

jobs : Before any command line pipe is run it is added into the jobs list. When the jobs 
       command is run the shell iterates through the jobs list and prints each job present
       in its respective format. jobs that are terminated or end are removed from the jobs
       list via a clean up function which runs every command cycle. This clean up function 
       iterates through the jobs list and checks to see if each job has ended our not. If 
       it finds an ended job then the job is removed.

fg   : The fg <job id> command can be run to bring a job into the foreground. the job id 
       provided by the user is first used to find the job it refers to via a function. This 
       function return a pointer to the job the job id refers to. With this pointer the 
       status of the job is first updated. Then the original commands are printed to the 
       screen using a function. After which a SIGCONT signal is sent to the jobs process 
       group. this restarts the job if it was stopped in the background. The SIGCHLD is then
       blocked and the jobs process group is given authority over the terminal. The job is then
       waited for to be completed. After the job completes authority over the terminal is given
       back to the shell.

bg   : The bg <job id> command can be run to resume a job in the background. The job id 
       provided by the user is first used to find the job it refers to via a function. This 
       function return a pointer to the job the job id refers to. With this pointer the 
       status of the job is first updated. Then a SIGCONT signal is sent to the jobs process 
       group. this restarts the job in the background. the background job will be able to
       print to the console even while running in the backgroud. Once a job completes in the
       background a message "Done" with its job id is printed.

kill : The kill <job id> command can be run to kill a job in the background. The job id 
       provided by the user is first used to find the group id number the job id refers to
       via a function. Then a SIGKILL signal is sent to the jobs process 
       group. This kills all the processes in the process group which the job was a part of.

stop : The stop <job id> command can be run to stop a running job in the background. The job id 
       provided by the user is first used to find the group id number the job id refers to
       via a function. Then a SIGSTOP signal is sent to the jobs process 
       group. This stops all the processes in the process group which the job was a part of.

^C   : ^C was not implemented with any additional handlers, rather UNIX's base functionality
       to kill a process group in the foreground was taken advantage of.  

^Z   : ^Z was not implemented with any additional handlers, rather UNIX's base functionality
       to stop a process group in the foreground was taken advantage of. When ^Z is pressed 
       the job id and description is printed to the screen by catching the SIGCHLD signal and 
       checking the jobs status.
-------------------------------------
I/O, Pipes, Exclusive Access:

I/O:   File IO is carried out through if-else branches. Pipes are put into place for files that
       need to be written to or appeneded and if stdout or stderro or both need to be sent. The
       same happens for files that needs to be written from. After the files are read from or
       written to the pipes are closed.
 
Pipes: Pipes are implemented for each child process created. The number of pipes that need to be created is
       calculated by subtracting one from the number of commands. a file descriptor array of size
       numpipes * 2 is created and used. Appropriate pipes are put into place by checking if the current
       process is the last process, middle process or first process. The first processes stdin is
       not piped into by the piping function (it can be changed by the IO block). The last processses
       stdout is not piped(in can be redirected into a file by the IO block). The middle processes pipes
       are linked to the corresponding processes with the use of indexes.

Exclusive Access: Jobs which are in the foreground are given authority over ther terminal. When
       a job is stopped its access is taken away by the shell. Using the fg command gives access 
       of the process back to the terminal. If a command that initiales a program that needs 
       access to the terminal is started in the background or is shifted to the background
       then such a command is stopped.

List of Additional Builtins Implemented
---------------------------------------
<history>
<description>
The history command can be typed into the command prompt to display the list of commands 
preceeded by their indexes. Piplelines are displayes as a single command pipeline with one
index. The history command displays both built in and external commands executed.

<cd>
<description>
The cd <dir> command can be used to change the current working directory.
This command takes in one argument. This argument can be:
1 "The directory you want to go to"
2 "~" This will cd to the home directory which is the direcrtory cush is located in
3 "-d" This switch will print the current working directory.

If a wrong directory is entered the command will print an error.
If wrong number of arguments are entered the command will print an error.

cd always prints the current directory after it is executed.
