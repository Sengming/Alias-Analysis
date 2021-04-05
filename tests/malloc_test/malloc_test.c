#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct pointed_to {
    int b;
    int c;
    struct storage_struct *reverseptr;
};

struct storage_struct {
    struct pointed_to *test;
    char *test2;
    int a;
    struct pointed_to notptr;
    struct pointed_to *ptr;
    void (*usedfunc)(char *);
};

struct dummy_struct {
    int a;
    int b;
};

struct storage_struct storage;
struct pointed_to pointed;
// struct storage_struct anotherstorage;

/**
 * @brief Should not trigger alias detection
 */
static void store_load_local() {
    int out = 0;
    struct storage_struct *malloc_localptr =
        (struct storage_struct *)malloc(sizeof(struct storage_struct));
    malloc_localptr->a = 5;
    out = malloc_localptr->a;
    free(malloc_localptr);
    //`int out = 0;
    //`struct dummy_struct *malloc_localptr =
    //`    (struct dummy_struct *)malloc(sizeof(struct dummy_struct));
    //`malloc_localptr->a = 5;
    //`out = malloc_localptr->a;
    //`free(malloc_localptr);
}

/**
 * @brief Should trigger alias detection on both store and load
 */
static void store_load_global() {
    void *ptr = NULL;
    struct storage_struct *malloc_ptr =
        (struct storage_struct *)malloc(sizeof(struct storage_struct));
    malloc_ptr->ptr = &pointed;
    ptr = malloc_ptr->ptr; // load address of global and give to local
    printf("%p\n", ptr);
    free(malloc_ptr);
}

/**
 * @brief Should trigger alias detection on both store and load
 */
static void store_load_global_address_taken() {
    int *ptr_eight = NULL;
    struct storage_struct *malloc_ptr =
        (struct storage_struct *)malloc(sizeof(struct storage_struct));
    ptr_eight = &(malloc_ptr->ptr); // load address of global and give to local

    ptr_eight -= 8;
    *ptr_eight = &storage;
    free(malloc_ptr);
}

/**
 * @brief Should not trigger alias detection on both store and load
 */
static void store_load_global_noaddr() {
    struct storage_struct *malloc_ptr =
        (struct storage_struct *)malloc(sizeof(struct storage_struct));
    malloc_ptr->notptr = pointed;
    free(malloc_ptr);
}

int main() {
    store_load_local();
    store_load_global();
    store_load_global_address_taken();
    store_load_global_noaddr();
    // newtestptr = malloc(1000);
    //*((unsigned long *)(newtestptr + 8)) = 10;
    // copyptr1 = &(malloc_ptr->a);
    // malloc_ptr->ptr->b = 2;
    // copyptr2 = copyptr1 - 32;
    // copyptr2 = &anotherstorage;
    return 0;
}
