/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
         ------------------------------------------------------------
        / pl - poll list, a very minimal linked list implementation  \
        \ that holds list of poll for the server                     /
         ------------------------------------------------------------
          \
           \ ,   _ ___.--'''`--''//-,-_--_.
              \`"' ` || \\ \ \\/ / // / ,-\\`,_
             /'`  \ \ || Y  | \|/ / // / - |__ `-,
            /@"\  ` \ `\ |  | ||/ // | \/  \  `-._`-,_.,
           /  _.-. `.-\,___/\ _/|_/_\_\/|_/ |     `-._._)
           `-'``/  /  |  // \__/\__  /  \__/ \
                `-'  /-\/  | -|   \__ \   |-' |
                  __/\ / _/ \/ __,-'   ) ,' _|'
                 (((__/(((_.' ((___..-'((__,'
   ==========================================================================
          _               __            __         ____ _  __
         (_)____   _____ / /__  __ ____/ /___     / __/(_)/ /___   _____
        / // __ \ / ___// // / / // __  // _ \   / /_ / // // _ \ / ___/
       / // / / // /__ / // /_/ // /_/ //  __/  / __// // //  __/(__  )
      /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/  /_/  /_//_/ \___//____/

   ========================================================================== */


#include "poll-list.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "valid.h"


/* ==========================================================================
                  _                __           ____
    ____   _____ (_)_   __ ____ _ / /_ ___     / __/__  __ ____   _____ _____
   / __ \ / ___// /| | / // __ `// __// _ \   / /_ / / / // __ \ / ___// ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / __// /_/ // / / // /__ (__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/  /_/   \__,_//_/ /_/ \___//____/
/_/
   ========================================================================== */


/* ==========================================================================
    Finds a node that contains 'data'

    Returns pointer to found node or null.

    If 'prev' is not null, function will also return previous node that
    'next' field points to returned node. This is usefull when deleting
    nodes and saves searching for previous node to rearange list after
    delete. If function returns valid pointer, and prev is NULL, this
    means first element from the list was returned.
   ========================================================================== */


static struct m2md_pl *m2md_pl_find_node
(
    struct m2md_pl             *head,  /* head of the list to search */
    const struct m2md_pl_data  *data,  /* data to look for */
    struct m2md_pl            **prev   /* previous node to returned node */
)
{
    struct m2md_pl             *node;  /* current node */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (prev != NULL)
    {
        *prev = NULL;
    }

    for (node = head; node != NULL; node = node->next)
    {
        /* check if nodes are equal - take only func, reg and uid
         * fields into consideration
         */
        if (node->data.func == data->func &&
            node->data.reg == data->reg &&
            node->data.uid == data->uid)
        {
            /* this is the node you are looking for
             */

            return node;
        }

        if (prev != NULL)
        {
            *prev = node;
        }
    }

    /* node of that data does not exist
     */

    return NULL;
}


/* ==========================================================================
    Creates new node with copy of 'data'

    Returns NULL on error or address on success

    errno:
            ENOMEM      not enough memory for new node
   ========================================================================== */


static struct m2md_pl *m2md_pl_new_node
(
    const struct m2md_pl_data  *data   /* data to create new node with */
)
{
    struct m2md_pl             *node;  /* pointer to new node */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /* allocate enough memory for data and node in one malloc(),
     */

    node = malloc(sizeof(struct m2md_pl));
    if (node == NULL)
    {
        return NULL;
    }

    /* make a copy of data
     */

    memcpy(&node->data, data, sizeof(*data));

    /* since this is new node, it doesn't point to anything
     */

    node->next = NULL;

    return node;
}


/* ==========================================================================
                       __     __ _          ____
        ____   __  __ / /_   / /(_)_____   / __/__  __ ____   _____ _____
       / __ \ / / / // __ \ / // // ___/  / /_ / / / // __ \ / ___// ___/
      / /_/ // /_/ // /_/ // // // /__   / __// /_/ // / / // /__ (__  )
     / .___/ \__,_//_.___//_//_/ \___/  /_/   \__,_//_/ /_/ \___//____/
    /_/
   ========================================================================== */


/* ==========================================================================
    Adds new node with 'data' to list pointed by 'head'

    Function will add node just after head, not at the end of list - this is
    so we can gain some speed by not searching for last node. So when 'head'
    list is

        +---+     +---+
        | 1 | --> | 2 |
        +---+     +---+

    And node '3' is added, list will be

        +---+     +---+     +---+
        | 1 | --> | 3 | --> | 2 |
        +---+     +---+     +---+

    If 'head' is NULL (meaning list is empty), function will create new list
    and add 'data' node to 'head'

    Function will first check if same node exists - if not just add new node
    if yes, it will check if poll time is smaller and if it's indeed smaller
    poll time will be updated to smaller value.
   ========================================================================== */


int m2md_pl_add
(
    struct m2md_pl            **head,  /* head of list where to add new node */
    const struct m2md_pl_data  *data   /* data for new node */
)
{
    struct m2md_pl             *node;  /* newly created node */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALID(EINVAL, head);
    VALID(EINVAL, data);

    /* let's check if such node already exist
     */

    if ((node = m2md_pl_find_node(*head, data, NULL)) != NULL)
    {
        /* oh look, such node already exist! Check if next poll
         * time is smaller or not
         */

        if (data->poll_time.tv_sec < node->data.poll_time.tv_sec ||
                (data->poll_time.tv_sec == node->data.poll_time.tv_sec &&
                 data->poll_time.tv_nsec < node->data.poll_time.tv_nsec))
        {
            /* it's less! update new - smaller - poll time and
             * exit with success
             */

            node->data.poll_time = data->poll_time;

            /* we also need to reset next read timer, so that new
             * poll data is handled immediately, without that next
             * read could be like 10 minutes in the future, and
             * changing poll time would be in effect only after
             * poll expires (after 10 minutes).
             */

            node->data.next_read.tv_sec = 0;
            node->data.next_read.tv_nsec = 0;
        }

        /* current poll_time is smaller from new one, don't change
         * anything, and just ignore the whole situation - don't
         * even add new node
         */

        return 0;
    }

    /* new poll data are not in the list yet so
     * create new node, let's call it 3
     *
     *           +---+
     *           | 3 |
     *           +---+
     *
     *      +---+     +---+
     *      | 1 | --> | 2 |
     *      +---+     +---+
     */

    node = m2md_pl_new_node(data);
    if (node == NULL)
    {
        return -1;
    }

    if (*head == NULL)
    {
        /* head is null, so list is empty and we are creating
         * new list here. In that case, simply set *head with
         * newly created node and exit
         */

        *head = node;
        return 0;
    }

    /* set new node's next field, to second item in the list,
     * if there is no second item, it will point to NULL
     *
     *           +---+
     *           | 3 |
     *           +---+
     *                 \
     *                 |
     *                 V
     *      +---+     +---+
     *      | 1 | --> | 2 |
     *      +---+     +---+
     */

    node->next = (*head)->next;

    /* set head's next to point to newly created node so our
     * list is complete once again.
     *
     *           +---+
     *           | 3 |
     *           +---+
     *           ^    \
     *           |     |
     *          /      V
     *      +---+     +---+
     *      | 1 |     | 2 |
     *      +---+     +---+
     */

    (*head)->next = node;

    return 0;
}


/* ==========================================================================
    Removes 'data' from list 'head'.

    - if 'data' is in 'head' node, function will modify 'head' pointer
      so 'head' points to proper node

    - if 'data' is in 'head' node and 'head' turns out to be last element
      int the list, 'head' will become NULL
   ========================================================================== */


int m2md_pl_delete
(
    struct m2md_pl            **head,       /* pointer to head of the list */
    const struct m2md_pl_data  *data        /* node with data to telete */
)
{
    struct m2md_pl             *node;       /* found node for with 'data' */
    struct m2md_pl             *prev_node;  /* previous node of found 'node' */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    VALID(EINVAL, head);
    VALID(ENOENT, *head);
    VALID(EINVAL, data);

    node = m2md_pl_find_node(*head, data, &prev_node);
    if (node == NULL)
    {
        /* cannot delete node with name 'data' because such node
         * does not exist
         */

        errno = ENOENT;
        return -1;
    }

    /* initial state of the list
     *
     *            +---+     +---+     +---+
     *   head --> | 1 | --> | 3 | --> | 2 |
     *            +---+     +---+     +---+
     */

    if (node == *head)
    {
        /* caller wants to delete node that is currently head node,
         * so we need to remove head, and them make head->next new
         * head of the list
         *
         *            +---+          +---+     +---+
         *   node --> | 1 | head --> | 3 | --> | 2 |
         *            +---+          +---+     +---+
         */

        *head = node->next;

        /* now '1' is detached from anything and can be safely freed
         */

        free(node);
        return 0;
    }

    /* node points to something else than 'head'
     *
     *            +---+     +---+     +---+     +---+
     *   head --> | 1 | --> | 3 | --> | 2 | --> | 4 |
     *            +---+     +---+     +---+     +---+
     *                                 ^
     *                                 |
     *                                node
     *
     * before deleting, we need to make sure '3' (prev node) points
     * to '4'. If node points to last element '4', then we will set
     * next member of '2' element to null.
     *
     *            +---+     +---+     +---+
     *   head --> | 1 | --> | 3 | --> | 4 |
     *            +---+     +---+     +---+
     *                                 ^
     *                                 |
     *                      +---+     /
     *             node --> | 2 | ---`
     *                      +---+
     */

    prev_node->next = node->next;

    /* now that list is consistent again, we can remove node (2)
     * without destroying list
     */

    free(node);
    return 0;
}


/* ==========================================================================
    Removes all elements in the list pointed by 'head'. After this function
    is called 'head' should no longer be used without calling m2md_pl_new()
    on it again
   ========================================================================== */


int m2md_pl_destroy
(
    struct m2md_pl *head  /* list to destroy */
)
{
    struct m2md_pl *next; /* next node to free() */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    VALID(EINVAL, head);

    for (;head != NULL; head = next)
    {
        next = head->next;
        free(head);
    }

    return 0;
}
