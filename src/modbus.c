/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ==========================================================================
          _               __            __         ____ _  __
         (_)____   _____ / /__  __ ____/ /___     / __/(_)/ /___   _____
        / // __ \ / ___// // / / // __  // _ \   / /_ / // // _ \ / ___/
       / // / / // /__ / // /_/ // /_/ //  __/  / __// // //  __/(__  )
      /_//_/ /_/ \___//_/ \__,_/ \__,_/ \___/  /_/  /_//_/ \___//____/

   ========================================================================== */


#include "modbus.h"

#include <embedlog.h>
#include <rb.h>
#include <errno.h>
#include <modbus/modbus.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>

#include "cfg.h"
#include "reg2topic-map.h"
#include "poll-list.h"
#include "mqtt.h"


/* ==========================================================================
          __             __                     __   _
     ____/ /___   _____ / /____ _ _____ ____ _ / /_ (_)____   ____   _____
    / __  // _ \ / ___// // __ `// ___// __ `// __// // __ \ / __ \ / ___/
   / /_/ //  __// /__ / // /_/ // /   / /_/ // /_ / // /_/ // / / /(__  )
   \__,_/ \___/ \___//_/ \__,_//_/    \__,_/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


static struct m2md_server servers[M2MD_SERVERS_MAX];
extern pthread_t g_main_thread_t;


/* ==========================================================================
                  _                __           ____
    ____   _____ (_)_   __ ____ _ / /_ ___     / __/__  __ ____   _____ _____
   / __ \ / ___// /| | / // __ `// __// _ \   / /_ / / / // __ \ / ___// ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / __// /_/ // / / // /__ (__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/  /_/   \__,_//_/ /_/ \___//____/
/_/
   ========================================================================== */


/* ==========================================================================
    Calculates diff between t1 - t2
   ========================================================================== */


static struct timespec m2md_modbus_subtract_timespec
(
    struct timespec   t1,   /* time to subtract from */
    struct timespec   t2    /* time to subtract */
)
{
    struct timespec   res;  /* subtract result */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    res.tv_sec = t1.tv_sec - t2.tv_sec;
    res.tv_nsec = t1.tv_nsec - t2.tv_nsec;

    if (res.tv_nsec < 0)
    {
        res.tv_sec -= 1;
        res.tv_nsec += 1000000000l;
    }

    return res;
}


/* ==========================================================================
    Finds server in 'servers' object with specified 'ip' and 'port'. Returns
    index to found server or -1 when modbus server doesn't exist with given
    'ip' and/or 'port'.
   ========================================================================== */


static int m2md_modbus_server_find
(
    const char  *ip,   /* ip address of server */
    int          port  /* port server listens on */
)
{
    int          i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    for (i = 0; i != M2MD_SERVERS_MAX; ++i)
    {
        if (servers[i].modbus == NULL)
        {
            /* empty slot, nothing to look for here
             */

            continue;
        }

        if (strcmp(servers[i].ip, ip) == 0 && servers[i].port == port)
        {
            /* that's the one!
             */

            return i;
        }
    }

    /* no servers with given ip and port in the array
     */

    return -1;
}


/* ==========================================================================
    Finds free slot in server array.
   ========================================================================== */


static int m2md_modbus_server_find_free
(
    void
)
{
    int   i;      /* iterator */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    for (i = 0; i != M2MD_SERVERS_MAX; ++i)
    {
        if (servers[i].modbus == NULL)
        {
            /* this slot is empty
             */

            return i;
        }
    }

    /* we iterated through all servers and none of them are free
     */

    return -1;
}


/* ==========================================================================
    Function clears out modbus'es thread queue and writes to it request to
    connect. It is possible that between rb_clear() and rb_write() buffer
    gets refiled again by other threads, so we do this until we sucessfully
    put message on queue.

    Also function will return -1 when errno is ECANCELED meaning we shall
    not use rb_* functions no more.
   ========================================================================== */


static int m2md_modbus_server_reconnect
(
    struct rb              *msgq  /* modbus communication queue */
)
{
    struct m2md_server_msg  msg;  /* message with connect command */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    msg.cmd = M2MD_SERVER_MSG_CONNECT;
    for (;;)
    {
        rb_clear(msgq, 0);
        if (rb_write(msgq, &msg, 1) == 1)
        {
            return 0;
        }

        if (errno == ECANCELED)
        {
            return -1;
        }

        /* Now talk about luck! Some other thrads managed
         * to fill whole buffer in time after we clear
         * buffer and put our message in!  We need to log
         * this unusual situation, as I am wondering if
         * this will ever happen
         */

        el_print(ELF, "interesting, look me up in the code!");
    }
}


/* ==========================================================================
    Thread handling single server connection.
   ========================================================================== */


static void *m2md_modbus_server_thread
(
    void                   *arg
)
{
    struct m2md_server     *server = arg;
    struct m2md_server_msg  msg;
    int                     ret;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    for (;;)
    {
        /* wait for command to arrive
         */

        if (rb_read(server->msgq, &msg, 1) != 1)
        {
            if (errno == ECANCELED)
            {
                /* someone called rb_stop() on us, we should
                 * stop using msgq at once!
                 */

                goto end_of_the_road;
            }

            /* should not happen, but world is a strange place
             */

            el_perror(ELW, "rb_read()");
            continue;
        }

        /* message received, what does it say?
         */

        switch (msg.cmd)
        {

        /* hmm, connection initialization, huh? Right on that
         */

        case M2MD_SERVER_MSG_CONNECT:

            /* in case someone wants to reconnect while still being
             * connected
             */

            modbus_close(server->modbus);

            if (modbus_connect(server->modbus) == 0)
            {
                /* connection was a success, open the
                 * champagne!
                 */

                el_print(ELN, "connected to modbus server %s:%d",
                        server->ip, server->port);
                continue;
            }

            /* we failed to connect, client could be dead, sleep
             * for some time before reconnecting
             */

            el_print(ELW, "modbus_connect(%s:%d) failed: %s, "
                    "reconnecting in %d seconds", server->ip,
                    server->port, modbus_strerror(errno), server->conn_to);

            sleep(server->conn_to);

            /* next sleep will be two times longer (if we still
             * cannot connect) but don't sleep longer than
             * configured time, like ever.
             */

            server->conn_to *= 2;
            if (server->conn_to > m2md_cfg->modbus_max_re_time)
            {
                server->conn_to = m2md_cfg->modbus_max_re_time;
            }

            /* send connect command back to ourselfs, so we try to
             * reconnect to the server. Before sending message
             * clear communication buffer, it's not like any of
             * them will succeed without active connection.
             */

            if (m2md_modbus_server_reconnect(server->msgq) != 0)
            {
                goto end_of_the_road;
            }

            break;


        /* we are suppose to poll for data and publish it on
         * mqtt bus
         */

        case M2MD_SERVER_MSG_POLL:
        {
            uint16_t  rval;   /* read value from modbus */
            float     data;   /* data to send over mqtt */
            int       ret;    /* return code from functions */
            int       tl;     /* topic length - from snprintf */
            int       regind; /* register position in reg2topic map */
            char      topic[M2MD_TOPIC_MAX + 1];  /* topic to publish */
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


            /* set unit id
             */

            if (modbus_set_slave(server->modbus, msg.data.poll.uid) != 0)
            {
                el_print(ELW, "poll: invalid unit id set: %d",
                        msg.data.poll.uid);
                continue;
            }

            /* what function should we use to read bits?
             */

            switch (msg.data.poll.func)
            {
            case M2MD_MODBUS_FUNC_READ_INPUT_REG:
                ret = modbus_read_input_registers(server->modbus,
                        msg.data.poll.reg, 1, &rval) != 1;
                break;

            case M2MD_MODBUS_FUNC_READ_MULTI_HOLD_REG:
                ret = modbus_read_registers(server->modbus,
                        msg.data.poll.reg, 1, &rval) != 1;
                break;

            default:
                el_print(ELW, "poll: invalid modbus function passed %d",
                        msg.data.poll.func);
                continue;
            }

            /* message sent, but was it successfull?
             */

            if (ret != 0)
            {
                /* sadly not, problems with sending and receiving
                 * data over modbustcp is usually due to connection
                 * problem.  It may not be, but meh, who care
                 * really. To be sure we close connection and try
                 * to reconnect to the server.
                 */

                el_print(ELE, "poll: modbus_* function %d %s",
                        msg.data.poll.func, modbus_strerror(errno));
                modbus_close(server->modbus);

                if (m2md_modbus_server_reconnect(server->msgq) != 0)
                {
                    goto end_of_the_road;
                }

                continue;
            }

            /* message received, now transform it into mqtt
             */

            regind = m2md_reg2topic_find(server->mfr, msg.data.poll.reg);
            if (regind < 0)
            {
                el_print(ELE, "poll: m2md_reg2topic_find(%d, %d) "
                        "someone is facing an update session!",
                        server->mfr, msg.data.poll.reg);
                continue;
            }

            /* construct topic on which send data
             */

            tl = snprintf(topic, sizeof(topic), "/%s:%d/%s",
                    server->ip, server->port,
                    m2md_reg2topic_map[server->mfr].elements[regind].topic);

            if (tl > sizeof(topic))
            {
                el_print(ELE, "poll: error creating topic for reg %d, mfr %d "
                        "managed to create only this %s",
                        server->mfr, msg.data.poll.reg, topic);
                continue;
            }

            /* prepare data to send, received data is just imaginary value
             * without unit, we apply scale factor to convert value to
             * known unit.
             */

            data = rval *
                m2md_reg2topic_map[server->mfr].elements[regind].scale;

            /* we are ready to publish message, so what are you
             * waiting for? hit em with it!
             */

            if (m2md_mqtt_publish(topic, &data, sizeof(data)) != 0)
            {
                el_perror(ELE, "poll: mqtt_publish(%s, %ld) failed",
                        topic, (long)sizeof(data));
            }
        }
        }
    }

end_of_the_road:
    modbus_close(server->modbus);
    modbus_free(server->modbus);

modbus_tcp_new_error:
    m2md_pl_destroy(server->polls);
    rb_destroy(server->msgq);

    /* this will use as flag to indicate if thread is still running
     * or not
     */

    server->modbus = NULL;
    return NULL;
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
    Init modbus module.
   ========================================================================== */


int m2md_modbus_init
(
    void
)
{
    int   i;  /* teh iterator */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /* just nullify 'modbus' field, to indicate all slots are free
     * after initialization
     */

    for (i = 0; i != M2MD_SERVERS_MAX; ++i)
    {
        servers[i].modbus = NULL;
    }

    return 0;
}


/* ==========================================================================
    Adds specified 'poll' for 'server' and 'port'. If this is first request
    for the server, function will start thread and connect to that server.
   ========================================================================== */


int m2md_modbus_add_poll
(
    struct m2md_pl_data      *poll,    /* register to poll */
    const char               *ip,      /* ip of server to poll */
    int                       port,    /* modbus port on the server */
    int                       mfr      /* manufacture of the server */
)
{
    struct m2md_server_msg    msg;     /* message to send to server thread */
    struct m2md_server       *server;  /* modbus server description */
    int                       sid;     /* existing server index */
    int                       ret;     /* return code for some functions */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /* use ntohl function to parse and check if passed ip address
     * is actually ip address
     */

    if (ntohl(inet_addr(ip)) == INADDR_ANY)
    {
        el_print(ELW, "poll/add: wrong server address %s", ip);
        return -1;
    }

    if ((sid = m2md_modbus_server_find(ip, port)) >= 0)
    {
        /* Server already exist, just push add request to server.
         */

        server = servers + sid;
        pthread_mutex_lock(&server->lock);
        if (m2md_pl_add(&server->polls, poll) != 0)
        {
            /* "Noooo i w pizdu... i cały misterny plan też w pizdu"
             *      ~Siara
             *
             *  This will happen when memory is exhausted in the
             *  system, we don't remove client, memory may be freed and
             *  we will continue then
             */

            pthread_mutex_unlock(&server->lock);
            el_perror(ELE, "poll/add: m2md_pl_add()");
            return -1;
        }

        pthread_mutex_unlock(&server->lock);

        /* new poll has been added, send signal to main thread so
         * it exits sleep and process new signal. It is crucial as
         * main might be sleeping for... let's say 10 minuts, and
         * new poll requires polling once every 1 second. Without
         * the signal it would take 10 minutes to start sending new
         * poll once a second. Not an ideal situation, is it?
         */

        pthread_kill(g_main_thread_t, SIGUSR1);
        el_print(ELN, "poll/add finished: host: %s:%d, mfr: %d, "
                "reg: %d, uid: %d, func: %d, poll_s: %ld, poll_ms: %d",
                ip, port, mfr, poll->reg, poll->uid, poll->func,
                poll->poll_time.tv_sec, poll->poll_time.tv_nsec / 1000000);

        /* publish ack on mqtt bus, maybe someone is listening
         */

        m2md_mqtt_publish_add_ack(poll, ip, port);
        return 0;
    }

    /* First request for that server, create new thread with
     * server connection and let that thread take it from here.
     */

    if ((sid = m2md_modbus_server_find_free()) < 0)
    {
        el_print(ELW, "poll/add: no free server slots");
        return -1;
    }

    /* initialize modbus context, we only have to do this once
     */

    server = servers + sid;
    strcpy(server->ip, ip);

    server->conn_to = 1;
    server->mfr = mfr;
    server->port = port;
    server->modbus = modbus_new_tcp(ip, port);
    if (server->modbus == NULL)
    {
        el_perror(ELE, "poll/add: modbus_tcp_new(%s, %d)", ip, port);
        goto modbus_tcp_new_error;
    }

    /* Create queue on which server will receive commands from main
     * thread.
     */

    if ((server->msgq = rb_new(16, sizeof(msg), O_MULTITHREAD)) == NULL)
    {
        el_perror(ELE, "poll/add: rb_new()");
        goto rb_new_error;
    }

    ret = pthread_mutex_init(&server->lock, NULL);

    if (ret)
    {
        errno = ret;
        el_perror(ELE, "poll/add: pthread_mutex_init()");
        goto pthread_mutex_init_error;
    }

    ret = pthread_create(&server->thandle, NULL,
            m2md_modbus_server_thread, server);

    if (ret)
    {
        errno = ret;
        el_perror(ELE, "poll/add: pthread_create()");
        goto pthread_create_error;
        return -1;
    }

    /* New thread started, send connect command so thread starts
     * connecting to modbus server. All data needed to make connection
     * is already in modbus variable.
     */

    msg.cmd = M2MD_SERVER_MSG_CONNECT;
    rb_write(server->msgq, &msg, 1);

    /* Server will be connecting in background, and we can add
     * poll to the list of polls for that server
     */

    pthread_mutex_lock(&server->lock);
    if (m2md_pl_add(&server->polls, poll) != 0)
    {
        /* "Noooo i w pizdu... i cały misterny plan też w pizdu"
         *      ~Siara
         *
         *  This will happen when memory is exhausted in the
         *  system, we don't remove client, memory may be freed and
         *  we will continue then
         */

        pthread_mutex_unlock(&server->lock);
        el_perror(ELE, "poll/add: m2md_pl_add()");
        return -1;
    }

    pthread_mutex_unlock(&server->lock);
    pthread_kill(g_main_thread_t, SIGUSR1);
    el_print(ELN, "poll/add finished: host: %s:%d, mfr: %d, "
            "reg: %d, uid: %d, func: %d, poll_s: %ld, poll_ms: %d",
            ip, port, mfr, poll->reg, poll->uid, poll->func,
            poll->poll_time.tv_sec, poll->poll_time.tv_nsec / 1000000);
    m2md_mqtt_publish_add_ack(poll, ip, port);
    return 0;

pthread_create_error:
    pthread_mutex_destroy(&server->lock);

pthread_mutex_init_error:
    rb_destroy(server->msgq);

rb_new_error:
    modbus_free(server->modbus);

modbus_tcp_new_error:
    server->modbus = NULL;
    return -1;
}


/* ==========================================================================
    Removes specified 'poll' from ip:port server.
   ========================================================================== */


int m2md_modbus_delete_poll
(
    struct m2md_pl_data  *poll,    /* register to poll */
    const char           *ip,      /* ip of server to poll */
    int                   port     /* modbus port on the server */
)
{
    int                   sid;     /* server index */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    /* use ntohl function to parse and check if passed ip address
     * is actually ip address
     */

    if (ntohl(inet_addr(ip)) == INADDR_ANY)
    {
        el_print(ELW, "poll/delete: wrong server address %s", ip);
        return -1;
    }

    if ((sid = m2md_modbus_server_find(ip, port)) < 0)
    {
        /* ip:port server doesn't exist, so nothing to delete
         */

        el_print(ELW, "poll/delete: specified server %s:%d does not exist",
                ip, port);
        return -1;
    }

    pthread_mutex_lock(&servers[sid].lock);
    if (m2md_pl_delete(&servers[sid].polls, poll) != 0)
    {
        /* failed to delete, specified poll doesn't exist
         * can't delete what doesn't exist
         */

        pthread_mutex_unlock(&servers[sid].lock);
        el_print(ELW, "poll/delete: func: %d, reg: %d, uid: %d doesn't exist "
                "in server %s:%d", poll->func, poll->reg, poll->uid, ip, port);
        return -1;
    }

    pthread_mutex_unlock(&servers[sid].lock);
    el_print(ELN, "poll/delete finished: host: %s:%d, func: %d, reg: %d, "
            "uid: %d", ip, port, poll->func, poll->reg, poll->uid);
    m2md_mqtt_publish_delete_ack(poll, ip, port);
    return 0;
}


/* ==========================================================================
    Loops through all servers and poll lists and checks if any poll timeout
    has occured, if so it trigger read for that register from that server.

    Function will return time when nearest poll should occur.
   ========================================================================== */


struct timespec m2md_modbus_loop
(
    void
)
{
    struct m2md_server       *server;        /* current server being probed */
    struct timespec           next_poll;     /* time left to next poll */
    struct timespec           parse_finish;  /* time taken parsing all polls */
    struct timespec           now;           /* current absolute time */
    struct m2md_pl           *poll;          /* current poll information */
    int                       i;             /* iterator */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    clock_gettime(CLOCK_MONOTONIC, &now);

    /* set next_poll to invalid value, as an indicator that
     * next_poll was modified or not.
     */

    next_poll.tv_nsec = LONG_MAX;

    /* for each active server
     */

    for (i = 0; i != M2MD_SERVERS_MAX; ++i)
    {
        server = servers + i;
        if (server->modbus == NULL)
        {
            /* that slot is not active
             */

            continue;
        }

        /* lock mutex - noone messes with our poll list while we are
         * messing with them!
         */

        pthread_mutex_lock(&server->lock);

        for (poll = server->polls; poll != NULL; poll = poll->next)
        {
            int                     expired;  /* did poll expire or not */
            struct m2md_server_msg  msg;      /* msg to send to serv thread */
            /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


            /* did timer for that poll expire?
             */

            expired = 0;
            if ((now.tv_sec > poll->data.next_read.tv_sec) ||
                    (now.tv_sec == poll->data.next_read.tv_sec &&
                     now.tv_nsec >= poll->data.next_read.tv_nsec))
            {
                expired = 1;
            }

            if (expired == 0)
            {
                /* no, it's not yet time to make a poll, just update
                 * next_poll timer if it is smaller than current
                 * next_poll
                 */

                if (next_poll.tv_nsec == LONG_MAX ||
                    poll->data.next_read.tv_sec < next_poll.tv_sec ||
                        (poll->data.next_read.tv_sec == next_poll.tv_sec &&
                         poll->data.next_read.tv_nsec < next_poll.tv_nsec))
                {
                    next_poll.tv_sec = poll->data.next_read.tv_sec;
                    next_poll.tv_nsec = poll->data.next_read.tv_nsec;
                }

                continue;
            }

            /* yes, that poll has expired, send request to server's
             * thread to read register
             */

            msg.cmd = M2MD_SERVER_MSG_POLL;
            msg.data.poll.func = poll->data.func;
            msg.data.poll.reg = poll->data.reg;
            msg.data.poll.uid = poll->data.uid;

            if (rb_send(server->msgq, &msg, 1, MSG_DONTWAIT) != 1)
            {
                /* sending poll request failed, could be that
                 * server dies and message queue is full, can't do
                 * anything about that, and surely we won't be
                 * waiting for situation to resolve itself, log
                 * situation and move on like nothing had happened
                 */

                el_perror(ELW, "rb_send()");
            }

            /* update poll's timer for next poll
             */

            poll->data.next_read.tv_sec = now.tv_sec +
                poll->data.poll_time.tv_sec;
            poll->data.next_read.tv_nsec = now.tv_nsec +
                poll->data.poll_time.tv_nsec;

            /* it is still possible that this poll has smallest
             * time, so we need to update next poll timer if
             * that is the case
             */

            if (next_poll.tv_nsec == LONG_MAX ||
                poll->data.next_read.tv_sec < next_poll.tv_sec ||
                    (poll->data.next_read.tv_sec == next_poll.tv_sec &&
                     poll->data.next_read.tv_nsec < next_poll.tv_nsec))
            {
                /* see? told you it could be smaller!
                 */

                next_poll.tv_sec = poll->data.next_read.tv_sec;
                next_poll.tv_nsec = poll->data.next_read.tv_nsec;
            }
        }

        /* all polls in that server are served, unlock mutex and
         * move to next server
         */

        pthread_mutex_unlock(&server->lock);
    }

    if (next_poll.tv_nsec == LONG_MAX)
    {
        /* next_poll was not modified, either there are no server
         * and/or poll, sleep for as long as we can, since when
         * request for new poll comes in, sleep will be interrupted
         * and this function triggered again - no need to call
         * this function when no polls are installed yet.
         *
         * time_t will never be smaller than int, no really, it's
         * no use. So it's the safest bigest value to sleep for,
         * it's enough.
         */

        next_poll.tv_sec = INT_MAX;
        next_poll.tv_nsec = 0;
        return next_poll;
    }

    /* processsing all have taken some time, so we need to update
     * next_poll with the time we spent here processing data, it is
     * possible that next time will have to be done immediately
     */

    clock_gettime(CLOCK_MONOTONIC, &parse_finish);

    /* subtract parse_finish from now (now as in time of entering
     * function) to get time spent on parsing
     */

    parse_finish = m2md_modbus_subtract_timespec(parse_finish, now);

    /* now subtract time spending on parsing from next poll time
     * this will give us time when next poll shall be made
     */

    next_poll = m2md_modbus_subtract_timespec(next_poll, parse_finish);

    /* now we just need to calculate how many seconds there are
     * until poll shall be made - now we only have info when poll
     * shall be made
     */

    clock_gettime(CLOCK_MONOTONIC, &now);
    next_poll = m2md_modbus_subtract_timespec(next_poll, now);

    if (next_poll.tv_sec < 0)
    {
        /* looks like we are behind schedule and should have
         * already polled data, set next_poll to 0 so upper layer
         * know it should poll again immediately
         */

        next_poll.tv_sec = 0;
        next_poll.tv_nsec = 0;
    }

    return next_poll;
}


/* ==========================================================================
    Stop all thrads and free resources allocated by adding polls operations
   ========================================================================== */


int m2md_modbus_cleanup
(
    void
)
{
    /* TODO: close all connections, free all resources, quite a lot
     * of work ahead
     */

    return 0;
}
