#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern void used_function(char *string);

void *globalpointer;
void *otherglobal;

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

void call_other_function(char *string) {
    int pid = 0;
    char *pointer;

    // copy_storage->ptr->c = 5;
    if (copy_storage->ptr->c > 0) {
        (*((int *)otherglobal))++;
        if (copy_storage->ptr->reverseptr->a > 1)
            anotherstorage.ptr = (struct pointed_to *)globalpointer;
        copy_storage->usedfunc("test");
    }
}

int main() {
    pointed.reverseptr = &anotherstorage;
    storage.usedfunc = used_function;
    copy_storage = &storage;
    globalpointer = &pointed;
    storage.ptr = &pointed;
    otherglobal = &storage.a;

    call_other_function("Pass this string to function");
    return 0;
}
