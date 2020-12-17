#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern void used_function(char *string);

struct pointed_to {
    int b;
    int c;
    struct storage_struct *reverseptr;
};

struct storage_struct {
    struct pointed_to *test;
    char *test2;
    int a;
    struct pointed_to *ptr;
    void (*usedfunc)(char *);
};

struct storage_struct storage;
struct pointed_to pointed;

struct storage_struct anotherstorage;

struct storage_struct *copy_storage;

int main() {
    struct storage_struct *malloc_ptr =
        (struct storage_struct *)malloc(sizeof(struct storage_struct));
    malloc_ptr->ptr = &pointed;
    return 0;
}
