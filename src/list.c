/*****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2023 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <https://dun.github.io/conman/>.
 *
 *  ConMan is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Refer to "list.h" for documentation on public functions.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#if WITH_PTHREADS
#  include <errno.h>
#  include <pthread.h>
#  include <stdio.h>
#  include <unistd.h>
#endif /* WITH_PTHREADS */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"


/*******************\
**  Out of Memory  **
\*******************/

#ifdef WITH_OOMF
#  undef out_of_memory
   extern void * out_of_memory(void);
#else /* !WITH_OOMF */
#  ifndef out_of_memory
#    define out_of_memory() (NULL)
#  endif /* !out_of_memory */
#endif /* WITH_OOMF */


/***************\
**  Constants  **
\***************/

#define LIST_ALLOC 10
#define LIST_MAGIC 0xDEADBEEF


/****************\
**  Data Types  **
\****************/

struct listNode {
    void                 *data;         /* node's data                       */
    struct listNode      *next;         /* next node in list                 */
};

struct listIterator {
    struct list          *list;         /* the list being iterated           */
    struct listNode      *pos;          /* the next node to be iterated      */
    struct listNode     **prev;         /* addr of 'next' ptr to prv It node */
    struct listIterator  *iNext;        /* iterator chain for list_destroy() */
#ifndef NDEBUG
    unsigned int          magic;        /* sentinel for asserting validity   */
#endif /* NDEBUG */
};

struct list {
    struct listNode      *head;         /* head of the list                  */
    struct listNode     **tail;         /* addr of last node's 'next' ptr    */
    struct listIterator  *iNext;        /* iterator chain for list_destroy() */
    ListDelF              fDel;         /* function to delete node data      */
    int                   count;        /* number of nodes in list           */
#if WITH_PTHREADS
    pthread_mutex_t       mutex;        /* mutex to protect access to list   */
#endif /* WITH_PTHREADS */
#ifndef NDEBUG
    unsigned int          magic;        /* sentinel for asserting validity   */
#endif /* NDEBUG */
};

typedef struct listNode * ListNode;


/****************\
**  Prototypes  **
\****************/

static void * list_node_create(List l, ListNode *pp, void *x);
static void * list_node_destroy(List l, ListNode *pp);
static List list_alloc(void);
static ListNode list_node_alloc(void);
static ListIterator list_iterator_alloc(void);
static void list_free(List l);
static void list_node_free(ListNode p);
static void list_iterator_free(ListIterator i);


/***************\
**  Variables  **
\***************/

static List freeLists = NULL;
static ListNode freeListNodes = NULL;
static ListIterator freeListIterators = NULL;
#if WITH_PTHREADS
static pthread_mutex_t freeListsLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t freeListNodesLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t freeListIteratorsLock = PTHREAD_MUTEX_INITIALIZER;
#endif /* WITH_PTHREADS */


/************\
**  Macros  **
\************/

#if WITH_PTHREADS

#  define list_mutex_init(mutex)                                              \
     do {                                                                     \
         if ((errno = pthread_mutex_init(mutex, NULL)) != 0)                  \
             perror("ERROR: pthread_mutex_init() failed"), exit(1);           \
     } while (0)

#  define list_mutex_lock(mutex)                                              \
     do {                                                                     \
         if ((errno = pthread_mutex_lock(mutex)) != 0)                        \
             perror("ERROR: pthread_mutex_lock() failed"), exit(1);           \
     } while (0)

#  define list_mutex_unlock(mutex)                                            \
     do {                                                                     \
         if ((errno = pthread_mutex_unlock(mutex)) != 0)                      \
             perror("ERROR: pthread_mutex_unlock() failed"), exit(1);         \
     } while (0)

#  define list_mutex_destroy(mutex)                                           \
     do {                                                                     \
         if ((errno = pthread_mutex_destroy(mutex)) != 0)                     \
             perror("ERROR: pthread_mutex_destroy() failed"), exit(1);        \
     } while (0)

#else /* !WITH_PTHREADS */

#  define list_mutex_init(mutex)
#  define list_mutex_lock(mutex)
#  define list_mutex_unlock(mutex)
#  define list_mutex_destroy(mutex)

#endif /* WITH_PTHREADS */


/***************\
**  Functions  **
\***************/

List list_create(ListDelF f)
{
    List l;

    if (!(l = list_alloc()))
        return(out_of_memory());
    l->head = NULL;
    l->tail = &l->head;
    l->iNext = NULL;
    l->fDel = f;
    l->count = 0;
    list_mutex_init(&l->mutex);
    assert((l->magic = LIST_MAGIC));    /* set magic via assert abuse */
    return(l);
}


void list_destroy(List l)
{
    ListIterator i, iTmp;
    ListNode p, pTmp;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    i = l->iNext;
    while (i) {
        assert(i->magic == LIST_MAGIC);
        iTmp = i->iNext;
        assert((i->magic = 1));         /* clear magic via assert abuse */
        list_iterator_free(i);
        i = iTmp;
    }
    p = l->head;
    while (p) {
        pTmp = p->next;
        if (p->data && l->fDel)
            l->fDel(p->data);
        list_node_free(p);
        p = pTmp;
    }
    assert((l->magic = 1));             /* clear magic via assert abuse */
    list_mutex_unlock(&l->mutex);
    list_mutex_destroy(&l->mutex);
    list_free(l);
    return;
}


int list_is_empty(List l)
{
    int n;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    n = l->count;
    list_mutex_unlock(&l->mutex);
    return(n == 0);
}


int list_count(List l)
{
    int n;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    n = l->count;
    list_mutex_unlock(&l->mutex);
    return(n);
}


void * list_append(List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, l->tail, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void * list_prepend(List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, &l->head, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void * list_find_first(List l, ListFindF f, void *key)
{
    ListNode p;
    void *v = NULL;

    assert(l != NULL);
    assert(f != NULL);
    assert(key != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    for (p=l->head; p; p=p->next) {
        if (f(p->data, key)) {
            v = p->data;
            break;
        }
    }
    list_mutex_unlock(&l->mutex);
    return(v);
}


int list_delete_all(List l, ListFindF f, void *key)
{
    ListNode *pp;
    void *v;
    int n = 0;

    assert(l != NULL);
    assert(f != NULL);
    assert(key != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    pp = &l->head;
    while (*pp) {
        if (f((*pp)->data, key)) {
            if ((v = list_node_destroy(l, pp))) {
                if (l->fDel)
                    l->fDel(v);
                n++;
            }
        }
        else {
            pp = &(*pp)->next;
        }
    }
    list_mutex_unlock(&l->mutex);
    return(n);
}


void list_sort(List l, ListCmpF f)
{
/*  Note: Time complexity O(n^2).
 */
    ListNode *pp, *ppPrev, *ppPos, pTmp;
    ListIterator i;

    assert(l != NULL);
    assert(f != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    if (l->count > 1) {
        ppPrev = &l->head;
        pp = &(*ppPrev)->next;
        while (*pp) {
            if (f((*pp)->data, (*ppPrev)->data) < 0) {
                ppPos = &l->head;
                while (f((*pp)->data, (*ppPos)->data) >= 0)
                    ppPos = &(*ppPos)->next;
                pTmp = (*pp)->next;
                (*pp)->next = *ppPos;
                *ppPos = *pp;
                *pp = pTmp;
                if (ppPrev == ppPos)
                    ppPrev = &(*ppPrev)->next;
            }
            else {
                ppPrev = pp;
                pp = &(*pp)->next;
            }
        }
        l->tail = pp;

        for (i=l->iNext; i; i=i->iNext) {
            assert(i->magic == LIST_MAGIC);
            i->pos = i->list->head;
            i->prev = &i->list->head;
        }
    }
    list_mutex_unlock(&l->mutex);
    return;
}


void * list_push(List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, &l->head, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void * list_pop(List l)
{
    void *v;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_destroy(l, &l->head);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void * list_peek(List l)
{
    void *v;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = (l->head) ? l->head->data : NULL;
    list_mutex_unlock(&l->mutex);
    return(v);
}


void * list_enqueue(List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, l->tail, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void * list_dequeue(List l)
{
    void *v;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_destroy(l, &l->head);
    list_mutex_unlock(&l->mutex);
    return(v);
}


ListIterator list_iterator_create(List l)
{
    ListIterator i;

    assert(l != NULL);
    if (!(i = list_iterator_alloc()))
        return(out_of_memory());
    i->list = l;
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    i->pos = l->head;
    i->prev = &l->head;
    i->iNext = l->iNext;
    l->iNext = i;
    list_mutex_unlock(&l->mutex);
    assert((i->magic = LIST_MAGIC));    /* set magic via assert abuse */
    return(i);
}


void list_iterator_reset(ListIterator i)
{
    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    i->pos = i->list->head;
    i->prev = &i->list->head;
    list_mutex_unlock(&i->list->mutex);
    return;
}


void list_iterator_destroy(ListIterator i)
{
    ListIterator *pi;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    for (pi=&i->list->iNext; *pi; pi=&(*pi)->iNext) {
        assert((*pi)->magic == LIST_MAGIC);
        if (*pi == i) {
            *pi = (*pi)->iNext;
            break;
        }
    }
    list_mutex_unlock(&i->list->mutex);
    assert((i->magic = 1));             /* clear magic via assert abuse */
    list_iterator_free(i);
    return;
}


void * list_next(ListIterator i)
{
    ListNode p;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if ((p = i->pos))
        i->pos = p->next;
    if (*i->prev != p)
        i->prev = &(*i->prev)->next;
    list_mutex_unlock(&i->list->mutex);
    return(p ? p->data : NULL);
}


void * list_insert(ListIterator i, void *x)
{
    void *v;

    assert(i != NULL);
    assert(x != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    v = list_node_create(i->list, i->prev, x);
    list_mutex_unlock(&i->list->mutex);
    return(v);
}


void * list_find(ListIterator i, ListFindF f, void *key)
{
    void *v;

    assert(i != NULL);
    assert(f != NULL);
    assert(key != NULL);
    assert(i->magic == LIST_MAGIC);
    while ((v=list_next(i)) && !f(v,key)) {;}
    return(v);
}


void * list_remove(ListIterator i)
{
    void *v = NULL;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if (*i->prev != i->pos)
        v = list_node_destroy(i->list, i->prev);
    list_mutex_unlock(&i->list->mutex);
    return(v);
}


int list_delete(ListIterator i)
{
    void *v;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    if ((v = list_remove(i))) {
        if (i->list->fDel)
            i->list->fDel(v);
        return(1);
    }
    return(0);
}


static void * list_node_create(List l, ListNode *pp, void *x)
{
/*  Inserts data pointed to by (x) into list (l) after (pp),
 *    the address of the previous node's "next" ptr.
 *  Returns a ptr to data (x), or NULL if insertion fails.
 *  This routine assumes the list is already locked upon entry.
 */
    ListNode p;
    ListIterator i;

    assert(l != NULL);
    assert(l->magic == LIST_MAGIC);
    assert(pp != NULL);
    assert(x != NULL);
    if (!(p = list_node_alloc()))
        return(out_of_memory());
    p->data = x;
    if (!(p->next = *pp))
        l->tail = &p->next;
    *pp = p;
    l->count++;
    for (i=l->iNext; i; i=i->iNext) {
        assert(i->magic == LIST_MAGIC);
        if (i->prev == pp)
            i->prev = &p->next;
        else if (i->pos == p->next)
            i->pos = p;
        assert((i->pos == *i->prev) || (i->pos == (*i->prev)->next));
    }
    return(x);
}


static void * list_node_destroy(List l, ListNode *pp)
{
/*  Removes the node pointed to by (*pp) from from list (l),
 *    where (pp) is the address of the previous node's "next" ptr.
 *  Returns the data ptr associated with list item being removed,
 *    or NULL if (*pp) points to the NULL element.
 *  This routine assumes the list is already locked upon entry.
 */
    void *v;
    ListNode p;
    ListIterator i;

    assert(l != NULL);
    assert(l->magic == LIST_MAGIC);
    assert(pp != NULL);
    if (!(p = *pp))
        return(NULL);
    v = p->data;
    if (!(*pp = p->next))
        l->tail = pp;
    l->count--;
    for (i=l->iNext; i; i=i->iNext) {
        assert(i->magic == LIST_MAGIC);
        if (i->pos == p)
            i->pos = p->next, i->prev = pp;
        else if (i->prev == &p->next)
            i->prev = pp;
        assert((i->pos == *i->prev) || (i->pos == (*i->prev)->next));
    }
    list_node_free(p);
    return(v);
}


static List list_alloc(void)
{
    List l;
    List last;

    list_mutex_lock(&freeListsLock);
    if (!freeLists) {
        freeLists = malloc(LIST_ALLOC * sizeof(struct list));
        if (freeLists) {
            last = freeLists + LIST_ALLOC - 1;
            for (l=freeLists; l<last; l++)
                l->iNext = (ListIterator) (l + 1);
            last->iNext = NULL;
        }
    }
    if ((l = freeLists))
        freeLists = (List) freeLists->iNext;
    list_mutex_unlock(&freeListsLock);
    return(l);
}


static ListNode list_node_alloc(void)
{
    ListNode p;
    ListNode last;

    list_mutex_lock(&freeListNodesLock);
    if (!freeListNodes) {
        freeListNodes = malloc(LIST_ALLOC * sizeof(struct listNode));
        if (freeListNodes) {
            last = freeListNodes + LIST_ALLOC - 1;
            for (p=freeListNodes; p<last; p++)
                p->next = p + 1;
            last->next = NULL;
        }
    }
    if ((p = freeListNodes))
        freeListNodes = freeListNodes->next;
    list_mutex_unlock(&freeListNodesLock);
    return(p);
}


static ListIterator list_iterator_alloc(void)
{
    ListIterator i;
    ListIterator last;

    list_mutex_lock(&freeListIteratorsLock);
    if (!freeListIterators) {
        freeListIterators = malloc(LIST_ALLOC * sizeof(struct listIterator));
        if (freeListIterators) {
            last = freeListIterators + LIST_ALLOC - 1;
            for (i=freeListIterators; i<last; i++)
                i->iNext = i + 1;
            last->iNext = NULL;
        }
    }
    if ((i = freeListIterators))
        freeListIterators = freeListIterators->iNext;
    list_mutex_unlock(&freeListIteratorsLock);
    return(i);
}


static void list_free(List l)
{
    list_mutex_lock(&freeListsLock);
    l->iNext = (ListIterator) freeLists;
    freeLists = l;
    list_mutex_unlock(&freeListsLock);
    return;
}


static void list_node_free(ListNode p)
{
    list_mutex_lock(&freeListNodesLock);
    p->next = freeListNodes;
    freeListNodes = p;
    list_mutex_unlock(&freeListNodesLock);
    return;
}


static void list_iterator_free(ListIterator i)
{
    list_mutex_lock(&freeListIteratorsLock);
    i->iNext = freeListIterators;
    freeListIterators = i;
    list_mutex_unlock(&freeListIteratorsLock);
    return;
}
