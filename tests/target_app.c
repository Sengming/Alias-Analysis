#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
//#include <lightweight_mvx.h>

char *globalpointer;

struct pointed_to {
    int b;
    int c;
};

struct storage_struct {
    int a;
    struct pointed_to *ptr;
};

struct storage_struct storage;
struct pointed_to pointed;

struct storage_struct *copy_storage;

void call_other_function(char *string) {
    int pid = 0;
    char *stack;
    char *stackTop;
    char *pointer;

    printf("This is the string: %s\n", string);
    stack = (char *)malloc(4096);
    stackTop = stack + 4096;

    pointer = stack;
    globalpointer = pointer;

    storage.ptr = &pointed;
    copy_storage = &storage;

    printf("Pointer is %p\n", pointer);
}

int main() { call_other_function("Pass this string to function"); }
