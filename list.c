/******************************************************************************\
 *  $Id: list.c,v 1.8 2001/08/14 23:16:47 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "list.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef USE_THREAD_SAFE_LIST
#  include <errno.h>
#  include <pthread.h>
#endif /* USE_THREAD_SAFE_LIST */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"


#define LIST_MAGIC 0xCD


struct listNode {
    void                 *data;		/* node's data                        */
    struct listNode      *next;		/* next node in list                  */
};

struct listIterator {
    struct list          *list;		/* the list being iterated            */
    struct listNode      *pos;		/* the next node to be iterated       */
    struct listNode     **prev;		/* addr of 'next' ptr to prev It node */
    struct listIterator  *iNext;	/* iterator chain for list_destroy()  */
    unsigned char         magic;	/* sentinel for asserting validity    */
};

struct list {
    struct listNode      *head;		/* head of the list                   */
    struct listNode     **tail;		/* addr of last node's 'next' ptr     */
    struct listIterator  *iNext;	/* iterator chain for list_destroy()  */
    ListDelF              fDel;		/* function to delete node data       */
    int                   count;	/* number of nodes in list            */
    unsigned char         magic;	/* sentinel for asserting validity    */
#ifdef USE_THREAD_SAFE_LIST
    pthread_mutex_t       mutex;	/* mutex to protect access to list    */
#endif /* USE_THREAD_SAFE_LIST */
};

typedef struct listNode * ListNode;


static void * list_node_create(List l, ListNode *pp, void *x);
static void * list_node_destroy(List l, ListNode *pp);


#ifdef USE_THREAD_SAFE_LIST

#  define LIST_INIT(mutex)                                                     \
     do {                                                                      \
         if ((errno = pthread_mutex_init(mutex, NULL)) != 0)                   \
             perror("ERROR: pthread_mutex_init() failed"), exit(1);            \
     } while (0)

#  define LIST_LOCK(mutex)                                                     \
     do {                                                                      \
         if ((errno = pthread_mutex_lock(mutex)) != 0)                         \
             perror("ERROR: pthread_mutex_lock() failed"), exit(1);            \
     } while (0)

#  define LIST_UNLOCK(mutex)                                                   \
     do {                                                                      \
         if ((errno = pthread_mutex_unlock(mutex)) != 0)                       \
             perror("ERROR: pthread_mutex_unlock() failed"), exit(1);          \
     } while (0)

#  define LIST_DESTROY(mutex)                                                  \
     do {                                                                      \
         if ((errno = pthread_mutex_destroy(mutex)) != 0)                      \
             perror("ERROR: pthread_mutex_destroy() failed"), exit(1);         \
     } while (0)

#else /* !USE_THREAD_SAFE_LIST */

#  define LIST_INIT(mutex)
#  define LIST_LOCK(mutex)
#  define LIST_UNLOCK(mutex)
#  define LIST_DESTROY(mutex)

#endif /* USE_THREAD_SAFE_LIST */


List list_create(ListDelF f)
{
    List l;

    if (!(l = (List) malloc(sizeof(struct list))))
        return(NULL);
    l->head = NULL;
    l->tail = &l->head;
    l->iNext = NULL;
    l->fDel = f;
    l->count = 0;
    LIST_INIT(&l->mutex);
    l->magic = LIST_MAGIC;
    return(l);
}


void list_destroy(List l)
{
    ListIterator i, iTmp;
    ListNode p, pTmp;

    assert(l);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    i = l->iNext;
    while (i) {
        assert(i->magic == LIST_MAGIC);
        iTmp = i->iNext;
        i->magic = 0;
        free(i);
        i = iTmp;
    }
    p = l->head;
    while (p) {
        pTmp = p->next;
        if (p->data && l->fDel)
            l->fDel(p->data);
        free(p);
        p = pTmp;
    }
    l->magic = 0;
    LIST_UNLOCK(&l->mutex);
    LIST_DESTROY(&l->mutex);
    free(l);
    return;
}


int list_is_empty(List l)
{
    int n;

    assert(l);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    n = l->count;
    LIST_UNLOCK(&l->mutex);
    return(n == 0);
}


int list_count(List l)
{
    int n;

    assert(l);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    n = l->count;
    LIST_UNLOCK(&l->mutex);
    return(n);
}


void * list_append(List l, void *x)
{
    void *v;

    assert(l);
    assert(x);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, l->tail, x);
    LIST_UNLOCK(&l->mutex);
    return(v);
}


void * list_prepend(List l, void *x)
{
    void *v;

    assert(l);
    assert(x);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, &l->head, x);
    LIST_UNLOCK(&l->mutex);
    return(v);
}


void * list_find_first(List l, ListFindF f, void *key)
{
    ListNode p;
    void *v = NULL;

    assert(l);
    assert(f);
    assert(key);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    for (p=l->head; p; p=p->next) {
        if (f(p->data, key)) {
            v = p->data;
            break;
        }
    }
    LIST_UNLOCK(&l->mutex);
    return(v);
}


int list_delete_all(List l, ListFindF f, void *key)
{
    ListNode *pp;
    void *v;
    int n = 0;

    assert(l);
    assert(f);
    assert(key);
    LIST_LOCK(&l->mutex);
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
    LIST_UNLOCK(&l->mutex);
    return(n);
}


void list_sort(List l, ListCmpF f)
{
/*  Note: Time complexity O(n^2).
 */
    ListNode *pp, *ppPrev, *ppPos, pTmp;
    ListIterator i;

    assert(l);
    assert(f);
    LIST_LOCK(&l->mutex);
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
    LIST_UNLOCK(&l->mutex);
    return;
}


void * list_push(List l, void *x)
{
    void *v;

    assert(l);
    assert(x);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, &l->head, x);
    LIST_UNLOCK(&l->mutex);
    return(v);
}


void * list_pop(List l)
{
    void *v;

    assert(l);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_destroy(l, &l->head);
    LIST_UNLOCK(&l->mutex);
    return(v);
}


void * list_peek(List l)
{
    void *v;

    assert(l);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = (l->head) ? l->head->data : NULL;
    LIST_UNLOCK(&l->mutex);
    return(v);
}


void * list_enqueue(List l, void *x)
{
    void *v;

    assert(l);
    assert(x);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, l->tail, x);
    LIST_UNLOCK(&l->mutex);
    return(v);
}


void * list_dequeue(List l)
{
    void *v;

    assert(l);
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_destroy(l, &l->head);
    LIST_UNLOCK(&l->mutex);
    return(v);
}


ListIterator list_iterator_create(List l)
{
    ListIterator i;

    assert(l);
    if (!(i = (ListIterator) malloc(sizeof(struct listIterator))))
        return(NULL);
    i->list = l;
    LIST_LOCK(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    i->pos = l->head;
    i->prev = &l->head;
    i->iNext = l->iNext;
    l->iNext = i;
    LIST_UNLOCK(&l->mutex);
    i->magic = LIST_MAGIC;
    return(i);
}


void list_iterator_reset(ListIterator i)
{
    assert(i);
    assert(i->magic == LIST_MAGIC);
    LIST_LOCK(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    i->pos = i->list->head;
    i->prev = &i->list->head;
    LIST_UNLOCK(&i->list->mutex);
    return;
}


void list_iterator_destroy(ListIterator i)
{
    ListIterator *pi;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    LIST_LOCK(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    for (pi=&i->list->iNext; *pi; pi=&(*pi)->iNext) {
        assert((*pi)->magic == LIST_MAGIC);
        if (*pi == i) {
            *pi = (*pi)->iNext;
            break;
        }
    }
    LIST_UNLOCK(&i->list->mutex);
    i->magic = 0;
    free(i);
    return;
}


void * list_next(ListIterator i)
{
    ListNode p;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    LIST_LOCK(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if ((p = i->pos))
        i->pos = p->next;
    if (*i->prev != p)
        i->prev = &(*i->prev)->next;
    LIST_UNLOCK(&i->list->mutex);
    return(p ? p->data : NULL);
}


void * list_insert(ListIterator i, void *x)
{
    assert(i);
    assert(x);
    assert(i->magic == LIST_MAGIC);
    LIST_LOCK(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if (!list_node_create(i->list, i->prev, x))
        x = NULL;
    LIST_UNLOCK(&i->list->mutex);
    return(x);
}


void * list_find(ListIterator i, ListFindF f, void *key)
{
    void *v;

    assert(i);
    assert(f);
    assert(key);
    assert(i->magic == LIST_MAGIC);
    while ((v=list_next(i)) && !f(v,key)) {;}
    return(v);
}


void * list_remove(ListIterator i)
{
    void *v = NULL;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    LIST_LOCK(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if (*i->prev != i->pos)
        v = list_node_destroy(i->list, i->prev);
    LIST_UNLOCK(&i->list->mutex);
    return(v);
}


int list_delete(ListIterator i)
{
    void *v;

    assert(i);
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

    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(pp);
    assert(x);
    if (!(p = (ListNode) malloc(sizeof(struct listNode))))
        return(NULL);
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

    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(pp);
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
    free(p);
    return(v);
}
