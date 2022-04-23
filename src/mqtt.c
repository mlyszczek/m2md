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


#if HAVE_CONFIG_H
#   include "m2md-config.h"
#endif

#include <arpa/inet.h>
#include <embedlog.h>
#include <errno.h>
#include <mosquitto.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cfg.h"
#include "modbus.h"
#include "poll-list.h"
#include "valid.h"
#include "macros.h"


/* ==========================================================================
          __             __                     __   _
     ____/ /___   _____ / /____ _ _____ ____ _ / /_ (_)____   ____   _____
    / __  // _ \ / ___// // __ `// ___// __ `// __// // __ \ / __ \ / ___/
   / /_/ //  __// /__ / // /_/ // /   / /_/ // /_ / // /_/ // / / /(__  )
   \__,_/ \___/ \___//_/ \__,_//_/    \__,_/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


#define M2MD_ON_MESSAGE_CLBK(f) static void f(struct mosquitto *mqtt, \
        void *userdata, const struct mosquitto_message *msg)

#if 0
M2MD_ON_MESSAGE_CLBK(m2md_mqtt_poll_add);
M2MD_ON_MESSAGE_CLBK(m2md_mqtt_poll_delete);
#endif

#ifndef M2MD_NO_SIGNALS
extern volatile int g_m2md_run;
#endif
static struct mosquitto *mqtt;

struct m2md_mqtt_sub
{
	const char  *topic;
	void (*on_message)(struct mosquitto *mqtt, void *userdata,
			const struct mosquitto_message *msg);
}
g_m2md_mqtt_subs[] =
{
#if 0
	{ "/ctl/poll/add",     m2md_mqtt_poll_add,   },
	{ "/ctl/poll/delete",  m2md_mqtt_poll_delete }
#endif
};

/* check m2md_mqtt_create_ack_msg() for frame details */
#define M2MD_ACK_FRAME_SIZE (28)
#define m2md_array_size(a) (sizeof(a)/sizeof(*(a)))


/* ==========================================================================
                  _                __           ____
    ____   _____ (_)_   __ ____ _ / /_ ___     / __/__  __ ____   _____ _____
   / __ \ / ___// /| | / // __ `// __// _ \   / /_ / / / // __ \ / ___// ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / __// /_/ // / / // /__ (__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/  /_/   \__,_//_/ /_/ \___//____/
/_/
   ========================================================================== */

/* TODO: dynamic polls need to be revisted, right now data is read
 * statically from file cause it's easier and faster to implement
 * but i t would be nice to have it done dynamically */

#if 0

/* ==========================================================================
    Creates message used for /ack, make sure buf is big enough as it is not
    checked here.

    message format, multibyte data is big-endian ordered

    char[16]       server_ip (padded with zeroes, with null terminator)
    uint16_t       server_port
    uint16_t       register to poll
    uint8_t        unit id
    uint8_t        function code to use to read register
    uint32_t       how often poll register? (seconds part)
    uint16_t       how often poll register? (milliseconds part)

    so size is: 16+2+2+1+1+4+2 = 28bytes
   ========================================================================== */
static int m2md_mqtt_create_ack_msg
(
	unsigned char            *buf,      /* place where frame will be stored */
	struct m2md_pl_data      *pdata,    /* poll data */
	const char               *ip,       /* server ip address */
	int                       portt     /* server port */
)
{
	uint16_t                  port;     /* port on which server listen */
	uint16_t                  reg;      /* register to poll */
	uint8_t                   uid;      /* unit id to read message from*/
	uint8_t                   func;     /* function to use to read reg */
	uint32_t                  poll_s;   /* poll time, seconds part */
	uint16_t                  poll_ms;  /* poll time, millisecond part */

	#define IP_OFFSET         (0)
	#define PORT_OFFSET       (IP_OFFSET + sizeof(ip))
	#define REG_OFFSET        (PORT_OFFSET + sizeof(port))
	#define UID_OFFSET        (REG_OFFSET + sizeof(reg))
	#define FUNC_OFFSET       (UID_OFFSET + sizeof(uid))
	#define POLL_SEC_OFFSET   (FUNC_OFFSET + sizeof(func))
	#define POLL_MSEC_OFFSET  (POLL_SEC_OFFSET + sizeof(poll_s))
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* copy data to designeted types */
	port = portt;
	reg = pdata->reg;
	uid = pdata->uid;
	func = pdata->func;
	poll_s = pdata->poll_time.tv_sec;
	poll_ms = pdata->poll_time.tv_nsec / 1000000000l;

	/* data should be sent in network (big) endianess */
	port = htons(port);
	reg = htons(reg);
	poll_s = htonl(poll_s);
	poll_ms = htons(poll_ms);

	/* copy fields to buffer */
	memcpy(buf + IP_OFFSET, ip, 16);
	memcpy(buf + PORT_OFFSET, &port, sizeof(port));
	memcpy(buf + REG_OFFSET, &reg, sizeof(reg));
	memcpy(buf + UID_OFFSET, &uid, sizeof(uid));
	memcpy(buf + FUNC_OFFSET, &uid, sizeof(func));
	memcpy(buf + POLL_SEC_OFFSET, &poll_s, sizeof(poll_s));
	memcpy(buf + POLL_MSEC_OFFSET, &poll_ms, sizeof(poll_ms));

	/* done, return number of bytes stored */
	return M2MD_ACK_FRAME_SIZE;

	#undef IP_OFFSET
	#undef PORT_OFFSET
	#undef REG_OFFSET
	#undef UID_OFFSET
	#undef FUNC_OFFSET
	#undef POLL_SEC_OFFSET
	#undef POLL_MSEC_OFFSET
}


/* ==========================================================================
    Request from network to register new modbus poll.
   ========================================================================== */
static void m2md_mqtt_poll_add
(
	struct mosquitto                *mqtt,     /* mqtt session */
	void                            *userdata, /* not used */
	const struct mosquitto_message  *msg       /* received message */
)
{
	char                 ip[INET_ADDRSTRLEN];  /* server ip address */
	uint16_t             port;                 /* port on which server listen */
	uint16_t             reg;                  /* register to poll */
	float                scale;
	uint8_t              func;                 /* function to use to read reg */
	uint8_t              uid;                  /* unit id to read message from*/
	uint32_t             poll_s;               /* poll time, seconds part */
	uint16_t             poll_ms;              /* poll time, millisecond part */
	char                *topic;
	struct m2md_pl_data  pdata;                /* poll data */

	/* message format, multibyte data is big-endian ordered
	 *
	 * char[16]         server_ip (padded with zeroes, with null terminator)
	 * uint16_t         server_port
	 * uint16_t         register to poll
	 * ieee754(single)  scale factor of field
	 * uint8_t          unit id
	 * uint8_t          function code to use to read register
	 * uint32_t         how often poll register? (seconds part)
	 * uint16_t         how often poll register? (milliseconds part)
	 * char[]           topic on which publish polled register
	 *
	 * so size is: 16+2+2+4+2+1+1+4+2+3 = 37bytes
	 *
	 * the last 3 bytes are minimum length of topic string, that should
	 * start with '/', and contain at least 1 character and ending '\0' */

	#define IP_OFFSET           (0)
	#define PORT_OFFSET         (IP_OFFSET + sizeof(ip))
	#define REG_OFFSET          (PORT_OFFSET + sizeof(port))
	#define SCALE_OFFSET        (REG_OFFSET + sizeof(reg))
	#define UID_OFFSET          (SCALE_OFFSET + sizeof(scale))
	#define FUNC_OFFSET         (UID_OFFSET + sizeof(uid))
	#define POLL_SEC_OFFSET     (FUNC_OFFSET + sizeof(func))
	#define POLL_MSEC_OFFSET    (POLL_SEC_OFFSET + sizeof(poll_s))
	#define TOPIC_OFFSET        (POLL_MSEC_OFFSET + sizeof(poll_ms))
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	(void)userdata;

	if (msg->payloadlen < 37)
		/* message frame must be at least 37 bytes
		 * long, no funny bussiness */
		goto incorect_data;

	if (((char*)msg->payload)[msg->payloadlen] != '\0')
		/* last character must be '\0' as it
		 * terminates topic string */
		goto incorect_data;

	/* copy data to fields */
	memcpy(ip, msg->payload + IP_OFFSET, sizeof(ip));
	ip[sizeof(ip) - 1] = '\0';
	memcpy(&port, msg->payload + PORT_OFFSET, sizeof(port));
	memcpy(&reg, msg->payload + REG_OFFSET, sizeof(reg));
	memcpy(&func, msg->payload + FUNC_OFFSET, sizeof(func));
	memcpy(&uid, msg->payload + UID_OFFSET, sizeof(uid));
	memcpy(&poll_s, msg->payload + POLL_SEC_OFFSET, sizeof(poll_s));
	memcpy(&poll_ms, msg->payload + POLL_MSEC_OFFSET, sizeof(poll_ms));
	topic = strndup(msg->payload + TOPIC_OFFSET, M2MD_TOPIC_MAX);

	/* received data is big-endian, convert to our architecture */
	port = ntohs(port);
	reg = ntohs(reg);
	poll_s = ntohl(poll_s);
	poll_ms = ntohs(poll_ms);
	scale = (float)ntohl((uint32_t)scale);

	/* prepare data to send to modbus module */
	pdata.func = func;
	pdata.reg = reg;
	pdata.uid = uid;
	pdata.scale = scale;
	pdata.topic = topic;
	pdata.poll_time.tv_sec = poll_s;
	pdata.poll_time.tv_nsec = poll_ms * 1000000l;
	pdata.next_read.tv_sec = 0;
	pdata.next_read.tv_nsec = 0;

	/* ship it! don't care for errors, they will
	 * be handled in modbus module. */

	m2md_modbus_add_poll(&pdata, ip, port);
	return;

incorect_data:
	el_print(ELW, "incorect poll/add request received");
	el_pmemory(ELW, msg->payload, msg->payloadlen);

	#undef IP_OFFSET
	#undef PORT_OFFSET
	#undef MFR_OFFSET
	#undef REG_OFFSET
	#undef UID_OFFSET
	#undef FUNC_OFFSET
	#undef POLL_SEC_OFFSET
	#undef POLL_MSEC_OFFSET
}


/* ==========================================================================
    Request from network to delete modbus poll.
   ========================================================================== */
static void m2md_mqtt_poll_delete
(
	struct mosquitto                *mqtt,     /* mqtt session */
	void                            *userdata, /* not used */
	const struct mosquitto_message  *msg       /* received message */
)
{
	char                 ip[INET_ADDRSTRLEN];  /* server ip address */
	uint16_t             port;                 /* port on which server listen */
	uint16_t             reg;                  /* register to poll */
	uint8_t              func;                 /* function to use to read reg */
	uint8_t              uid;                  /* unit id to read message from*/
	struct m2md_pl_data  pdata;                /* poll data */

	/* message format, multibyte data is big-endian ordered
	 *
	 * char[16]       server_ip (padded with zeroes, with null terminator)
	 * uint16_t       server_port
	 * uint16_t       register to poll
	 * uint8_t        unit id
	 * uint8_t        function code to use to read register
	 *
	 * so size is: 16+2+2+1+1 = 22bytes
	 */

	#define IP_OFFSET           (0)
	#define PORT_OFFSET         (IP_OFFSET + sizeof(ip))
	#define REG_OFFSET          (PORT_OFFSET + sizeof(port))
	#define UID_OFFSET          (REG_OFFSET + sizeof(reg))
	#define FUNC_OFFSET         (UID_OFFSET + sizeof(uid))
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	(void)userdata;

	if (msg->payloadlen != 22)
		/* message frame must be exactly 22 bytes
		 * long, no funny bussiness */
		goto incorect_data;

	/* copy data to fields */
	memcpy(ip, msg->payload + IP_OFFSET, sizeof(ip));
	ip[sizeof(ip) - 1] = '\0';
	memcpy(&port, msg->payload + PORT_OFFSET, sizeof(port));
	memcpy(&reg, msg->payload + REG_OFFSET, sizeof(reg));
	memcpy(&func, msg->payload + FUNC_OFFSET, sizeof(func));
	memcpy(&uid, msg->payload + UID_OFFSET, sizeof(uid));

	/* received data is big-endian, convert to our architecture */
	port = ntohs(port);
	reg = ntohs(reg);

	/* prepare data to send to modbus module */
	pdata.func = func;
	pdata.reg = reg;
	pdata.uid = uid;

	/* ship it! don't care for errors, they will
	 * be handled in modbus module. */
	m2md_modbus_delete_poll(&pdata, ip, port);
	return;

incorect_data:
	el_print(ELW, "incorect poll/delete request received");
	el_pmemory(ELW, msg->payload, msg->payloadlen);

	#undef IP_OFFSET
	#undef PORT_OFFSET
	#undef REG_OFFSET
	#undef UID_OFFSET
	#undef FUNC_OFFSET
}

#endif

/* ==========================================================================
    Called by mosquitto on connection response.
   ========================================================================== */
static void m2md_mqtt_on_connect
(
	struct mosquitto  *mqtt,      /* mqtt session */
	void              *userdata,  /* not used */
	int                result     /* connection result */
)
{
	const char *reasons[5] =
	{
		"connected with success",
		"refused: invalid protocol version",
		"refused: invalid identifier",
		"refused: broker unavailable",
		"reserved"
	};
	int i;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	(void)userdata;
	result = result > 4 ? 4 : result;

	if (result != 0)
	{
		el_print(ELE, "connection failed %s", reasons[result]);
		return;
	}

	el_print(ELN, "connected to the broker");

	for (i = 0; i != m2md_array_size(g_m2md_mqtt_subs); ++i)
	{
		char  topic[M2MD_TOPIC_MAX + 1];
		int   mid;
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		if (snprintf(topic, sizeof(topic), "%s%s", m2md_cfg->mqtt_topic,
					g_m2md_mqtt_subs[i].topic) > sizeof(topic))
			/* constructed topic is too big */
			continue_print(ELE,
					"cannot subscribe to %s topic to long, made this: %s",
					g_m2md_mqtt_subs[i], topic);

		if (mosquitto_subscribe(mqtt, &mid, topic, 0) != 0)
			continue_perror(ELE, "mosquitto_subscribe(%s)", topic);

		el_print(ELN, "sent subscribe request for %s, mid: %d", topic, mid);
	}
}


/* ==========================================================================
    Called by mosquitto when we subscribe to topic.
   ========================================================================== */
static void m2md_mqtt_on_subscribe
(
	struct mosquitto  *mqtt,         /* mqtt session */
	void              *userdata,     /* not used */
	int                mid,          /* message id */
	int                qos_count,    /* not used */
	const int         *granted_qos   /* not used */
)
{
	(void)userdata;
	(void)qos_count;
	(void)granted_qos;

	el_print(ELN, "subscribed to topic mid: %d", mid);
}


/* ==========================================================================
    Called by mosquitto when we disconnect from broker
   ========================================================================== */
static void m2md_mqtt_on_disconnect
(
	struct mosquitto  *mqtt,      /* mqtt session */
	void              *userdata,  /* not used */
	int                rc         /* disconnect reason */
)
{
	if (rc == 0)
	{
		/* called by us, it's fine */
		el_print(ELN, "mqtt disconnected with success");
		return;
	}

	/* unexpected disconnect, try to reconnect */
	while (mosquitto_reconnect(mqtt) != 0)
		el_print(ELC, "mosquitto_reconnect()"); /* STOP. GIVING. UP! */
}


/* ==========================================================================
    Called by mosquitto when we receive message. We send here proper command
    to proper module based on topic.
   ========================================================================== */
static void m2md_mqtt_on_message
(
	struct mosquitto                *mqtt,     /* mqtt session */
	void                            *userdata, /* not used */
	const struct mosquitto_message  *msg       /* received message */
)
{
	int                              i;        /* the i iterator */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	for (i = 0; i != m2md_array_size(g_m2md_mqtt_subs); ++i)
	{
		const char  *topic;
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

		/* strip first part of topic for conveniance */
		topic = msg->topic + strlen(m2md_cfg->mqtt_topic);

		if (strcmp(topic, g_m2md_mqtt_subs[i].topic) == 0)
			g_m2md_mqtt_subs[i].on_message(mqtt, userdata, msg);
	}
}


/* ==========================================================================
                       __     __ _          ____
        ____   __  __ / /_   / /(_)_____   / __/__  __ ____   _____ _____
       / __ \ / / / // __ \ / // // ___/  / /_ / / / // __ \ / ___// ___/
      / /_/ // /_/ // /_/ // // // /__   / __// /_/ // / / // /__ (__  )
     / .___/ \__,_//_.___//_//_/ \___/  /_/   \__,_//_/ /_/ \___//____/
    /_/
   ==========================================================================
    Initializes mosquitto context and connects to broker at 'ip:port'
   ========================================================================== */
int m2md_mqtt_init
(
	const char  *ip,    /* ip of the broker to connect */
	int          port   /* port on which broker listens */
)
{
	int          n;     /* number of conn failures */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	mosquitto_lib_init();

	if ((mqtt = mosquitto_new(m2md_cfg->mqtt_id, 1, NULL)) == NULL)
	{
		el_perror(ELF, "mosquitto_new(%s, 1, NULL)", m2md_cfg->mqtt_id);
		mosquitto_lib_cleanup();
		return -1;
	}

	mosquitto_connect_callback_set(mqtt, m2md_mqtt_on_connect);
	mosquitto_message_callback_set(mqtt, m2md_mqtt_on_message);
	mosquitto_subscribe_callback_set(mqtt, m2md_mqtt_on_subscribe);
	mosquitto_disconnect_callback_set(mqtt, m2md_mqtt_on_disconnect);

	n = 60;
	for (;;)
	{
		if (mosquitto_connect(mqtt, ip, port, 60) == 0)
			/* connected to the broker, bail out of the loop */
			break;

		/* error connecting or interupted by signal */
		if (g_m2md_run == 0)
		{
			/* someone lost his nerve and send us
			 * SIGTERM, enough is enough */
			mosquitto_destroy(mqtt);
			mosquitto_lib_cleanup();
			return -1;
		}
		else if (errno == ECONNREFUSED)
		{
			/* connection refused, either broker is not up yet
			 * or it actively rejets us, we will keep trying,
			 * printing log about it once a while */
			if (n++ == 60)
			{
				el_perror(ELF, "mosquitto_connect(%s, %d)", ip, port);
				n = 0;
			}

			/* sleep for some time before reconnecting */
			sleep(1);
			continue;
		}

		el_perror(ELF, "mosquitto_connect(%s, %d)", ip, port);
		mosquitto_destroy(mqtt);
		mosquitto_lib_cleanup();
		return -1;
	}

	return 0;
}


/* ==========================================================================
    Publishes message on specified 'broker' on given 'topic' with 'payload'
    of size 'paylen'. Function will construct topic with prefix from config,
    so don't do it yourself.
   ========================================================================== */
int m2md_mqtt_publish
(
	const char  *topic,    /* topic on which to publish message */
	const void  *payload,  /* data to publish */
	int          paylen    /* length of payload buffer */
)
{
	char         top[M2MD_TOPIC_MAX];
	int          toplen;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	VALID(EINVAL, topic);
	VALID(EINVAL, payload);

	/* construct topic with base from config file and passed topic */

	/* strip first slash */
	if (topic[0] == '/')
		topic += 1;

	toplen = snprintf(top, sizeof(top), "%s/%s", m2md_cfg->mqtt_topic, topic);

	if (toplen > sizeof(top))
		return_print(-1, ENOBUFS, ELE,
				"topic turned to be too large: %d, made this: %s",
				toplen, top);

	if (mosquitto_publish(mqtt, NULL, top, paylen, payload, 0, 0) != 0)
		return_perror(ELE, "mosquitto_publish(%s)", top);

	return 0;
}


#if 0

/* ==========================================================================
    Publishes poll/add/ack message telling others that new poll has been
    added.
   ========================================================================== */
int m2md_mqtt_publish_add_ack
(
	struct m2md_pl_data  *pdata,
	const char           *ip,
	int                   port
)
{
	unsigned char         buf[M2MD_ACK_FRAME_SIZE];
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* create buffer to send with ack */
	if (m2md_mqtt_create_ack_msg(buf, pdata, ip, port) != sizeof(buf))
		return_print(-1, 0, ELE, "m2md_mqtt_create_ack_msg() failed");

	/* and send ack, it's that simple to ack */
	return m2md_mqtt_publish("/ctl/poll/add/ack", buf, sizeof(buf));
}


/* ==========================================================================
    Publishes poll/delete/ack message telling others that new poll has been
    deleted. It could be used to readd it, once someone notices his favorite
    poll has been deleted by someone.
   ========================================================================== */
int m2md_mqtt_publish_delete_ack
(
	struct m2md_pl_data  *pdata,
	const char           *ip,
	int                   port
)
{
	unsigned char         buf[M2MD_ACK_FRAME_SIZE];
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	/* create buffer to send with ack */
	if (m2md_mqtt_create_ack_msg(buf, pdata, ip, port) != sizeof(buf))
		return_print(-1, 0, ELE, "m2md_mqtt_create_ack_msg() failed");

	/* and send ack, it's that simple to ack */
	return m2md_mqtt_publish("/ctl/poll/delete/ack", buf, sizeof(buf));
}

#endif

/* ==========================================================================
    Starts thread that will loop mosquitto object until stopped.
   ========================================================================== */
int m2md_mqtt_loop_start
(
	void
)
{
	return mosquitto_loop_start(mqtt);
}


/* ==========================================================================
    Disconnect from broker and restroy mosquitto context.
   ========================================================================== */
int m2md_mqtt_cleanup
(
	void
)
{
	mosquitto_disconnect(mqtt);
	mosquitto_destroy(mqtt);
	mosquitto_lib_cleanup();
	return 0;
}
