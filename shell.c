#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <grp.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>

struct node {
    char name[4097];
    int process_id;
    int bg;
    char status[7];
} typedef node;

pid_t executeProgram(char *args[], int rel, int background);
void searchBin(char *args[], int bg);
int get_position();
int get_pid_position(int pid);

void cd(char *args[], int length);
void jobs_command();
void kill_cmd(char *args[], int length);
void bg_cmd(char *args[], int length);
void fg_cmd(char *args[], int length);
void exit_cmd();

void stop_job(int pid);
void remove_job(int pid);
void add_jobs(int pid, int bg, char *args[]);

void sigint_handler();
void sigstp_handler();
void sigchild_handler();

volatile sig_atomic_t fg;
static node jobs[500];
volatile sig_atomic_t job_length = 1;
volatile sig_atomic_t exit_flag;

int main(int argc, char *argv[]){
    
    char *input = malloc(sizeof(char));
    size_t size = 1;

    // instal signal handlers
    if( signal(SIGCHLD, sigchild_handler) == SIG_ERR){
        printf("signal err");
        return 0;
    }

    if( signal(SIGINT, sigint_handler) == SIG_ERR){
        printf("signal err");
        return 0;
    }

    if( signal(SIGTSTP, sigstp_handler) == SIG_ERR){
        printf("signal err");
        return 0;
    }

    while(1){
        printf("> ");
        
        //get input
        getline(&input, &size, stdin);
        if(!strcmp(input, "\n")){
            continue;
        }
        //ctrl+D/EOF
        if(feof(stdin)) { 
            exit_cmd();
	        printf("\n");
            break;
	    }
        // remove newline @ end
        input[strlen(input)-1] = '\0';
        
        if(!strcmp(input, "exit")){
            exit_cmd();
            break;
        }
        //split args into string array
        char *token = strtok(input, " ");
        char **args = malloc(sizeof(char *)); 
        args[0] = token;
        int i = 1;

        while(token != NULL){
            token = strtok(NULL, " ");
            // dynamically resize args array
            args = realloc(args, sizeof(char*) * (i+1) );
            args[i] = token;
            i++;
        }
        if(args[0] == NULL){
            free(args);
            continue;
        }
        //built in shell commands
        if(!strcmp(args[0], "cd")){
            cd(args, i);
            free(args);
            continue;
        }
        if(!strcmp(args[0], "jobs")){
            jobs_command();
            free(args);
            continue;
        }
        if(!strcmp(args[0], "kill")){
            kill_cmd(args, i);
            free(args);
            continue;
        }
        if(!strcmp(args[0], "bg")){
            bg_cmd(args, i);
            free(args);
            continue;
        }
        if(!strcmp(args[0], "fg")){
            fg_cmd(args, i);
            free(args);
            continue;
        }


        //background process check
        int bg = 0;
        if(!strcmp(args[i-2], "&")){
            bg = 1;
            args[i-2] = NULL;
        }
        //check if command
        if((args[0][0] != '.') && (strstr(args[0], "/") == NULL)   ){
            searchBin(args, bg);
        //check for relative path
        } else if( (strstr(args[0], "/") != NULL) && (args[0][0] != '/') ) {
            char *relative = malloc(4097);
            strcpy(relative, "./");
            strcat(relative, args[0]);
            args[0] = relative;
            executeProgram(args, 1, bg);
            free(relative);
        //absolute path
        } else {
            executeProgram(args, 0, bg);
        }
        free(args);
        
    }
    free(input);
    return 0;
}

pid_t executeProgram(char *args[], int rel, int background){
    pid_t pid;
    int status;
    
    errno = 0;

    //signal masks
    sigset_t mask_all, mask_one, prev_one;
    sigfillset(&mask_all);
    sigemptyset(&mask_one);
    sigaddset(&mask_one, SIGCHLD);



    sigprocmask(SIG_BLOCK, &mask_one, &prev_one); 
    if((pid = fork()) < 0) {
        fprintf(stderr, "Fork error: %s\n", strerror(errno));
        return pid;
    }
    if(pid == 0){
        sigprocmask(SIG_SETMASK, &prev_one, NULL);
        setpgid(0,0);
        execv(args[0], args);
        //if execv fails
        if(rel)
            args[0] += 2;
        printf("%s: ", *args);
        printf("No such file or directory\n");
        exit(0);

    } else {
        sigprocmask(SIG_BLOCK, &mask_all, NULL);
        
        //if relative path
        //move ptr up by 2 to only get name
        if(rel)
            args[0] += 2;
        add_jobs(pid, background, args);
        sigprocmask(SIG_SETMASK, &prev_one, NULL); 

        //not background
        if(background == 0){
            fg =  pid;
            pid = waitpid(pid, &status, WUNTRACED);

            if(WIFSTOPPED(status)){
                sigprocmask(SIG_BLOCK, &mask_all, NULL);
                stop_job(pid);
                fg =  0;
                sigprocmask(SIG_SETMASK, &prev_one, NULL); 

            } else if(WIFEXITED(status)) {
                sigprocmask(SIG_BLOCK, &mask_all, NULL);
                fg =  0;
                remove_job(pid);
                sigprocmask(SIG_SETMASK, &prev_one, NULL); 

            } else if(WIFSIGNALED(status)) {
                sigprocmask(SIG_BLOCK, &mask_all, NULL);
                int x = get_pid_position(pid);
                printf("[%d] %d terminated by signal %d\n", x, pid, WTERMSIG(status));
                remove_job(pid);
                sigprocmask(SIG_SETMASK, &prev_one, NULL); 
            }
        } else {
            // running in background
            int x = get_pid_position(pid);
            if(x){
                sigprocmask(SIG_BLOCK, &mask_all, NULL);
                printf("[%d] %d\n", x, pid);
                sigprocmask(SIG_SETMASK, &prev_one, NULL); 
            }
        }
        //if it is in background 
        //sigchild handler can deal with it
        
        return pid;
    }

}

void searchBin(char *args[], int bg){
    struct dirent *dir;
    DIR *dirp = opendir("/usr/bin");
    errno = 0;
            
    while((dir = readdir(dirp)) != NULL) {
        char *str = dir->d_name;
        //get rid of dots
        if(str[0] != '.'){
            if(!strcmp(str, args[0])){
                //correct path if found in usr bin
                char *new_path = malloc(11 + sizeof(args[0]));
                strcpy(new_path, "/usr/bin/");
                strcat(new_path, args[0]);
                args[0] = new_path;
                closedir(dirp);
                executeProgram(args, 0, bg);
                free(new_path);
 		return;
            }
        }
    }
    
    closedir(dirp);
    DIR *new_dirp = opendir("/bin");

    while((dir = readdir(new_dirp)) != NULL) {
        char *str = dir->d_name;
        //get rid of dots
        if(str[0] != '.'){
            if(!strcmp(str, args[0])){
                //correct path if found in bin
                char *new_path = malloc(5 + sizeof(args[0]));
                strcpy(new_path, "/bin/");
                strcat(new_path, args[0]);
                args[0] = new_path;
                closedir(new_dirp);
                executeProgram(args, 0, bg);
                free(new_path);
                return;
            }
        }
    }
    closedir(new_dirp);
    printf("%s: command not found\n", args[0]);
    if(errno)
        printf("error!");
}

void cd(char *args[], int length){
    
    int val;
    if(length == 2){
        char *home = getenv("HOME");
        val = chdir(home);
        if(val < 0){
            printf("Unable to find home\n");
            return;
        }
        return;
    }
    val = chdir(args[1]);
    if(val < 0){
        printf("%s: %s: No such file or directory\n", args[0], args[1]);
        return;
    }  
    return;
}

void exit_cmd(){
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);

    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    exit_flag = 1;
    sigprocmask(SIG_SETMASK, &prev_all, NULL);

    //sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id != 0){
            pid_t pid = jobs[i].process_id;
            if(!strcmp(jobs[i].status, "Stopped")){
                sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
                kill(pid, SIGHUP);
                kill(pid, SIGCONT);
                sigprocmask(SIG_SETMASK, &prev_all, NULL);
            } else {
                sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
                kill(pid, SIGHUP);
                sigprocmask(SIG_SETMASK, &prev_all, NULL);
            }
        }
    }
}

void jobs_command(){

    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    
    
    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id != 0) {
            sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            printf("[%d] %d %s %s", (i+1), jobs[i].process_id, jobs[i].status, jobs[i].name);
            if(jobs[i].bg != 0) {
                printf(" &\n");
            } else {
                printf("\n");
            }
            sigprocmask(SIG_SETMASK, &prev_all, NULL);
                
        }
    }
    return;
}

void fg_cmd(char *args[], int length){
    if(length == 2) {
        return;
    }
    
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

    args[1]++;
    int job_id = atoi(args[1]);
    job_id = job_id - 1;

    if(jobs[job_id].process_id == 0){
        sigprocmask(SIG_SETMASK, &prev_all, NULL);
        return;
    }
    kill(jobs[job_id].process_id, SIGCONT);
    strcpy(jobs[job_id].status, "Running");
    jobs[job_id].bg = 0;
    pid_t pid = jobs[job_id].process_id;
    fg = pid;
    sigprocmask(SIG_SETMASK, &prev_all, NULL);
    
    //wait for foreground to complete
    int status;
    waitpid(pid, &status, WUNTRACED);

    sigset_t m_all, p_all;
    sigfillset(&m_all);
    //sigprocmask(SIG_BLOCK, &m_all, &p_all);

    if(WIFSTOPPED(status)){
        sigprocmask(SIG_BLOCK, &m_all, NULL);
        fg =  0;
        stop_job(pid);
        sigprocmask(SIG_SETMASK, &p_all, NULL); 
    } else if(WIFEXITED(status)) {
        sigprocmask(SIG_BLOCK, &m_all, NULL);
        fg =  0;
        remove_job(pid);
        sigprocmask(SIG_SETMASK, &p_all, NULL); 
    } else if(WIFSIGNALED(status)) {
        sigprocmask(SIG_BLOCK, &m_all, NULL);
        int x = get_pid_position(pid);
        printf("[%d] %d terminated by signal %d\n", x, pid, WTERMSIG(status));
        remove_job(pid);
        sigprocmask(SIG_SETMASK, &p_all, NULL); 
    }

}

void bg_cmd(char *args[], int length){
    if(length == 2) {
        return;
    }
    
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

    args[1]++;
    int job_id = atoi(args[1]);
    job_id = job_id - 1;

    if(jobs[job_id].process_id == 0){
        sigprocmask(SIG_SETMASK, &prev_all, NULL);
        return;
    }
    kill(jobs[job_id].process_id, SIGCONT);
    jobs[job_id].bg = 1;
    strcpy(jobs[job_id].status, "Running");
    sigprocmask(SIG_SETMASK, &prev_all, NULL);

}

void kill_cmd(char *args[], int length){
    if(length == 2) {
        return;
    }
    
    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);

    args[1]++;
    
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    int job_id = atoi(args[1]);
    job_id = job_id - 1;
    if(jobs[job_id].process_id == 0){
        sigprocmask(SIG_SETMASK, &prev_all, NULL);
        return;
    }
    int pro_id = jobs[job_id].process_id;
    int x = job_id + 1;
    printf("[%d] %d terminated by signal 15\n", x, pro_id);
    kill(jobs[job_id].process_id, SIGTERM);
    if(fg == pro_id){fg = 0;}
    remove_job(jobs[job_id].process_id);
    sigprocmask(SIG_SETMASK, &prev_all, NULL);
    return;
}

void sigint_handler(){
    
    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);

    if(!fg){
        char buf[4];
        int fd = 0;
        strcpy(buf, "\n> ");
        int nbytes = strlen(buf);
        write(fd, buf, nbytes);

    } else {
        char buf[2];
        int fd = 0;
        strcpy(buf, "\n");
        int nbytes = strlen(buf);
        write(fd, buf, nbytes);
        
        
        sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        // int x = get_position();
        // printf("[%d] %d terminated by signal 2\n", x, fg);
        kill(fg, SIGINT);
        //remove_job(fg);
        fg = 0;
        sigprocmask(SIG_SETMASK, &prev_all, NULL);

        
    }
    
    fflush(stdout);
    return;
}
void sigchild_handler(){
    int status;
    int old = errno;
    pid_t pid;

    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);
    
    //reap all zombie children (mostly for background)
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("%d\n", pid);
        if(WIFSIGNALED(status)){
            sigprocmask(SIG_BLOCK, &mask_all, NULL);
            int x = get_pid_position(pid);
            if(!x){continue;}
            if(exit_flag == 0)
                printf("[%d] %d terminated by signal %d\n", x, pid, WTERMSIG(status));
            remove_job(pid);
            sigprocmask(SIG_SETMASK, &prev_all, NULL);
        }
        sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        //maybe print
        // int x = get_pid_position(pid);
        // if(!x){continue;}
        // printf("[%d] %d terminated by signal %d\n", x, pid, WTERMSIG(status));
        remove_job(pid);
        sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    errno = old;
    fflush(stdout);
    return;
    
}
void sigstp_handler(){

    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);
    
    if(!fg){

        char buf[4];
        int fd = 0;
        
        strcpy(buf, "\n> ");
        int nbytes = strlen(buf);
        write(fd, buf, nbytes);

    } else {
        sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        kill(fg, SIGTSTP);
        fg = 0;
        sigprocmask(SIG_SETMASK, &prev_all, NULL);
        char buf[2];
        int fd = 0;
        
        strcpy(buf, "\n");
        int nbytes = strlen(buf);
        write(fd, buf, nbytes);
    }
    
    fflush(stdout);
    return;
}

void remove_job(int pid){
    if(pid == 0){
        return;
    }
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);


    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id == pid){
            sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            jobs[i].process_id = 0;
            jobs[i].bg = 0;
            sigprocmask(SIG_SETMASK, &prev_all, NULL);
            return;
        }
    }
}

void add_jobs(int pid, int bg, char *args[]){
    
    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);

    //printf("%d\n", job_length);

    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id == 0){
            sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            jobs[i].process_id = pid;
            jobs[i].bg = bg;
            strcpy(jobs[i].status, "Running");
            strcpy(jobs[i].name, args[0]);

            int check = 1;
            while(args[check] != NULL){
                strcat(jobs[i].name, " ");
                strcat(jobs[i].name, args[check]);
                check++;
            }

            int x = job_length;
            x++;
            job_length = x;

            sigprocmask(SIG_SETMASK, &prev_all, NULL);
            return;
        }
    }
}

void stop_job(int pid){

    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);

    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id == pid){
            sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            strcpy(jobs[i].status, "Stopped");
            sigprocmask(SIG_SETMASK, &prev_all, NULL);
            return;
        }
    }
}

int get_position(){
    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id == fg){
            return (i+1);
        }
    }
    return 0;
}

int get_pid_position(int pid){
    for(int i = 0; i < job_length; i++){
        if(jobs[i].process_id == pid){
            return (i+1);
        }
    }
    return 0;
}
