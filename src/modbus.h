/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#ifndef M2MD_MODBUS_H
#define M2MD_MODBUS_H 1

#if HAVE_CONFIG_H
#   include "m2md-config.h"
#endif

#include <arpa/inet.h>
#include <modbus/modbus.h>
#include <time.h>

#include "poll-list.h"

enum m2md_modbus_functions
{
	/* bit access */
	M2MD_MODBUS_FUNC_READ_COIL              = 1,
	M2MD_MODBUS_FUNC_READ_DISCRETE_INPUT    = 2,
	M2MD_MODBUS_FUNC_WRITE_COIL             = 5,
	M2MD_MODBUS_FUNC_WRITE_MULTI_COIL       = 15,

	/* 16bit access */
	M2MD_MODBUS_FUNC_READ_MULTI_HOLD_REG    = 3,
	M2MD_MODBUS_FUNC_READ_INPUT_REG         = 4,
	M2MD_MODBUS_FUNC_WRITE_SINGLE_HOLD_REG  = 6,
	M2MD_MODBUS_FUNC_WRITE_MULTI_HOLD_REG   = 16
};

enum m2md_server_msg_cmd
{
	M2MD_SERVER_MSG_CONNECT,
	M2MD_SERVER_MSG_POLL
};

/* single com message for server thread */
struct m2md_server_msg
{
	int  cmd;
	union
	{
		struct m2md_pl_data poll;
	}
	data;
};

/* struct describing connection to single server */
struct m2md_server
{
	modbus_t         *modbus;  /* libmodbus object */
	struct m2md_pl   *polls;   /* list of register to poll */
	pthread_mutex_t   lock;    /* server access mutex */
	pthread_t         thandle; /* thread handle */
	struct rb        *msgq;    /* one way comm bus with thread */
	int               conn_to; /* time to wait between reconnections */
	int               port;    /* porn on which modbus server listens */
	char              ip[INET_ADDRSTRLEN];  /* ip of the server */
};

int m2md_modbus_init(void);
#if 0
int m2md_modbus_read(struct m2md_modbus *modbus,
        struct m2md_modbus_frame *frame);
int m2md_modbus_write(struct m2md_modbus *modbus,
        const struct m2md_modbus_frame *frame);
#endif
int m2md_modbus_cleanup(void);
struct timespec m2md_modbus_loop(void);
int m2md_modbus_add_poll(struct m2md_pl_data *poll,
		const char *ip, int port);
int m2md_modbus_delete_poll(struct m2md_pl_data *poll,
		const char *ip, int port);

#endif
