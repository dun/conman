/******************************************************************************\
 *  list.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: list.c,v 1.3 2001/05/09 16:00:05 dun Exp $
 ******************************************************************************
 *  Refer to "list.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "list.h"


#define LIST_MAGIC 0xCD


struct listNode {
    void		*data;		/* node's data */
    struct listNode	*next;		/* next node in list */
};

struct listIterator {
    struct list		*list;		/* the list being iterated */
    struct listNode	*pos;		/* the next node to be iterated */
    struct listNode	**prev;		/* addr of 'next' ptr to prev It node */
    struct listIterator	*iNext;		/* iterator chain for list_destroy() */
    unsigned char	magic;		/* sentinel for asserting validity */
};

struct list {
    struct listNode	*head;		/* head of the list */
    struct listNode	**tail;		/* addr of last node's 'next' ptr */
    struct listIterator *iNext;		/* iterator chain for list_destroy() */
    ListDelF		fDel;		/* function to delete node data */
    int			count;		/* number of nodes in list */
    unsigned char	magic;		/* sentinel for asserting validity */
};

typedef struct listNode * ListNode;


static void * list_node_create(List l, ListNode *pp, void *x);
static void * list_node_destroy(List l, ListNode *pp);


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
    l->magic = LIST_MAGIC;
    return(l);
}


void list_destroy(List l)
{
    ListIterator i, iTmp;
    ListNode p, pTmp;

    assert(l);
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
    free(l);
    return;
}


int list_is_empty(List l)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    return(l->count == 0);
}


int list_count(List l)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    return(l->count);
}


void * list_append(List l, void *x)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(x);
    return(list_node_create(l, l->tail, x));
}


void * list_prepend(List l, void *x)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(x);
    return(list_node_create(l, &l->head, x));
}


void * list_find_first(List l, ListFindF f, void *key)
{
    ListNode p;

    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(f);
    assert(key);
    for (p=l->head; p; p=p->next) {
        if (f(p->data, key))
            return(p->data);
    }
    return(NULL);
}


int list_delete_all(List l, ListFindF f, void *key)
{
    ListNode *pp;
    void *x;
    int cnt = 0;

    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(f);
    assert(key);
    pp = &l->head;
    while (*pp) {
        if (f((*pp)->data, key)) {
            if ((x = list_node_destroy(l, pp))) {
                if (l->fDel)
                    l->fDel(x);
                cnt++;
            }
        }
        else {
            pp = &(*pp)->next;
        }
    }
    return(cnt);
}


void list_sort(List l, ListCmpF f)
{
/*  Note: Time complexity O(n^2).
 */
    ListNode *pp, *ppPrev, *ppPos, pTmp;

    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(f);
    ppPrev = &l->head;
    if (!*ppPrev)
        return;
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
    return;
}


void * list_push(List l, void *x)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(x);
    return(list_node_create(l, &l->head, x));
}


void * list_pop(List l)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    return(list_node_destroy(l, &l->head));
}


void * list_enqueue(List l, void *x)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(x);
    return(list_node_create(l, l->tail, x));
}


void * list_dequeue(List l)
{
    assert(l);
    assert(l->magic == LIST_MAGIC);
    return(list_node_destroy(l, &l->head));
}


ListIterator list_iterator_create(List l)
{
    ListIterator i;

    assert(l);
    assert(l->magic == LIST_MAGIC);
    if (!(i = (ListIterator) malloc(sizeof(struct listIterator))))
        return(NULL);
    i->list = l;
    i->pos = l->head;
    i->prev = &l->head;
    i->iNext = l->iNext;
    l->iNext = i;
    i->magic = LIST_MAGIC;
    return(i);
}


void list_iterator_reset(ListIterator i)
{
    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    i->pos = i->list->head;
    i->prev = &i->list->head;
    return;
}


void list_iterator_destroy(ListIterator i)
{
    ListIterator *pi;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    for (pi=&i->list->iNext; *pi; pi=&(*pi)->iNext) {
        assert((*pi)->magic == LIST_MAGIC);
        if (*pi == i) {
            *pi = (*pi)->iNext;
            break;
        }
    }
    i->magic = 0;
    free(i);
    return;
}


void * list_next(ListIterator i)
{
    ListNode p;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    p = i->pos;
    if (*i->prev != p)
        i->prev = &(*i->prev)->next;
    if (!p)
        return(NULL);
    i->pos = p->next;
    return(p->data);
}


void * list_insert(ListIterator i, void *x)
{
    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    assert(x);
    if (list_node_create(i->list, i->prev, x))
        return(x);
    return(NULL);
}


void * list_find(ListIterator i, ListFindF f, void *key)
{
    void *x;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    assert(f);
    assert(key);
    while ((x=list_next(i)) && !f(x,key)) {;}
    return(x);
}


void * list_remove(ListIterator i)
{
    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    return((*i->prev != i->pos) ? list_node_destroy(i->list, i->prev) : NULL);
}


int list_delete(ListIterator i)
{
    void *x;

    assert(i);
    assert(i->magic == LIST_MAGIC);
    assert(i->list->magic == LIST_MAGIC);
    if ((x = list_remove(i))) {
        if (i->list->fDel)
            i->list->fDel(x);
        return(1);
    }
    return(0);
}


static void * list_node_create(List l, ListNode *pp, void *x)
{
/*  Inserts data pointed to by (x) into list (l) after (pp),
 *    the address of the previous node's "next" ptr.
 *  Returns a ptr to data (x), or NULL if insertion fails.
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
 */
    void *x;
    ListNode p;
    ListIterator i;

    assert(l);
    assert(l->magic == LIST_MAGIC);
    assert(pp);
    if (!(p = *pp))
        return(NULL);
    x = p->data;
    if (!(*pp = p->next))
        l->tail = pp;
    l->count--;
    for (i=l->iNext; i; i=i->iNext) {
        if (i->pos == p)
            i->pos = p->next, i->prev = pp;
        else if (i->prev == &p->next)
            i->prev = pp;
        assert((i->pos == *i->prev) || (i->pos == (*i->prev)->next));
    }
    free(p);
    return(x);
}
