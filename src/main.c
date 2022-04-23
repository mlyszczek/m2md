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


#include "cfg.h"

#include <embedlog.h>
#include <errno.h>
#include <mosquitto.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "modbus.h"
#include "mqtt.h"
#include "macros.h"


/* ==========================================================================
     ____/ /___   _____ / /____ _ _____ ____ _ / /_ (_)____   ____   _____
    / __  // _ \ / ___// // __ `// ___// __ `// __// // __ \ / __ \ / ___/
   / /_/ //  __// /__ / // /_/ // /   / /_/ // /_ / // /_/ // / / /(__  )
   \__,_/ \___/ \___//_/ \__,_//_/    \__,_/ \__//_/ \____//_/ /_//____/
   ========================================================================== */


volatile int g_m2md_run;
volatile int g_flush_now;
pthread_t g_main_thread_t;


/* ==========================================================================
                  _                __           ____
    ____   _____ (_)_   __ ____ _ / /_ ___     / __/__  __ ____   _____ _____
   / __ \ / ___// /| | / // __ `// __// _ \   / /_ / / / // __ \ / ___// ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / __// /_/ // / / // /__ (__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/  /_/   \__,_//_/ /_/ \___//____/
/_/
   ==========================================================================
    Signal handler for SIGINT and SIGTERM.
   ========================================================================== */
static void sigint_handler
(
	int signo  /* signal that triggered this handler */
)
{
	(void)signo;

	g_m2md_run = 0;
}


/* ==========================================================================
    Handler for handling SIGUSR1 and SIGUSR2. On SIGUSR1 we flush logs to
    disk, SIGUSR2 does nothing, but is needed because internal logic sends
    this signal to force program to update its state. Check modbus.c file
    for details and look for SIGUSR2 usage.
   ========================================================================== */
static void sigusr_handler
(
	int signo  /* signal that triggered this handler */
)
{
	(void)signo;

	if (signo == SIGUSR1)
		g_flush_now = 1;
}


static int m2md_get_number
(
	const char  *num,  /* string to convert to number */
	long        *n     /* converted num will be placed here */
)
{
	const char  *ep;   /* endptr for strtol function */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	if (*num == '\0')
		return_errno(EINVAL);

	*n = strtol(num, (char **)&ep, 10);
	if (*ep != '\0')
		return_errno(EINVAL);
	if (*n == LONG_MAX || *n == LONG_MIN)
		return_errno(ERANGE);

	return 0;
}

static int m2md_get_float
(
	const char  *num,  /* string to convert to float */
	float       *n     /* converted num will be placed here */
)
{
	const char  *ep;   /* endptr for strtol function */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	if (*num == '\0')
		return_errno(EINVAL);

	*n = strtof(num, (char **)&ep);
	if (*ep != '\0')
		return_errno(EINVAL);
	if (*n == LONG_MAX || *n == LONG_MIN)
		return_errno(ERANGE);

	return 0;
}


static int m2md_parse_poll_file
(
	void
)
{
	const char          *file;
	FILE                *f;
	char                 line[4096];
	struct m2md_pl_data  poll;
	char                 ip[INET_ADDRSTRLEN];
	int                  port;
	char                *linetok;
	int                  lineno;
	long                 value;
	float                floatval;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#define NEXT_TOKEN(name) \
	if ((linetok = strtok(NULL, ",")) == NULL) \
		continue_print(ELW, "[%s:%d] missing field: %s", file, lineno, name);


	file = m2md_cfg->modbus_poll_list;
	if ((f = fopen(file, "r")) == NULL)
		return_perror(ELC, "fopen(%s)", file);

	for (lineno = 1;;++lineno)
	{
		/* set last byte of line buffer to something other than
		 * '\0' to know whether fgets have overwritten it or not */
		line[sizeof(line) - 1] = 0xaa;

		/* try to read whole line into buffer */
		if (fgets(line, sizeof(line), f) == NULL)
		{
			if (feof(f))
				/* end of file reached, and fgets didn't write
				 * anything into line - we parsed whole file */
				return 0;

			el_perror(ELC, "fgets(%s)", file);
			fclose(f);
			return -1;
		}

		/* fgets overwritted last byte with '\0', which means
		 * it filled whole line buffer with data
		 *   -- and --
		 * last character in string is not a new line
		 * character, so our line buffer turns out to be too
		 * small and we couln't read whole line into buffer.  */
		if (line[sizeof(line) - 1] == '\0' && line[sizeof(line) - 2] != '\n')
			continue_print(ELC,
					"[%s:%d], line is longer than %ld, ignoring line",
					file, lineno, (long)(sizeof(line) - 2));

		/* line is empty (only new line character is present)
		 *   -- or --
		 * line is a comment (starting from #) */
		if (line[0] == '\n' || line[0] == '#')
			continue;

		/* remove last newline character from line */
		line[strlen(line) - 1] = '\0';

		/* now that we have full line, we can parse fields in it */


	/* ==================================================================
	             (_)___    ___ _ ___/ /___/ /____ ___  ___  ___
	            / // _ \  / _ `// _  // _  // __// -_)(_-< (_-<
	           /_// .__/  \_,_/ \_,_/ \_,_//_/   \__//___//___/
	             /_/
	   ================================================================== */
		if ((linetok = strtok(line, ",")) == NULL)
			continue_print(ELW, "[%s:%d] no fields found", file, lineno);

		if (strlen(linetok) > sizeof(ip))
			continue_print(ELW, "%s:%d, invalid ip address: %s",
					file, lineno, linetok);

		strcpy(ip, linetok);


	/* ==================================================================
	                          ___  ___   ____ / /_
	                         / _ \/ _ \ / __// __/
	                        / .__/\___//_/   \__/
	                       /_/
	   ================================================================== */
		NEXT_TOKEN("port");

		if (m2md_get_number(linetok, &value) != 0)
			continue_print(ELW, "[%s:%d] invalid port %s", file, lineno, linetok);

		if (value < 1 || 65535 < value)
			continue_print(ELW, "[%s:%d] port is out of range [1,65535]",
					file, lineno);

		port = value;


	/* ==================================================================
	                  ___  / /___ _ _  __ ___   (_)___/ /
	                 (_-< / // _ `/| |/ // -_) / // _  /
	                /___//_/ \_,_/ |___/ \__/ /_/ \_,_/
	   ================================================================== */
		NEXT_TOKEN("slave id");

		if (m2md_get_number(linetok, &value) != 0)
			continue_print(ELW, "[%s:%d], invalid slave id: %s",
					file, lineno, linetok);

		if (value < 0 || 255 < value)
			continue_print(ELW, "[%s:%d] slave id is out of range [0,255]",
					file, lineno);

		poll.uid = value;


	/* ==================================================================
	                         / /_ __ __ ___  ___
	                        / __// // // _ \/ -_)
	                        \__/ \_, // .__/\__/
	                            /___//_/
	   ================================================================== */
		NEXT_TOKEN("type");

		if (linetok[0] != '+' && linetok[0] != '-')
			continue_print(ELW, "[%s:%d] first character of type must be + or -",
					file, lineno);

		poll.is_signed = linetok[0] == '-' ? 1 : 0;

		if (m2md_get_number(&linetok[1], &value) != 0)
			continue_print(ELW, "[%s:%d], invalid field width %s",
					file, lineno, &linetok[1]);

		if (value < 0 || 2 < value)
			continue_print(ELW, "[%s:%d] field width out of range [0,2]",
					file, lineno);

		poll.field_width = value;


	/* ==================================================================
	                 ____ ___  ___ _ (_)___ / /_ ___  ____
	                / __// -_)/ _ `// /(_-</ __// -_)/ __/
	               /_/   \__/ \_, //_//___/\__/ \__//_/
	                         /___/
	   ================================================================== */
		NEXT_TOKEN("register");

		if (m2md_get_number(linetok, &value) != 0)
			continue_print(ELW, "[%s:%d], invalid register number %s",
					file, lineno, linetok);

		if (value < 0 || 65535 < value)
			continue_print(ELW, "[%s:%d] register number is out of range [0,65535]",
					file, lineno);

		poll.reg = value;


	/* ==================================================================
	       __ _  ___  ___/ // /  __ __ ___   / _/__ __ ___  ____ / /_
	      /  ' \/ _ \/ _  // _ \/ // /(_-<  / _// // // _ \/ __// __/
	     /_/_/_/\___/\_,_//_.__/\_,_//___/ /_/  \_,_//_//_/\__/ \__/
	   ================================================================== */
		NEXT_TOKEN("modbus functions");

		if (m2md_get_number(linetok, &value) != 0)
			continue_print(ELW, "[%s:%d], invalid modbus function: %s",
					file, lineno, linetok);

		if (value < 0 || 255 < value)
			continue_print(ELW, "[%s:%d] modbus function is out of range [0,255]",
					file, lineno);

		poll.func = value;


	/* ==================================================================
	                         __       ___            __
	         ___ ____ ___ _ / /___   / _/___ _ ____ / /_ ___   ____
	        (_-</ __// _ `// // -_) / _// _ `// __// __// _ \ / __/
	       /___/\__/ \_,_//_/ \__/ /_/  \_,_/ \__/ \__/ \___//_/

	   ================================================================== */
		NEXT_TOKEN("scale factor");

		if (m2md_get_float(linetok, &floatval) != 0)
			continue_print(ELW, "[%s:%d] invalid scale factor: %s",
					file, lineno, linetok);

		poll.scale = floatval;


	/* ==================================================================
	          ___  ___   / // / ___ ___  ____ ___   ___  ___/ /___
	         / _ \/ _ \ / // / (_-</ -_)/ __// _ \ / _ \/ _  /(_-<
	        / .__/\___//_//_/ /___/\__/ \__/ \___//_//_/\_,_//___/
	       /_/
	   ================================================================== */
		NEXT_TOKEN("poll seconds");

		if (m2md_get_number(linetok, &value) != 0)
			continue_print(ELW, "[%s:%d], invalid poll seconds: %s",
					file, lineno, linetok);

		if (value < 0)
			continue_print(ELW, "[%s:%d] poll seconds is out of range [0,inf)",
					file, lineno);

		poll.poll_time.tv_sec = value;


	/* ==================================================================
	           ___  ___   / // / __ _   (_)/ // /(_)___ ___  ____
	          / _ \/ _ \ / // / /  ' \ / // // // /(_-</ -_)/ __/
	         / .__/\___//_//_/ /_/_/_//_//_//_//_//___/\__/ \__/
	        /_/
	   ================================================================== */
		NEXT_TOKEN("poll milliseconds");

		if (m2md_get_number(linetok, &value) != 0)
			continue_print(ELW, "[%s:%d], invalid poll milliseconds: %s",
					file, lineno, linetok);

		if (value < 0 || 999 < value)
			continue_print(ELW, "[%s:%d] poll milliseconds is out of range "
					"[0,999]", file, lineno);

		poll.poll_time.tv_nsec = value * 1000000l;


	/* ==================================================================
	                       / /_ ___   ___   (_)____
	                      / __// _ \ / _ \ / // __/
	                      \__/ \___// .__//_/ \__/
	                               /_/
	   ================================================================== */
		NEXT_TOKEN("topic");

		if (strlen(linetok) > M2MD_TOPIC_MAX)
			continue_print(ELW, "[%s:%d] topic is too long, max is %d",
					file, lineno, M2MD_TOPIC_MAX);

		if (mosquitto_sub_topic_check(linetok) != 0)
			continue_print(ELW, "[%s:%d] topic %s is not valid mqtt topic",
					file, lineno, linetok);

		poll.topic = strdup(linetok);


	/* ==================================================================
	                 ___ _ ___/ /___/ / ___  ___   / // /
	                / _ `// _  // _  / / _ \/ _ \ / // /
	                \_,_/ \_,_/ \_,_/ / .__/\___//_//_/
	                                 /_/
	   ================================================================== */

		/* all fields parsed, add new poll */
		if (m2md_modbus_add_poll(&poll, ip, port) != 0)
		{
			el_print(ELW, "m2md_modbus_add_poll(%s:%d)", ip, port);
			free(poll.topic);
			continue;
		}

		/* move to the next line */
	}

	fclose(f);
}


/* ==========================================================================
                           ____ ___   ____ _ (_)____
                          / __ `__ \ / __ `// // __ \
                         / / / / / // /_/ // // / / /
                        /_/ /_/ /_/ \__,_//_//_/ /_/
   ========================================================================== */


#if M2MD_ENABLE_LIBRARY
int m2md_main
#else
int main
#endif
(
	int     argc,        /* number of arguments in argv */
	char   *argv[]       /* array of passed arguments */
)
{
	int     ret;         /* return code from the program */
	time_t  prev_flush;  /* last time we flushed logs */
	time_t  now;         /* current timestamp */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	{
		/* install SIGTERM and SIGINT signals for clean exit.  */
		struct sigaction  sa;  /* signal action instructions */
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


		g_m2md_run = 1;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sigint_handler;
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);

		sa.sa_handler = sigusr_handler;
		sigaction(SIGUSR1, &sa, NULL);
		sigaction(SIGUSR2, &sa, NULL);
	}

	/* first things first, initialize configuration of the program */
	ret = m2md_cfg_init(argc, argv);

	if (ret == -1)
		/* critical error occured when config was being parsed,
		 * in such case we don't want to continue.  */
		return 1;

	if (ret == -2)
		/* it was decided by config that program should not run,
		 * but we should not exit with and error. This happens
		 * when either -v or -h was passed */
		return 0;

	/* I am pessimist in nature, so I assume
	 * things will screw up from now on */
	ret = 1;

	/* Initialize logger. Should it fail? Fine, we don't care and
	 * still continue with our program. Of course if logger fails
	 * program will probably fail to, but meh... one can allow
	 * himself to be optimistic from time to time. */
	if (el_init() == 0)
	{
		/* logger init succeed, configure it */
		el_option(EL_FROTATE_NUMBER, m2md_cfg->log_frotate_number);
		el_option(EL_FROTATE_SIZE, m2md_cfg->log_frotate_size);
		el_option(EL_FSYNC_EVERY, m2md_cfg->log_fsync_every);
		el_option(EL_FSYNC_LEVEL, m2md_cfg->log_fsync_level);
		el_option(EL_TS, m2md_cfg->log_ts);
		el_option(EL_TS_TM, m2md_cfg->log_ts_tm);
		el_option(EL_TS_FRACT, m2md_cfg->log_ts_tm_fract);
		el_option(EL_FINFO, m2md_cfg->log_finfo);
		el_option(EL_FUNCINFO, m2md_cfg->log_funcinfo);
		el_option(EL_COLORS, m2md_cfg->log_colors);
		el_option(EL_PREFIX, m2md_cfg->log_prefix);
		el_option(EL_OUT, m2md_cfg->log_output);
		el_option(EL_LEVEL, m2md_cfg->log_level);

		if (m2md_cfg->log_output & EL_OUT_FILE)
		{
			/* we will be outputing logs to file,
			 * so we need to open file now */
			if (el_option(EL_FPATH, m2md_cfg->log_path) != 0)
			{
				if (errno == ENAMETOOLONG || errno == EINVAL)
					/* in general embedlog will try to recover from
					 * any error that it may stumble upon (like
					 * directory does not yet exist - but will be
					 * created later, or permission is broker, but
					 * will be fixed later). That errors could be
					 * recovered from with some external help so
					 * there is no point disabling file logging.
					 * Any errors, except for these two.  In this
					 * case, disable logging to file as it is
					 * pointless, so we disable logging to file
					 * leaving other destinations intact */
					el_option(EL_OUT, m2md_cfg->log_output & ~EL_OUT_FILE);

				/* print warning to stderr so it's not missed by integrator
				 * in case file output was the only output enabled */
				fprintf(stderr, "w/failed to open log file %s, %s\n",
						m2md_cfg->log_path, strerror(errno));
			}
		}
	}

	/* dump config, it's good to know what is program
	 * configuration when debugging later */
	m2md_cfg_dump();
	g_main_thread_t = pthread_self();

	if (m2md_modbus_init() != 0)
		goto_perror(m2md_modbus_init_error, ELF, "m2md_modbus_init()");

	if (m2md_parse_poll_file() != 0)
		goto_perror(m2md_parse_poll_file_error, ELF, "m2md_parse_poll_file()");

	if (m2md_mqtt_init(m2md_cfg->mqtt_ip, m2md_cfg->mqtt_port) != 0)
		goto_perror(m2md_mqtt_init_error, ELF, "m2md_mqtt_init()");

	/* start mosquitto thread that will receive messages for us */
	if (m2md_mqtt_loop_start() != 0)
		/* or maybe not...  */
		goto_perror(m2md_mqtt_loop_start_error, ELF, "mosquitto_start_loop()");

	/* all resources initialized, now start main loop */
	el_print(ELN, "all resources initialized, starting main loop");

	prev_flush = 0;
	while (g_m2md_run)
	{
		struct timespec  req;
		/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


		/* go and poll what is to be polled, and sleep until
		 * it is time to do next polling */
		req = m2md_modbus_loop();
		nanosleep(&req, NULL);

		now = time(NULL);
		if (now - prev_flush >= 60 || g_flush_now)
		{
			if (g_flush_now)
				el_print(ELN, "flushing due to flush_now flag");

			/* it's been more than 60 seconds from last flush,
			 * or flush_now flag is set, let's flush logs now */
			el_flush();

			/* save time of last flush */
			prev_flush = now;
			g_flush_now = 0;
		}
	}

	/* Code should get here only when it finished without problems,
	 * like it received signal. If it should exit abnormally, jump
	 * after ret = 0; oh look, things didn't go wrong after all!
	 * Set ret to 0 to caller of the app know about that */
	ret = 0;

m2md_mqtt_loop_start_error:
	m2md_mqtt_cleanup();

m2md_mqtt_init_error:
m2md_parse_poll_file_error:
	m2md_modbus_cleanup();

m2md_modbus_init_error:
	el_print(ELN, "goodbye %s world!", ret ? "cruel" : "beautiful");
	el_cleanup();
	return ret;
}
