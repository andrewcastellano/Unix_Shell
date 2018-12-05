#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>

void changeDir();
void status(int);
void parseInput(char[]);
char* cmd[512]; //array of 512 char*s to satisfy (maximum of 512 arguments) requirement
int bgprocesses[128]; //holds background processes
void execCommand(pid_t,int*,char* [],bool,bool,bool);
bool _background, _foregroundOnly, _input, _output = false;
char inputFile[128];
char outputFile[128];
int fileDescriptor;
int numBgProcesses=0;
int smallshpid;
void catchSIGINT(int);
void catchSIGTSTP(int);
struct sigaction sa = {0};
struct sigaction si = {0};
struct sigaction SIGTSTP_action = {0};
int childExitStatus = -5;

int main(){
	smallshpid=getpid();
	
	sa.sa_handler = SIG_DFL;//utilize default action
	sigfillset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	
	SIGTSTP_action.sa_handler = catchSIGTSTP;//using custom catchSIGTSTP function 
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	
	si.sa_handler = SIG_IGN;//shell ignores ^C

	sigaction(SIGINT,&si,NULL);//set shell to ignore ^C always
	sigaction(SIGTSTP,&SIGTSTP_action,NULL);
	
	char input[2048];
	bool exitSmallsh = false;
	pid_t spawnPid = -5;
	int i;
	for (i = 0; i < 128; ++i)
		bgprocesses[i] = -1;
	//Main loop for Smallsh, continue until exitSmallsh boolean is true
	do {
		int i;
		for(i = 0; i < 512; ++i)
			cmd[i] = NULL;
		memset(input,'\0',sizeof(input));
		printf(": ");
		fflush(stdout);
		fgets(input, 2048, stdin);//2048 characters maximum per input
		input[strlen(input)-1] = 0; // place null terminator at length of string-1 to remove trailing \n
		
		//if the user just hits enter
		if (strlen(input) == 0){
			continue;
		}

		parseInput(input);
 
		if(cmd[0][0] == '#'){
		//do nothing as it is a comment
			continue;
		}else if (strcmp(cmd[0],"exit") == 0){
			//if the user wants to exit
			exitSmallsh=true;
		}else if(strcmp(cmd[0],"cd") == 0){
			//if the user wants to change directory
			changeDir();
		}else if(strcmp(cmd[0],"status") == 0){
			//if the user wants the status
			status(childExitStatus);
		}else{
			//execute passed in command
			if(_foregroundOnly && _background){
				_background=false;//reset background
			}
			execCommand(spawnPid,&childExitStatus,cmd,_background,_input,_output);
		}
	}
	while(!exitSmallsh);
}

/*****************************************************************************
 *Function to parse input into separate commands
 *Input: Char[] input from user
 *Output: Changes global variable char* cmd[] to hold 1 command per pointer
*****************************************************************************/
void parseInput(char input[]){
	const char d[2] = " ";
	char* token;
	char buffer[256];
	memset(buffer,'\0',(sizeof(buffer)));
	int i,j = 0;
	bool pidflag=false;
	
	token = strtok(input,d);
	while(token != NULL){
		if (!strcmp(token,"<")){//signaling that we are using an input file
			_input=true;
			token = strtok(NULL,d);//get the name of the input file
			strcpy(inputFile,token);//hold name in global array variable
			i++;
			continue;//do not add this to our command array
		}else if (!strcmp(token,">")){//output file
			_output=true;
			token = strtok(NULL,d); //get the name of the output file
			strcpy(outputFile,token);//hold name in global array variable
			i++;
			continue;
		}else if (!strcmp(token,"&")){//background process
			_background=true;
			i++;
			token = strtok(NULL,d);
			continue;
		
		}
		//if there are 2 $'s next to each other
		//expand to pid using getpid()
		j=0;
		while(token[j] != '\0'){
			if(token[j] == '$'){
				if(token[j+1] == '$'){
					pidflag=true;
					token[j] = '\0';
					token[j+1] = '\0';
					sprintf(buffer,"%s%d",token,getpid());
				}
			}
	j++;
}

		//need to allocate space the size of buffer variable if we are expanding $$ to pid
		if(pidflag){
			cmd[i] = calloc(1,sizeof(char) * sizeof(buffer));
			strcpy(cmd[i],buffer);//add command
			pidflag=false;//reset flag
		}else{
			cmd[i] = calloc(1, (sizeof(char) * sizeof(input)));
			strcpy(cmd[i],token);//add command
		}
		i++;
		token = strtok(NULL,d);
	}
}

/*****************************************************************************
 *Function to change directories
 *Input: Cd with 0 or 1 argument, if 1 argument chdir to arg1 if 0 arg go to HOME
 *Output: smallsh changes directory to specified dir
*****************************************************************************/
void changeDir(){
	//if cd is passed in with an argument
	if (cmd[1] != NULL){
		if(chdir(cmd[1]) == -1){ //check if the directory path passed in as argument 1 exists
			printf("Sorry, that isn't a directory!\n");
			fflush(stdout);
			return; // break out of the function
		}
		chdir(cmd[1]);
	}else{
		chdir(getenv("HOME"));
	}
}

void status(int childExitStatus){
	if(WIFEXITED(childExitStatus)){//only if WIFEXITED evaluates to true can we get actual status
		printf("exit value %d\n",WEXITSTATUS(childExitStatus));
		fflush(stdout);	
	}else{//then it was executed by a signal
		printf("terminated by signal %d\n",WTERMSIG(childExitStatus));
		fflush(stdout);
	}
}

void execCommand(pid_t spawnPid, int* childExitStatus, char* cmd[],bool background, bool input, bool output){
	int ifd,ofd,result; // desclare input file descriptor and output file descriptor
	spawnPid = fork(); // spawn a new process to complete the command
	switch(spawnPid){
		//something has gone horribly wrong
		case -1: { perror("HULL BREACH!\n"); exit(1); break; }
		case 0 : {
		//we are the child process
						if(!_background){//if we're a fg process
							sigaction(SIGINT,&sa,NULL);//use default action for ^C						
						}else{//we are a bg process
							sigaction(SIGINT,&si,NULL);//ignore ^C
							sigaction(SIGTSTP,&si,NULL);//ignore ^Z
						}
						//if we are a bg command and no stdin given
						if (_background && !_input){
							ifd=open("/dev/null",O_RDONLY);
							result=dup2(ifd,0);//redirect stdin to devnull
							if (result == -1) { perror("dup2"); exit(2); }
							close(ifd);
						}
						//if we are a background command and no stdout given
						if (_background && !_output){
							ofd=open("/dev/null",O_WRONLY);
							result=dup2(ofd,1);//redirect stdout to devnull
							if (result == -1) { perror("dup2"); exit(2); }
							close(ofd);
						}
						if(_input){//if our command has the input operator ">"
							ifd=open(inputFile,O_RDONLY);//open the file specified
							if(ifd == -1) { printf("cannot open %s for input\n",inputFile); exit(1); }
								fflush(stdout);
								result = dup2(ifd,0); //redirect ifd to stdin
									if (result == -1) { perror("dup2"); exit(2); }

						fcntl(ifd, F_SETFD, FD_CLOEXEC);//close on Exec
						}

						if(_output){//if our command has the output operator "<"
							ofd=open(outputFile,O_WRONLY | O_CREAT | O_TRUNC, 0644);//open the file specified	
							if (ofd == -1) { perror("open()"); exit(1); }
								fflush(stdout);
								result = dup2(ofd,1); //redirect ofd to stdout 
									if (result == -1) { perror("dup2"); exit(2); }	
						
						fcntl(ofd, F_SETFD, FD_CLOEXEC); //Close on Exec
						}
					
						//finally execute the command
						execvp(cmd[0], cmd);
						perror("");
						exit(2); break;
						
						}
		default: {
		//we are the parent process
						
						if(_background){//if our background variable is true
							waitpid(spawnPid, childExitStatus,WNOHANG);	
							_background=false; //reset background
							printf("background pid is %d\n",spawnPid);
							fflush(stdout);
							bgprocesses[numBgProcesses] = spawnPid;//store current BG process in array
							numBgProcesses++;//increment number of background processes
							}else{//our child is a foreground process
							waitpid(spawnPid, childExitStatus,0);
							if (WIFSIGNALED(*childExitStatus)){//if foreground child process terminated by signal
								printf("terminated by signal %d\n",WTERMSIG(*childExitStatus));//print signal it was terminated by
								fflush(stdout);
							}
						}

						_input=false;//reset input 
						_output=false;//reset output
							
							//check for compelted background processes
							//loop through as many background processes as we have and check if they've terminated
							//if they have terminated, print their id and status
							int i;
							pid_t actualPid;
							for (i = 0; i < numBgProcesses; ++i){
								fflush(stdout);
								actualPid = waitpid(bgprocesses[i],childExitStatus,WNOHANG);
								if (actualPid > 0){//bg process terminated
									printf("background pid %d is done: ",actualPid);
									fflush(stdout);
									status(*childExitStatus);
								}
							}

						break;
		}
	}
}
//custom handler for catching ^Z
//Swap flag from true to false depending on current state
//If false, we can write in foreground or background, if true
//we can write in foreground only
void catchSIGTSTP(int signo){
	if(!_foregroundOnly){
	char* message = "\nEntering foreground-only mode (& is now ignored)\n";
	write(STDOUT_FILENO,message,50);
	_foregroundOnly=true;

	}else{
	char* message = "\nExiting foreground-only mode\n";
	write(STDOUT_FILENO,message,30);
	_foregroundOnly=false;
	}
}

