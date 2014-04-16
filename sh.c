#define _POSIX_SOURCE
#define _GNU_SOURCE

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "jobs.h"

#define BUFSIZE 1024 

//global variables
int errno;
int child_status;


const char *error1 = 
	"parser error: more than 1 '<' notation\n";
const char *error2 =
	"parser error: more than 1 '>' notation\n";
const char *error3 = 
	"parser error: no redirection argument specified\n";
const char *error4 = 
	"parser error: no argv[0] specified\n";
 
pid_t fg_pid = -1;
//for the foreground child process
//-1 if there is no fg child process
int fg_jid = -1;
char *fg_command;


int job_id_counter;
//a counter for background job id's

job_list_t *jlist;
//a list of jobs 
//used to store bg jobs


//install_handler:
//arguments:
//int sig represents a SIGNAL
//handler: a pointer to a specific handler
//return value: the return value of sigaction
int install_handler(int sig, void (*handler)(int)) {  
	struct sigaction act;
	//setting the sa_mask to empty
	//sa_mask specifies a mask of signals which
	//should be blocked during the execution of the signal handler
	sigemptyset(&(act.sa_mask));
	sigaddset(&(act.sa_mask), sig);
	act.sa_flags = SA_RESTART;//prevent the main process from stoping
	act.sa_handler = handler;//specifying the handler
	return sigaction(sig, &act, NULL);
	//notice this template
}


//error: wrongly goes to the fg_pid == -1 condition


/* sigint_handler
 * Respond to SIGINT signal (CTRL-C)
 * Argument: int sig - the integer code representing this signal
 */
void sigint_handler(int sig) {
	if (fg_pid == -1) {
		write(2, "sigint_handler: no foreground process running\n",
		strlen("sigint_handler: no foreground process running\n"));
		#ifndef NO_PROMPT
		write(1, "\n$", 2);
		#endif     
	} else {
		if (kill(-fg_pid, sig) == -1) {
		write(2, "sigint_handler failure:\n", strlen("sigint_handler failure:\n"));
		write(2, strerror(errno), strlen(strerror(errno)));
		#ifndef NO_PROMPT
		write(1, "\n$", 2);
		#endif
		}
	}
}


/* sigtstp_handler 
 * Respond to SIGTSTP signal (CTRL-Z)
 * Argument: int sig - the integer code representing this signal
 */
void sigtstp_handler(int sig) {
	if (fg_pid == -1) {
		write(2, "sigtstp_handler: no foreground process running\n",
		strlen("sigtstp_handler: no foreground process running\n"));
		#ifndef NO_PROMPT
		write(1, "\n$", 2);
		#endif
	} else {
		if (kill(-fg_pid, sig) == -1) {
		write(2, "sigtstp_handler failure:\n", strlen("sigtstp_handler failure:\n"));
		write(2, strerror(errno), strlen(strerror(errno)));
		#ifndef NO_PROMPT
		write(1, "\n$", 2);
		#endif
		}
	}
}


/* sigquit_handler
 * Catches SIGQUIT signal (CTRL-\)
 * Argument: int sig - the integer code representing this signal
 */
void sigquit_handler(int sig) {
	if (fg_pid == -1) {
		write(2, "sigquit_handler: no foreground process running\n",
		strlen("sigquit_handler: no foreground process running\n"));
		#ifndef NO_PROMPT
		write(1, "\n$", 2);
		#endif
	} else {
		if (kill(-fg_pid, sig) == -1) {
		write(2, "sigtquit_handler failure:\n", strlen("sigtquit_handler failure:\n"));
		write(2, strerror(errno), strlen(strerror(errno)));
		#ifndef NO_PROMPT
		write(1, "\n$", 2);
		#endif
		}
	}
}


//sigchld_handler
//argument: int sig -- the integer code representing a signal

void sigchld_handler(int sig) {
	sig = 0;
 
	int status;
	sig = 1;
	int fg_return_value;
	pid_t current_pid = -1;
	//I can use the waitpid(-1, &status, *) method
	//but it requires to much changing of identation
	//so I leave the original method here
	
	//fg and bg requires some specific handling
	//purely for clarification purpose
	//i included two blocks in this method
	while ((current_pid = get_next_pid(jlist)) != -1) {
		if (current_pid == fg_pid) continue;
		pid_t result = waitpid(current_pid, &status, WUNTRACED | WNOHANG | WCONTINUED);
		if (result == current_pid) {//meaning there is a state change      
			#ifndef NO_PROMPT
			write(1, "SIGCHLD received(bg)\n", strlen("SIGCHLD received(bg)\n"));    
			#endif 
			if (!(WIFEXITED(status))) {//not normally terminated
				if (WIFSIGNALED(status)) {
					
					int mysig = WTERMSIG(status);
					int temp_jid = get_job_jid(jlist, result);
					
					if (remove_job_pid(jlist, current_pid) != 0) {
						perror("sigchld_handler: remove_job_pid failed\n");
					} else {	
			
						printf("Job [<%d>] (<%d>) terminated by signal <%d>\n",
							temp_jid,
							result,
							mysig);	
						
						#ifndef NO_PROMPT
						write(1, "\n$", 2);
						#endif
					}
	  
				} else if (WIFSTOPPED(status)) {
					if (update_job_pid(jlist, current_pid, _STATE_STOPPED) != 0) {
						perror("sigchld_handler: update_job_pid failed\n");
					} else {
					  #ifndef NO_PROMPT
					  write(1, "\n$", 2);
					  #endif
					}
				} else if (WIFCONTINUED(status)) {
					if (update_job_pid(jlist, current_pid, _STATE_RUNNING) != 0) {
						 perror("sigchld_handler: update_job_pid failed\n");
					} 
				}
			} else {
			  //normally terminated
				if (remove_job_pid(jlist, current_pid) != 0) {
					perror("sigchld_handler: remove_job_pid failed\n");
				} else {
					#ifndef NO_PROMPT
					write(1, "\n$", 2);
					#endif
				}
			}
		}  
    
	} 
  
	//dealing with terminated/stopped foreground
	//jobs
	if (fg_pid != -1) {
		fg_return_value = waitpid(fg_pid, &status, WUNTRACED | WNOHANG | WCONTINUED);
		if (fg_return_value == fg_pid) {//meaning that this fg_pid has changed state
			#ifndef NO_PROMPT
			write(1, "SIGCHLD received(fg)\n", strlen("SIGCHLD received(fg)\n"));    
			#endif   
			if (!(WIFEXITED(status))) {
			//if not normally terminated
				if (WIFSIGNALED(status)) {
				//if terminated by a signal
						
					if (remove_job_pid(jlist, fg_pid) != 0) {
						perror("sigchld_handler: remove_job_pid failed");
					} else {
						
						int mysig = WTERMSIG(status);
						printf("Job [<%d>] (<%d>) terminated by signal <%d>\n",
							fg_jid,
							fg_pid,
							mysig);
					}
					fg_pid = -1;
					fg_jid = -1;
				} else if (WIFSTOPPED(status)) {
				//if stopped by a signal
					if (update_job_pid(jlist, fg_pid, _STATE_STOPPED) != 0) {
						perror("sigchld_handler: update_job failed\n");
					} 
					fg_pid = -1;
					fg_jid = -1;
					
				} else if (WIFCONTINUED(status)) {
					if (update_job_pid(jlist, fg_pid, _STATE_RUNNING) == -1) {
						perror("sigchld_handler error: update_job_pid failed");
						return;
					}									  
				}
				 
			} else {
			//terminate on its won
				if (remove_job_pid(jlist, fg_pid) != 0) {
					perror("sigchld_handler: remove_job_pid failed");
				} 
				fg_pid = -1;
				fg_jid = -1;
			}
		}
	}
	return;  
}





//install()
//called in main
//purpose is to install the three handlers
//while masking all the signals
void install() {  
	sigset_t old;
	sigset_t full;
	sigfillset(&full);
	// Ignore signals while installing handlers
	sigprocmask(SIG_SETMASK, &full, &old);
	//Install signal handlers
	if(install_handler(SIGINT, &sigint_handler))
		perror("Warning: could not install handler for SIGINT");
	if(install_handler(SIGTSTP, &sigtstp_handler))
		perror("Warning: could not install handler for SIGTSTP");
	if(install_handler(SIGQUIT, &sigquit_handler))
		perror("Warning: could not install handler for SIGQUIT");
	if(install_handler(SIGCHLD, &sigchld_handler))
		perror("Warning: could not install handler for SIGCHLD");
	// Restore signal mask to previous value
	sigprocmask(SIG_SETMASK, &old, NULL); 
}


//parse_args parses valid argument 
//in a user-input string
//and saves them in *myargv[]
//an array of char pointers
//it returns an int representing the total number of args
int  parse_args (char *myargv[], char str[BUFSIZE]) {
	//myargv is the argv to be changed by this procedure
	//str is the string of the user input
	char *head = str;
	int counter = 0;
	int arg_counter = 0;
 
	while (counter < BUFSIZE) {
		int arg_length;
		while ((*head == '\0') || (*head == ' ')) {
			head++;
			counter++;
		}
    
		if (counter < BUFSIZE) {
			myargv[arg_counter] = head;
			arg_length = (signed) strcspn(head, " \0");
			head = head+arg_length;
			arg_counter++;
			counter = counter+arg_length;
		}
	}
  
	for (int i = 0; i < 1024; i++) {
		if (str[i] == ' ') {
			str[i] = 0;
		}
	}
	return arg_counter;
}

//implementiation of 
//resuming jobs in bg
//input value: argvars
void resume_in_bg(char *argvars[]) {
  	if (argvars[1] == NULL) {
			perror("execute_args error: no argument specified for bg\n");
			return;
	} else {
		pid_t my_pid;
		int int_pid;
		int my_jid;

		if (sscanf(argvars[1], "%%%d", &my_jid) > 0) {
			my_pid = get_job_pid(jlist, my_jid);
			if (my_pid == -1) {
			    write(2, "execute_args error: invalide jid", strlen("execute_args error: invalide jid"));
			    return;
			}
		} else if (sscanf(argvars[1], "%d", &int_pid) > 0) {
			my_pid = (pid_t) int_pid;
			if (my_jid == -1) {
				perror("execute_args error: invalid pid");
				return;
			}
		} else {
			perror("execute_args error: argument not in correct format\n");
			return;
		}
		if (kill(my_pid, SIGCONT) != 0) {
			perror(strerror(errno));
			return;
		}
	}
}


//implementiation of 
//resuming jobs in fg
//input value: argvars
void resume_in_fg(char *argvars[]) {
	if (argvars[1] == NULL) {
		perror("execute_args error: no argument specified for fg\n");
		return;
	} else {
		pid_t my_pid;
		int int_pid;
		int my_jid;
		sigset_t old;
		sigset_t more;//more inludes the SIGCHLD

		if (sscanf(argvars[1], "%%%d", &my_jid) > 0) {
			my_pid = get_job_pid(jlist, my_jid);
			if (my_pid == -1) {
			    write(2, "execute_args error: invalide jid", strlen("execute_args error: invalide jid"));
			    return;
			}
		} else if (sscanf(argvars[1], "%d", &int_pid) > 0) {
			my_pid = (pid_t) int_pid;
			my_jid = get_job_jid(jlist, my_pid);
			if (my_jid == -1) {
				perror("execute_args error: invalid pid");
				return;
			}
		} else {
			perror("execute_args error: argument not in correct format\n");
			return;
		}
		sigemptyset(&more);
		sigaddset(&more, SIGCHLD);
		sigprocmask(SIG_BLOCK, &more, &old);//adding mask of SIGCHLD   
		if (kill(my_pid, SIGCONT) != 0) {
			perror(strerror(errno));
			return;
		} 
		fg_pid = my_pid;
		fg_jid = my_jid;
		sigprocmask(SIG_SETMASK, &old, NULL);//removing mask
	}
     
	while (fg_pid != -1) {//while waitpid does not return	
		pause();	  
	}
}

//run_in_bg
//runs the arguments in background
//input:
//argvars: the parsed argument variables
//actual_input: start with 0 if there is none
//actual_append: start with 0 if there is none 
//actual_output: start with 0 if there is none
void run_in_bg(char *argvars[], char *actual_input, char *actual_append, char *actual_output) {
	pid_t bg_pid;    
	sigset_t old;
	sigset_t more;//more inludes the SIGCHLD
	sigemptyset(&more);
	sigaddset(&more, SIGCHLD);
	sigprocmask(SIG_BLOCK, &more, &old);//adding mask of SIGCHLD   
      
	
	if (!(bg_pid = fork())) {
	//now in the child process
	//setting group id
		sigprocmask(SIG_SETMASK, &old, NULL);//removing mask
		setpgid(getpid(), getpid());
				
		if (actual_input[0] != 0) {//there is need for input redirection
			close(0);
			if (open(actual_input, O_RDONLY) == -1) {
				perror(actual_input);
				return;
			}	
		}	
		if (actual_append[0] != 0) {//there is need for append redirection
			close(1);
			if (open(actual_append, O_CREAT| O_APPEND | O_WRONLY, 
				S_IRUSR |S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP |S_IWOTH | S_IXUSR) == -1) {
				perror(actual_append);
				return;
			}
		}	
		if (actual_output[0] != 0) {//there is need for output redirection
			close(1);
			if (open(actual_output, O_CREAT | O_TRUNC| O_WRONLY, 
				S_IRUSR | S_IRGRP | S_IROTH| S_IWUSR | S_IWGRP | S_IWOTH | S_IXUSR) == -1) {
				perror(actual_output);
				return;
			}
		}  
	  
		if (execv(argvars[0], argvars) == -1) {
			if (errno == ENOENT) {
				if (write(STDERR_FILENO, "sh: command not found: ",
					strlen("sh: command not found: ")) < 0 ||
					write(STDERR_FILENO, argvars[0], strlen(argvars[0])) < 0 ||
					write(STDERR_FILENO, "\n", 1) < 0) {
						
					perror("sh: execute_args: write error\n");
				}
				exit(1);
			} else {
				if (write(STDERR_FILENO, "sh: execution of ",
					strlen("sh: execution of ")) < 0 ||
					write(STDERR_FILENO, argvars[0], strlen(argvars[0])) < 0 ||
					write(STDERR_FILENO, " failed: ", strlen(" failed: ")) < 0 ||
					write(STDERR_FILENO, strerror(errno),
					strlen(strerror(errno))) < 0 ||
					write(STDERR_FILENO, ".\n", 2) < 0) {
						  
					perror("sh: execute_args: write error\n");
				}  
				exit(1);
			}   
		}			 
	}
			
	//adding to the bg job list
	add_job(jlist, job_id_counter, bg_pid, _STATE_RUNNING, argvars[0]);	    
	job_id_counter++;
	sigprocmask(SIG_SETMASK, &old, NULL);//removing mask
	//implementing bg jobs above  
}


//run_in_fg
//runs the arguments in foreground
//input:
//argvars: the parsed argument variables
//actual_input: start with 0 if there is none
//actual_append: start with 0 if there is none 
//actual_output: start with 0 if there is none
void run_in_fg(char *argvars[], char *actual_input, char *actual_append, char *actual_output) {
		
	sigset_t old;
	sigset_t more;
	
	fg_jid = job_id_counter;
	fg_command = argvars[0];
	job_id_counter++;
	
	//
	sigaddset(&more, SIGCHLD);
	sigprocmask(SIG_SETMASK, &more, &old);//adding mask of SIGCHLD   
	
	if (!(fg_pid = fork())) {	
	/*now in the child process*/
	//setting group id
			
		sigprocmask(SIG_SETMASK, &old, NULL);//removing mask
		setpgid(getpid(), getpid());
				
		if (actual_input[0] != 0) {//there is need for input redirection
			close(0);
			if (open(actual_input, O_RDONLY) == -1) {
				perror(actual_input);
				return;
			}	
		}	
		if (actual_append[0] != 0) {//there is need for append redirection
			close(1);
			if (open(actual_append, O_CREAT| O_APPEND | O_WRONLY, 
				S_IRUSR |S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP |S_IWOTH | S_IXUSR) == -1) {
				perror(actual_append);
				return;
			}
		}	
		if (actual_output[0] != 0) {//there is need for output redirection
			close(1);
			if (open(actual_output, O_CREAT | O_TRUNC| O_WRONLY, 
				S_IRUSR | S_IRGRP | S_IROTH| S_IWUSR | S_IWGRP | S_IWOTH | S_IXUSR) == -1) {
				perror(actual_output);
				return;
			}
		}
	
	
		if (execv(argvars[0], argvars) == -1) {
			if (errno == ENOENT) {
				if (write(STDERR_FILENO, "sh: command not found: ",
					strlen("sh: command not found: ")) < 0 ||
					write(STDERR_FILENO, argvars[0], strlen(argvars[0])) < 0 ||
					write(STDERR_FILENO, "\n", 1) < 0) {
					perror("sh: execute_args: write error\n");
				}
				exit(1);
			} else {
				if (write(STDERR_FILENO, "sh: execution of ",
					strlen("sh: execution of ")) < 0 ||
					write(STDERR_FILENO, argvars[0], strlen(argvars[0])) < 0 ||
					write(STDERR_FILENO, " failed: ", strlen(" failed: ")) < 0 ||
					write(STDERR_FILENO, strerror(errno),
					strlen(strerror(errno))) < 0 ||
					write(STDERR_FILENO, ".\n", 2) < 0) {
						  
					perror("sh: execute_args: write error\n");
				} 
				exit(1);
			}   
		} 
		
	}        
    
	if (add_job(jlist, fg_jid, fg_pid, _STATE_RUNNING, argvars[0]) != 0) {
		perror("add_job error\n");
	}
	sigprocmask(SIG_SETMASK, &old, NULL);//removing mask
	while (fg_pid != -1) {//while waitpid does not return	
		pause();	  
	}
}

//executing the parsed commands
//input:
//argvars: the parsed argument variables
//actual_input: start with 0 if there is none
//actual_append: start with 0 if there is none 
//actual_output: start with 0 if there is none
void execute_args(int argcounter, char *argvars[], char *actual_input, char *actual_append, char *actual_output) {
	if (strcmp(argvars[0], "ln") == 0) {
		if (link(argvars[1], argvars[2]) == -1) {
			write(2, strerror(errno), strlen(strerror(errno)));
		}
	} else if (strcmp(argvars[0], "cd") == 0) {
		if (chdir(argvars[1]) == -1) {
			write(2, strerror(errno), strlen(strerror(errno)));
		}
	} else if (strcmp(argvars[0], "rm") == 0) {
		if (unlink(argvars[1]) == -1) {
			write(2, strerror(errno), strlen(strerror(errno)));
		}   
	} else if (strcmp(argvars[0], "jobs") == 0) {
		jobs(jlist);      
	} else if (strcmp(argvars[0], "bg") == 0) {      
		resume_in_bg(argvars);      
	} else if (strcmp(argvars[0], "fg") == 0) {   
		resume_in_fg(argvars);
	} else {  
		if (strcmp(argvars[argcounter-1], "&") == 0) {
			//implementing bg jobs
			argvars[argcounter - 1] = NULL;
			run_in_bg(argvars, actual_input, actual_append, actual_output);
			
		} else {
			run_in_fg(argvars, actual_input, actual_append, actual_output);
		}
	}   
}


int main() {
	char buf[BUFSIZE];
	int n;  
  
	job_id_counter = 1;
	jlist = init_job_list();
	install();
  
	#ifndef NO_PROMPT
	write(1, "\n$", 2);
	#endif
	memset(buf, 0, sizeof(buf));
	while (((n = read(0, buf, sizeof(buf)))) > 0) {   
		char *argvars[10];
		//an array of char pointers storing argument variables
		//we set the length of this char pointer array to be 10 
    
		//removing new lines
		for (int i = 0; i < BUFSIZE; i++) {
			if (buf[i] == '\n') {
			buf[i] = 0;
			}
		}
    
		//removing tabs
		for (int i = 0; i < BUFSIZE; i++) {
			if (buf[i] == '\t') {
			buf[i] = ' ';
			}
		}
    
		char *ptr_to_input = NULL; //char pointer pointing to < notation
		char *ptr_to_output = NULL;//char pointer pointing to > notatoin
		char *ptr_to_append = NULL;
    
		char *ptr_to_input_arg;    
		size_t input_length; //length of the input after <
		char actual_input[256];//keeping the value of input
    
		char *ptr_to_append_arg;
		size_t append_length;//length of the argument after >>
		char actual_append[256];//keeping the value of the argument after
 
    
		char *ptr_to_output_arg;
		size_t output_length; //length of the output after >
		char actual_output[256];//keeping the value of output
   
    
		int arg_counter;
    
		memset(actual_input, 0, 256);
		memset(actual_output, 0, 256);
		memset(actual_append, 0, 256);
   
		//checking for multiple < or >
		if (strchr(buf, '<') != strrchr(buf, '<')) {
			write(2, error1, strlen(error1));
			#ifndef NO_PROMPT
			write(1, "\n$", 2);
			#endif
			memset(buf, 0, sizeof(buf));
			continue;
		 } else {
			ptr_to_input = strchr(buf, '<');
			if (strrchr(buf, '>') - strchr(buf, '>') == 1) {
				ptr_to_append = strchr(buf, '>');//need for append
			} else {
				if (strchr(buf, '>') != strrchr(buf, '>')) {
					write(2, error2, strlen(error2));
					#ifndef NO_PROMPT
					write(1, "\n$", 2);
					#endif
					memset(buf, 0, sizeof(buf));
					continue;
				} else {
					ptr_to_output = strchr(buf, '>');
				}
			}
		}
    
		//if there is need for input redirection
		if (ptr_to_input != NULL) {
			char *current = ptr_to_input;
			while ((*current == ' ') || (*current == '<')) {
				current++;
			} 
			input_length = strcspn(current, " >\0");      
			if ((*current == '\0') || (*current == '>')) {
				write(2, error3, strlen(error3));
				#ifndef NO_PROMPT
				write(1, "\n$", 2);
				#endif
				memset(buf, 0, sizeof(buf));
				continue;
			} else {
				ptr_to_input_arg = current;
				for (int k = 0; k < (signed) input_length; k++) {
					actual_input[k] = current[k];
				}
			//storing the output_arg to actual_output
			}
			//erasing the slots representing redrections
			*ptr_to_input = 0;
			for (int i = 0; i < (signed) input_length; i++) {
				ptr_to_input_arg[i] = 0;
			}      
		}
	    
	    //if there is need for append redirection:
	    //exclusive with output redirection    
	    if (ptr_to_append != NULL) {
			char *current = ptr_to_append;
			while ((*current == ' ') || (*current == '>')) {
				current++;
			}      
			append_length = strcspn(current, " <\0");      
			if ((*current == 0) || (*current == '<')) {
			write(2, error3, strlen(error3));
			#ifndef NO_PROMPT
			write(1, "\n$", 2);
			#endif
			memset(buf, 0, sizeof(buf));
			continue;
			} else {
				ptr_to_append_arg = current;
				for (int k = 0; k < (signed) append_length; k++) {
					actual_append[k] = current[k];
				}
			}
			*ptr_to_append = 0;
			ptr_to_append[1] = 0;
			for (int i = 0; i < (signed) append_length; i++) {
				ptr_to_append_arg[i] = 0;
			}      
		}
		//if there is need for output redirection
		if (ptr_to_output != NULL) {
			char *current = ptr_to_output;
			while ((*current == ' ') || (*current == '>')) {
			current++;
			}
	      
			output_length = strcspn(current, " <\0");
			if ((*current == 0) || (*current == '<')) {
				write(2, error3, strlen(error3));
				#ifndef NO_PROMPT
				write(1, "\n$", 2);
				#endif
				memset(buf, 0, sizeof(buf));
				continue;
			} else {
				ptr_to_output_arg = current;
				for (int k = 0; k < (signed) output_length; k++) {
					actual_output[k] = current[k];
				}
				//storing the output_arg to actual_output
			}         
			//erasing the slots representing redirections
			*ptr_to_output = 0;
			for (int i = 0; i < (signed) output_length; i++) {
				ptr_to_output_arg[i] = 0;
			}      
		}  
		  
		arg_counter = parse_args(argvars, buf);
		argvars[arg_counter] = NULL;
		
		if ((argvars[0] == 0) && (ptr_to_output == NULL) && (ptr_to_append == NULL) && (ptr_to_input == NULL)) {
			#ifndef NO_PROMPT
			write(1, "\n$", 2);
			#endif
			memset(buf, 0, sizeof(buf));
			continue;
		}
		
		if (argvars[0]== 0) {
			write(2, error4, strlen(error4));
			#ifndef NO_PROMPT
			write(1, "\n$", 2);
			#endif
			memset(buf, 0, sizeof(buf));
			continue;
		}
	    
		//exit case:
		if (strcmp(argvars[0], "exit") == 0) {   
			cleanup_job_list(jlist);
			return 0;
		}
	    
		
		
		//executing the command
		execute_args(arg_counter, argvars, actual_input, actual_append, actual_output);
		#ifndef NO_PROMPT
		write(1, "\n$", 2);    
		#endif
		memset(buf, 0, sizeof(buf));
	}  
		
	cleanup_job_list(jlist);
	return 0;
}