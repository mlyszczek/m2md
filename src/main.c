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
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <signal.h>
#include <pthread.h>


#include "modbus.h"
#include "mqtt.h"

/* ==========================================================================
          __             __                     __   _
     ____/ /___   _____ / /____ _ _____ ____ _ / /_ (_)____   ____   _____
    / __  // _ \ / ___// // __ `// ___// __ `// __// // __ \ / __ \ / ___/
   / /_/ //  __// /__ / // /_/ // /   / /_/ // /_ / // /_/ // / / /(__  )
   \__,_/ \___/ \___//_/ \__,_//_/    \__,_/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


volatile int g_m2md_run;
pthread_t g_main_thread_t;

/* ==========================================================================
                  _                __           ____
    ____   _____ (_)_   __ ____ _ / /_ ___     / __/__  __ ____   _____ _____
   / __ \ / ___// /| | / // __ `// __// _ \   / /_ / / / // __ \ / ___// ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / __// /_/ // / / // /__ (__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/  /_/   \__,_//_/ /_/ \___//____/
/_/
   ========================================================================== */


/* ==========================================================================
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
    Useless signal handler for SIGUSR1 that does nothing but still is needed
   ========================================================================== */


static void sigusr_handler
(
    int signo  /* signal that triggered this handler */
)
{
    (void)signo;
}


/* ==========================================================================
                                              _
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
    int    argc,   /* number of arguments in argv */
    char  *argv[]  /* array of passed arguments */
)
{
    int    ret;    /* return code from the program */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    {
        /* install SIGTERM and SIGINT signals for clean exit.
         */

        struct sigaction  sa;  /* signal action instructions */
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        g_m2md_run = 1;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sigint_handler;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        sa.sa_handler = sigusr_handler;
        sigaction(SIGUSR1, &sa, NULL);
    }

    /* first things first, initialize configuration of the program
     */

    ret = m2md_cfg_init(argc, argv);

    if (ret == -1)
    {
        /* critical error occured when config was being parsed,
         * in such case we don't want to continue.
         */

        return 1;
    }

    if (ret == -2)
    {
        /* it was decided by config that program should not run,
         * but we should not exit with and error. This happens
         * when either -v or -h was passed
         */

        return 0;
    }

    /* I am pessimist in nature, so I assume things will screw
     * up from now on
     */

    ret = 1;

    /* Initialize logger. Should it fail? Fine, we don't care and
     * still continue with our program. Of course if logger fails
     * program will probably fail to, but meh... one can allow
     * himself to be optimistic from time to time.
     */

    if (el_init() == 0)
    {
        /* logger init succeed, configure it
        */

        el_option(EL_FROTATE_NUMBER, m2md_cfg->log_frotate_number);
        el_option(EL_FROTATE_SIZE, m2md_cfg->log_frotate_size);
        el_option(EL_FSYNC_EVERY, m2md_cfg->log_fsync_every);
        el_option(EL_FSYNC_LEVEL, m2md_cfg->log_fsync_level);
        el_option(EL_TS, m2md_cfg->log_ts);
        el_option(EL_TS_TM, m2md_cfg->log_ts_tm);
        el_option(EL_TS_FRACT, m2md_cfg->log_ts_tm_fract);
        el_option(EL_FINFO, m2md_cfg->log_finfo);
        el_option(EL_COLORS, m2md_cfg->log_colors);
        el_option(EL_PREFIX, m2md_cfg->log_prefix);
        el_option(EL_OUT, m2md_cfg->log_output);
        el_option(EL_LEVEL, m2md_cfg->log_level);

        if (m2md_cfg->log_output & EL_OUT_FILE)
        {
            /* we will be outputing logs to file, so we need to
             * open file now
             */

            if (el_option(EL_FPATH, m2md_cfg->log_path) != 0)
            {
                if (errno == ENAMETOOLONG || errno == EINVAL)
                {
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
                     * leaving other destinations intact
                     */

                    el_option(EL_OUT, m2md_cfg->log_output & ~EL_OUT_FILE);
                }

                /* print warning to stderr so it's not missed by integrator
                 * in case file output was the only output enabled
                 */

                fprintf(stderr, "w/failed to open log file %s, %s\n",
                        m2md_cfg->log_path, strerror(errno));
            }
        }
    }

    /* dump config, it's good to know what is program configuration
     * when debugging later
     */

    m2md_cfg_dump();
    g_main_thread_t = pthread_self();

    if (m2md_modbus_init() != 0)
    {
        el_perror(ELF, "m2md_modbus_init()");
        goto m2md_modbus_init_error;
    }

    if (m2md_mqtt_init(m2md_cfg->mqtt_ip, m2md_cfg->mqtt_port) != 0)
    {
        el_perror(ELF, "m2md_mqtt_init()");
        goto m2md_mqtt_init_error;
    }

    /* start mosquitto thread that will receive messages for us
     */

    if (m2md_mqtt_loop_start() != 0)
    {
        /* or maybe not...
         */

        el_perror(ELF, "mosquitto_start_loop()");
        goto m2md_mqtt_loop_start_error;
    }

    /* all resources initialized, now start main loop
     */

    el_print(ELN, "all resources initialized, starting main loop");

    while (g_m2md_run)
    {
        struct timespec  req;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


        /* go and poll what is to be polled, and sleep until
         * it is time to do next polling
         */

        req = m2md_modbus_loop();
        el_print(ELD, "main: sleep for %ld.%ld", (long)req.tv_sec, req.tv_nsec);
        nanosleep(&req, NULL);
    }

    /* Code should get here only when it finished without problems,
     * like it received signal. If it should exit abnormally, jump
     * after ret = 0; oh look, things didn't go wrong after all!
     * Set ret to 0 to caller of the app know about that
     */

    ret = 0;

m2md_mqtt_loop_start_error:
    m2md_mqtt_cleanup();

m2md_mqtt_init_error:
    m2md_modbus_cleanup();

m2md_modbus_init_error:
    el_print(ELN, "goodbye %s world!", ret ? "cruel" : "beautiful");
    el_cleanup();
    return ret;
}
