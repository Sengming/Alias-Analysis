#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
//#include <lightweight_mvx.h>

extern struct storage_struct *copy_storage;
extern char *globalpointer;
char *used_function_ptr;

void used_function(char *string) {
    int pid = 0;
    char *stack;
    char *stackTop;
    char *pointer;
    used_function_ptr = (char*)copy_storage;
    printf("This is the string: %s\n", string);
    stack = (char *)malloc(4096);
    stackTop = stack + 4096;
}
