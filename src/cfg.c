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

#if HAVE_CONFIG_H
#   include "m2md-config.h"
#endif

#if M2MD_ENABLE_INI
#   include <ini.h>
#endif

#if M2MD_ENABLE_GETOPT
#   include <unistd.h>
#endif

#if M2MD_ENABLE_GETOPT_LONG
#   include <unistd.h>
#   include <getopt.h>
#endif

#include <embedlog.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#if M2MD_ENABLE_GETOPT && M2MD_ENABLE_GETOPT_LONG
#   error Both M2MD_ENABLE_GETOPT and M2MD_ENABLE_GETOPT_LONG,   \
    you can set only one
#endif


/* ==========================================================================
          __             __                     __   _
     ____/ /___   _____ / /____ _ _____ ____ _ / /_ (_)____   ____   _____
    / __  // _ \ / ___// // __ `// ___// __ `// __// // __ \ / __ \ / ___/
   / /_/ //  __// /__ / // /_/ // /   / /_/ // /_ / // /_/ // / / /(__  )
   \__,_/ \___/ \___//_/ \__,_//_/    \__,_/ \__//_/ \____//_/ /_//____/

   ========================================================================== */

/* declarations to make life easier when parsing same thing over and over again
 */

#define PARSE_INT(OPTNAME, OPTARG, MINV, MAXV)                                 \
    {                                                                          \
        long   val;     /* value converted from name */                        \
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/   \
                                                                               \
        if (m2md_cfg_get_number(OPTARG, &val) != 0)                     \
        {                                                                      \
            fprintf(stderr, "%s, invalid number %s", #OPTNAME, OPTARG);        \
            return -1;                                                         \
        }                                                                      \
                                                                               \
        if (val < MINV || MAXV < val)                                          \
        {                                                                      \
            fprintf(stderr, "%s: bad value %s(%ld), min: %ld, max: %ld\n",     \
                    #OPTNAME, OPTARG, (long)val, (long)MINV, (long)MAXV);      \
            return -1;                                                         \
        }                                                                      \
        g_m2md_cfg.OPTNAME = val;                                       \
    }


#define PARSE_INT_INI(SECTION, OPTNAME, MINV, MAXV)                            \
    PARSE_INT(SECTION ## _ ## OPTNAME, value, MINV, MAXV)


#define VALID_STR(OPTNAME, OPTARG)                                             \
    size_t optlen;                                                             \
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/      \
                                                                               \
    optlen = strlen(OPTARG);                                                   \
                                                                               \
    if (optlen >= sizeof(g_m2md_cfg.OPTNAME))                           \
    {                                                                          \
        fprintf(stderr, "%s: is too long %s(%ld),  max: %ld\n",                \
                #OPTNAME, optarg, (long)optlen,                                \
                (long)sizeof(g_m2md_cfg.OPTNAME));                      \
        return -1;                                                             \
    }


#define PARSE_STR(OPTNAME, OPTARG)                                             \
    {                                                                          \
        VALID_STR(OPTNAME, OPTARG);                                            \
        strcpy(g_m2md_cfg.OPTNAME, OPTARG);                             \
    }


#define PARSE_STR_INI(SECTION, OPTNAME)                                        \
    PARSE_STR(SECTION ## _ ## OPTNAME, value)

#define PARSE_MAP(OPTNAME, OPTARG, MAP)                                        \
    {                                                                          \
        char map[43 + 1];                                       \
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/   \
                                                                               \
        strcpy(map, MAP);                                                      \
                                                                               \
        if (m2md_cfg_parse_map(map, OPTARG, &g_m2md_cfg.OPTNAME) != 0) \
        {                                                                      \
            fprintf(stderr, "error, value %s not found in allowed list %s\n",  \
                OPTARG, MAP);                                                  \
            return -1;                                                         \
        }                                                                      \
    }

#define PARSE_MAP_INI(SECTION, OPTNAME, MAP)                                   \
    PARSE_MAP(SECTION ## _ ## OPTNAME, value, MAP)

/* define config object in static storage duration - c'mon, you
 * won't be passing config pointer to every function, are you?
 */

static struct m2md_cfg  g_m2md_cfg;

/* define constant pointer to config object - it will be
 * initialized in init() function. It's const because you really
 * shouldn't modify config once it's set, if you need mutable
 * config, you will be better of creating dedicated module for that
 * and store data in /var/lib instead. This config module should
 * only be used with configs from /etc which should be readonly by
 * the programs
 */

const struct m2md_cfg  *m2md_cfg;

/* arrays of strings of options that have mapped values, used
 * to map config ints back into strings for nice cfg_dump()
 */

static const char *g_m2md_log_level_strings[] =
{
    "fatal",
    "alert",
    "crit",
    "error",
    "warn",
    "notice",
    "info",
    "dbg"
};

static const char *g_m2md_log_ts_strings[] =
{
    "off",
    "short",
    "long"
};

static const char *g_m2md_log_ts_tm_strings[] =
{
    "clock",
    "time",
    "realtime",
    "monotonic"
};

static const char *g_m2md_log_ts_tm_fract_strings[] =
{
    "off",
    "ms",
    "us",
    "ns"
};


/* ==========================================================================
                  _                __           ____
    ____   _____ (_)_   __ ____ _ / /_ ___     / __/__  __ ____   _____ _____
   / __ \ / ___// /| | / // __ `// __// _ \   / /_ / / / // __ \ / ___// ___/
  / /_/ // /   / / | |/ // /_/ // /_ /  __/  / __// /_/ // / / // /__ (__  )
 / .___//_/   /_/  |___/ \__,_/ \__/ \___/  /_/   \__,_//_/ /_/ \___//____/
/_/
   ========================================================================== */


/* ==========================================================================
    Parses map of strings and if arg is found in map, it's numerical value
    (map index) is stored in field
   ========================================================================== */


static int m2md_cfg_parse_map
(
    char        *map,
    const char  *arg,
    int         *field
)
{
    char        *maps;
    char        *maptok;
    int          i;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    maptok = strtok_r(map, ":", &maps);
    i = 0;

    /* iterate through all field in map and check if optarg
     * is in map
     */

    while (maptok)
    {
        if (strcmp(maptok, arg) == 0)
        {
            *field = i;

            /* value found in map and stored in field,
             * that's all we had to do
             */

            return 0;
        }

        ++i;
        maptok = strtok_r(NULL, ":", &maps);
    }

    return -1;
}


/* ==========================================================================
    Guess what! It prints help into stdout! Would you belive that?
   ========================================================================== */


#if M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG

static void m2md_cfg_print_help
(
    const char  *name
)
{
    printf("m2md - publishes modbus messages over mqtt broker\n"
"\n"
"Usage: %s [-h | -v | options] \n"
"\n", name);

    printf(

#if M2MD_ENABLE_GETOPT

"\t-h            print this help and exit\n"
"\t-v            print version information and exit\n"
#if M2MD_ENABLE_INI
"\t-c<path>      custom config file\n"
#endif
"\t-l<level>     maximum log level to print\n"
"\t-o<output>    outputs to enable for printing\n"
"\t-i<ip>        address of the mqtt broker\n"
"\t-p<port>      port on which broker listens\n"
"\t-t<topic>     base topic name for all messages\n"

#endif /* M2MD_ENABLE_GETOPT */
#if M2MD_ENABLE_GETOPT_LONG

"\t-h, --help                            print this help and exit\n"
"\t-v, --version                         print version information and exit\n"
#if M2MD_ENABLE_INI
"\t-c, --config=<path>                   custom config file\n"
#endif
"\t    --log-frotate-number=<num>        max number of files to rotate\n"
"\t    --log-fsync-level=<level>         minimum level of log that should always be synced\n"
"\t    --log-frotate-size=<size>         maximum size single log file can get\n"
"\t    --log-fsync-every=<size>          log will be synced to drive when this ammount of bytes have been written\n"
"\t-l, --log-level=<level>               maximum log level to print\n"
"\t    --log-ts=<ts>                     timestamp format to add to each log message\n"
"\t    --log-ts-tm=<tm>                  source of the clock to use for timestamping\n"
"\t    --log-ts-tm-fract=<fract>         level of fraction of seconds detail to print\n"
"\t    --log-finfo                       add filename to every print\n"
"\t    --log-funcinfo                    add function name to every print\n"
"\t    --log-colors                      add ascii colors to logs dependin on level printed\n"
"\t-o, --log-output=<output>             outputs to enable for printing\n"
"\t    --log-prefix=<prefix>             string to prefix each log print with\n"
"\t    --log-path=<path>                 path where to store logs\n"
"\t-i, --mqtt-ip=<ip>                    address of the mqtt broker\n"
"\t-p, --mqtt-port=<port>                port on which broker listens\n"
"\t-t, --mqtt-topic=<topic>              base topic name for all messages\n"
"\t    --mqtt-id=<name>                  mqtt id to use when connecting to broker\n"
"\t    --modbus-max-re-time=<seconds>    max time between reconnects in case connection to server fails\n"
"\t    --modbus-poll-list=<path>         path to file with poll list\n"
"\t    --modbus-map-list=<path>          path to file with mqtt->modbus map\n"
#endif /* M2MD_ENABLE_GETOPT_LONG */
);

}

#endif /* M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG */


/* ==========================================================================
    Print version and author of the program. And what else did you thing
    this function might do?
   ========================================================================== */


#if M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG

static void m2md_cfg_print_version
(
    void
)
{
    printf("m2md "PACKAGE_VERSION"\n"
        "by Michał Łyszczek <michal.lyszczek@bofc.pl>\n");
}

#endif /* M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG */


/* ==========================================================================
    Converts string number 'num' into number representation. Converted value
    will be stored in address pointed by 'n'.
   ========================================================================== */


#if M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG || M2MD_ENABLE_INIT

static int m2md_cfg_get_number
(
    const char  *num,  /* string to convert to number */
    long        *n     /* converted num will be placed here */
)
{
    const char  *ep;   /* endptr for strtol function */
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


    if (*num == '\0')
    {
        errno = EINVAL;
        return -1;
    }

    *n = strtol(num, (char **)&ep, 10);

    if (*ep != '\0')
    {
        errno = EINVAL;
        return -1;
    }

    if (*n == LONG_MAX || *n == LONG_MIN)
    {
        errno = ERANGE;
        return -1;
    }

    return 0;
}

#endif /* M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG || M2MD_ENABLE_INIT */


/* ==========================================================================
    Parse arguments passed from ini file. This funciton is called by inih
    each time it reads valid option from file.
   ========================================================================== */


#if M2MD_ENABLE_INI


static int m2md_cfg_parse_ini
(
    void        *user,     /* user pointer - not used */
    const char  *section,  /* section name of current option */
    const char  *name,     /* name of current option */
    const char  *value,    /* value of current option */
    int          lineno    /* line of current option */
)
{
    (void)user;
    (void)lineno;

    /* parsing section log
     */

    if (strcmp(section, "log") == 0)
    {
        if (strcmp(name, "frotate_number") == 0)
            PARSE_INT_INI(log, frotate_number, 1, INT_MAX)
        else if (strcmp(name, "fsync_level") == 0)
            PARSE_INT_INI(log, fsync_level, 0, 7)
        else if (strcmp(name, "frotate_size") == 0)
            PARSE_INT_INI(log, frotate_size, 0, SIZE_MAX)
        else if (strcmp(name, "fsync_every") == 0)
            PARSE_INT_INI(log, fsync_every, 0, SIZE_MAX)
        else if (strcmp(name, "level") == 0)
            PARSE_MAP_INI(log, level, "fatal:alert:crit:error:warn:notice:info:dbg")
        else if (strcmp(name, "ts") == 0)
            PARSE_MAP_INI(log, ts, "off:short:long")
        else if (strcmp(name, "ts_tm") == 0)
            PARSE_MAP_INI(log, ts_tm, "clock:time:realtime:monotonic")
        else if (strcmp(name, "ts_tm_fract") == 0)
            PARSE_MAP_INI(log, ts_tm_fract, "off:ms:us:ns")
        else if (strcmp(name, "finfo") == 0)
            PARSE_INT_INI(log, finfo, 0, 1)
        else if (strcmp(name, "funcinfo") == 0)
            PARSE_INT_INI(log, funcinfo, 0, 1)
        else if (strcmp(name, "colors") == 0)
            PARSE_INT_INI(log, colors, 0, 1)
        else if (strcmp(name, "output") == 0)
            PARSE_INT_INI(log, output, 0, 127)
        else if (strcmp(name, "prefix") == 0)
            PARSE_STR_INI(log, prefix)
        else if (strcmp(name, "path") == 0)
            PARSE_STR_INI(log, path)
    }

    /* parsing section mqtt
     */

    else if (strcmp(section, "mqtt") == 0)
    {
        if (strcmp(name, "ip") == 0)
            PARSE_STR_INI(mqtt, ip)
        else if (strcmp(name, "port") == 0)
            PARSE_INT_INI(mqtt, port, 0, 65535)
        else if (strcmp(name, "topic") == 0)
            PARSE_STR_INI(mqtt, topic)
        else if (strcmp(name, "id") == 0)
            PARSE_STR_INI(mqtt, id)
    }

    /* parsing section modbus
     */

    else if (strcmp(section, "modbus") == 0)
    {
        if (strcmp(name, "max_re_time") == 0)
            PARSE_INT_INI(modbus, max_re_time, 1, INT_MAX)
        else if (strcmp(name, "poll_list") == 0)
            PARSE_STR_INI(modbus, poll_list)
        else if (strcmp(name, "map_list") == 0)
            PARSE_STR_INI(modbus, map_list)
    }

    /* as far as inih is concerned, 1 is OK, while 0 would be error
     */

    return 1;
}

#endif /* M2MD_ENABLE_INI */


/* ==========================================================================
    Parse arguments passed from command line using getopt_long
   ========================================================================== */


#if M2MD_ENABLE_GETOPT_LONG

static int m2md_cfg_parse_args
(
    int    argc,
    char  *argv[]
)
{
    /* list of short options for getopt_long
     */

    const char *shortopts = ":hvc:l:o:i:p:t:";

    /* array of long options for getop_long
     */

    struct option longopts[] =
    {
        {"help",               no_argument,       NULL, 'h'},
        {"version",            no_argument,       NULL, 'v'},
        {"config",             required_argument, NULL, 'c'},
        {"log-frotate-number", required_argument, NULL, 256},
        {"log-fsync-level",    required_argument, NULL, 257},
        {"log-frotate-size",   required_argument, NULL, 258},
        {"log-fsync-every",    required_argument, NULL, 259},
        {"log-level",          required_argument, NULL, 'l'},
        {"log-ts",             required_argument, NULL, 260},
        {"log-ts-tm",          required_argument, NULL, 261},
        {"log-ts-tm-fract",    required_argument, NULL, 262},
        {"log-finfo",          no_argument,       NULL, 263},
        {"log-funcinfo",       no_argument,       NULL, 264},
        {"log-colors",         no_argument,       NULL, 265},
        {"log-output",         required_argument, NULL, 'o'},
        {"log-prefix",         required_argument, NULL, 266},
        {"log-path",           required_argument, NULL, 267},
        {"mqtt-ip",            required_argument, NULL, 'i'},
        {"mqtt-port",          required_argument, NULL, 'p'},
        {"mqtt-topic",         required_argument, NULL, 't'},
        {"mqtt-id",            required_argument, NULL, 268},
        {"modbus-max-re-time", required_argument, NULL, 269},
        {"modbus-poll-list",   required_argument, NULL, 270},
        {"modbus-map-list",    required_argument, NULL, 271},
        {NULL, 0, NULL, 0}
    };

    int  arg;
    int  loptind;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    optind = 1;
    while ((arg = getopt_long(argc, argv, shortopts, longopts, &loptind)) != -1)
    {
        switch (arg)
        {
        case 'h': m2md_cfg_print_help(argv[0]); return -2;
        case 'v': m2md_cfg_print_version(); return -2;
        case 'c': /* already parsed, ignore */; break;
        case 'l': PARSE_MAP(log_level, optarg, "fatal:alert:crit:error:warn:notice:info:dbg"); break;
        case 'o': PARSE_INT(log_output, optarg, 0, 127); break;
        case 'i': PARSE_STR(mqtt_ip, optarg); break;
        case 'p': PARSE_INT(mqtt_port, optarg, 0, 65535); break;
        case 't': PARSE_STR(mqtt_topic, optarg); break;
        case 256: PARSE_INT(log_frotate_number, optarg, 1, INT_MAX); break;
        case 257: PARSE_INT(log_fsync_level, optarg, 0, 7); break;
        case 258: PARSE_INT(log_frotate_size, optarg, 0, SIZE_MAX); break;
        case 259: PARSE_INT(log_fsync_every, optarg, 0, SIZE_MAX); break;
        case 260: PARSE_MAP(log_ts, optarg, "off:short:long"); break;
        case 261: PARSE_MAP(log_ts_tm, optarg, "clock:time:realtime:monotonic"); break;
        case 262: PARSE_MAP(log_ts_tm_fract, optarg, "off:ms:us:ns"); break;
        case 263: g_m2md_cfg.log_finfo = 1; break;
        case 264: g_m2md_cfg.log_funcinfo = 1; break;
        case 265: g_m2md_cfg.log_colors = 1; break;
        case 266: PARSE_STR(log_prefix, optarg); break;
        case 267: PARSE_STR(log_path, optarg); break;
        case 268: PARSE_STR(mqtt_id, optarg); break;
        case 269: PARSE_INT(modbus_max_re_time, optarg, 1, INT_MAX); break;
        case 270: PARSE_STR(modbus_poll_list, optarg); break;
        case 271: PARSE_STR(modbus_map_list, optarg); break;

        case ':':
            fprintf(stderr, "option -%c, --%s requires an argument\n",
                optopt, longopts[loptind].name);
            return -1;

        case '?':
            fprintf(stderr, "unknown option -%c (0x%02x)\n", optopt, optopt);
            return -1;

        default:
            fprintf(stderr, "unexpected return from getopt 0x%02x\n", arg);
            return -1;
        }
    }

    return 0;
}

#endif /* M2MD_ENABLE_GETOPT_LONG */


/* ==========================================================================
    Parse arguments passed from command line using getopt - no long options
    supported here, if option does not have short opt, use won't be able
    to set it via command line.
   ========================================================================== */


#if M2MD_ENABLE_GETOPT

static int m2md_cfg_parse_args
(
    int    argc,
    char  *argv[]
)
{
    /* list of short options for getopt()
     */

    const char *shortopts = ":hvc:l:o:i:p:t:";


    int  arg;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    optind = 1;
    while ((arg = getopt(argc, argv, shortopts)) != -1)
    {
        switch (arg)
        {
        case 'h': m2md_cfg_print_help(argv[0]); return -2;
        case 'v': m2md_cfg_print_version(); return -2;
        case 'c': /* already parsed, ignore */; break;
        case 'l': PARSE_MAP(log_level, optarg, "fatal:alert:crit:error:warn:notice:info:dbg"); break;
        case 'o': PARSE_INT(log_output, optarg, 0, 127); break;
        case 'i': PARSE_STR(mqtt_ip, optarg); break;
        case 'p': PARSE_INT(mqtt_port, optarg, 0, 65535); break;
        case 't': PARSE_STR(mqtt_topic, optarg); break;

        case ':':
            fprintf(stderr, "option -%c requires an argument\n", optopt);
            return -1;

        case '?':
            fprintf(stderr, "unknown option -%c (0x%02x)\n", optopt, optopt);
            return -1;

        default:
            fprintf(stderr, "unexpected return from getopt 0x%02x\n", arg);
            return -1;
        }
    }

    return 0;
}

#endif /* M2MD_ENABLE_GETOPT */


/* ==========================================================================
                       __     __ _          ____
        ____   __  __ / /_   / /(_)_____   / __/__  __ ____   _____ _____
       / __ \ / / / // __ \ / // // ___/  / /_ / / / // __ \ / ___// ___/
      / /_/ // /_/ // /_/ // // // /__   / __// /_/ // / / // /__ (__  )
     / .___/ \__,_//_.___//_//_/ \___/  /_/   \__,_//_/ /_/ \___//____/
    /_/
   ========================================================================== */



/* ==========================================================================
    Parses options in following order (privided that parsing was enabled
    during compilation).

    - set option values to their well-known default values
    - if proper c define (-D) is enabled, overwrite that option with it
    - overwrite options specified in ini
    - overwrite options passed by command line
   ========================================================================== */


int m2md_cfg_init
(
    int          argc,
    char        *argv[]
)
{
#if M2MD_ENABLE_INI
    const char  *file;
    int          arg;
    int          custom_conf;
#endif
    int          ret;
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    (void)argc;
    (void)argv;
    ret = 0;

    /* disable error printingfrom getopt library, some getopt() libraries
     * (like the one in nuttx) don't support opterr, so we add define to
     * disable this set. By default it is enabled though. To disable it
     * just pass -DM2MD_NO_OPTERR to compiler.
     */

#if M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG
#   ifndef M2MD_NO_OPTERR
    opterr = 0;
#   endif
#endif

    /* set cfg with default, well known values
     */

    g_m2md_cfg.log_frotate_number = 10;
    g_m2md_cfg.log_fsync_level = 1;
    g_m2md_cfg.log_frotate_size = 10485760;
    g_m2md_cfg.log_fsync_every = 4096;
    PARSE_MAP(log_level, "info", "fatal:alert:crit:error:warn:notice:info:dbg")
    PARSE_MAP(log_ts, "long", "off:short:long")
    PARSE_MAP(log_ts_tm, "realtime", "clock:time:realtime:monotonic")
    PARSE_MAP(log_ts_tm_fract, "ms", "off:ms:us:ns")
    g_m2md_cfg.log_finfo = 0;
    g_m2md_cfg.log_funcinfo = 0;
    g_m2md_cfg.log_colors = 0;
    g_m2md_cfg.log_output = 1;
    strcpy(g_m2md_cfg.log_prefix, "m2md: ");
    strcpy(g_m2md_cfg.log_path, "/var/log/m2md/m2md.log");

    strcpy(g_m2md_cfg.mqtt_ip, "127.0.0.1");
    g_m2md_cfg.mqtt_port = 1883;
    strcpy(g_m2md_cfg.mqtt_topic, "/modbus");
    strcpy(g_m2md_cfg.mqtt_id, "m2md");

    g_m2md_cfg.modbus_max_re_time = 60;
    strcpy(g_m2md_cfg.modbus_poll_list, "/etc/m2md/poll-list.conf");
    strcpy(g_m2md_cfg.modbus_map_list, "/etc/m2md/map-list.conf");

    /* overwrite values with those define in compiletime
     */

#ifdef M2MD_CFG_LOG_FROTATE_NUMBER
    g_m2md_cfg.log_frotate_number = M2MD_CFG_LOG_FROTATE_NUMBER;
#endif

#ifdef M2MD_CFG_LOG_FSYNC_LEVEL
    g_m2md_cfg.log_fsync_level = M2MD_CFG_LOG_FSYNC_LEVEL;
#endif

#ifdef M2MD_CFG_LOG_FROTATE_SIZE
    g_m2md_cfg.log_frotate_size = M2MD_CFG_LOG_FROTATE_SIZE;
#endif

#ifdef M2MD_CFG_LOG_FSYNC_EVERY
    g_m2md_cfg.log_fsync_every = M2MD_CFG_LOG_FSYNC_EVERY;
#endif

#ifdef M2MD_CFG_LOG_LEVEL
    PARSE_MAP(log_level, M2MD_CFG_LOG_LEVEL, "fatal:alert:crit:error:warn:notice:info:dbg")
#endif

#ifdef M2MD_CFG_LOG_TS
    PARSE_MAP(log_ts, M2MD_CFG_LOG_TS, "off:short:long")
#endif

#ifdef M2MD_CFG_LOG_TS_TM
    PARSE_MAP(log_ts_tm, M2MD_CFG_LOG_TS_TM, "clock:time:realtime:monotonic")
#endif

#ifdef M2MD_CFG_LOG_TS_TM_FRACT
    PARSE_MAP(log_ts_tm_fract, M2MD_CFG_LOG_TS_TM_FRACT, "off:ms:us:ns")
#endif

#ifdef M2MD_CFG_LOG_FINFO
    g_m2md_cfg.log_finfo = M2MD_CFG_LOG_FINFO;
#endif

#ifdef M2MD_CFG_LOG_FUNCINFO
    g_m2md_cfg.log_funcinfo = M2MD_CFG_LOG_FUNCINFO;
#endif

#ifdef M2MD_CFG_LOG_COLORS
    g_m2md_cfg.log_colors = M2MD_CFG_LOG_COLORS;
#endif

#ifdef M2MD_CFG_LOG_OUTPUT
    g_m2md_cfg.log_output = M2MD_CFG_LOG_OUTPUT;
#endif

#ifdef M2MD_CFG_LOG_PREFIX
    strcpy(g_m2md_cfg.log_prefix, M2MD_CFG_LOG_PREFIX);
#endif

#ifdef M2MD_CFG_LOG_PATH
    strcpy(g_m2md_cfg.log_path, M2MD_CFG_LOG_PATH);
#endif

#ifdef M2MD_CFG_MQTT_IP
    strcpy(g_m2md_cfg.mqtt_ip, M2MD_CFG_MQTT_IP);
#endif

#ifdef M2MD_CFG_MQTT_PORT
    g_m2md_cfg.mqtt_port = M2MD_CFG_MQTT_PORT;
#endif

#ifdef M2MD_CFG_MQTT_TOPIC
    strcpy(g_m2md_cfg.mqtt_topic, M2MD_CFG_MQTT_TOPIC);
#endif

#ifdef M2MD_CFG_MQTT_ID
    strcpy(g_m2md_cfg.mqtt_id, M2MD_CFG_MQTT_ID);
#endif

#ifdef M2MD_CFG_MODBUS_MAX_RE_TIME
    g_m2md_cfg.modbus_max_re_time = M2MD_CFG_MODBUS_MAX_RE_TIME;
#endif

#ifdef M2MD_CFG_MODBUS_POLL_LIST
    strcpy(g_m2md_cfg.modbus_poll_list, M2MD_CFG_MODBUS_POLL_LIST);
#endif

#ifdef M2MD_CFG_MODBUS_MAP_LIST
    strcpy(g_m2md_cfg.modbus_map_list, M2MD_CFG_MODBUS_MAP_LIST);
#endif


#if M2MD_ENABLE_INI

    /* next, process .ini file
     */

#   if M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG

    /* custom file can only be passed via command line if command
     * line parsing is enabled, no?
     */

    optind = 1;
    custom_conf = 0;
    file = M2MD_CONFIG_PATH_DEFAULT;

    /* we need to scan arg list to check if user provided custom
     * file path
     */

    while ((arg = getopt(argc, argv, "c:")) != -1)
    {
        if (arg == 'c')
        {
            file = optarg;
            custom_conf = 1;
        }

        /* ignore any unknown options, time will come to parse them too
         */
    }

#   else /* M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG */

    /* since user cannot pass custom conf, and only config is the
     * .ini file, we set custom_conf to 1, so logic in next step
     * can error out if config file does not exist
     */

     custom_conf = 1;

#   endif /* M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG */

    /* parse options from .ini config file, overwritting default
     * and/or compile time options
     */

     if (ini_parse(file, m2md_cfg_parse_ini, NULL) != 0)
     {
        /* failed to parse config file
         */

        if (custom_conf == 0 && errno == ENOENT)
        {
            /* if config file path was not overwritten by user -
             * and so we use default path - and file does not
             * exist, then don't crash the app, just continue with
             * default options
             */

            goto not_an_error_after_all;
        }

        return -1;
    }

not_an_error_after_all:
#endif /* M2MD_ENABLE_INI */

#if M2MD_ENABLE_GETOPT || M2MD_ENABLE_GETOPT_LONG
    /* parse options passed from command line - these have the
     * highest priority and will overwrite any other options
     */

    optind = 1;
    ret = m2md_cfg_parse_args(argc, argv);
#endif

    /* all good, initialize global config pointer with config
     * object
     */

    m2md_cfg = (const struct m2md_cfg *)&g_m2md_cfg;
    return ret;
}


/* ==========================================================================
    Dumps content of cfg to place which has been configured in embedlog.

    NOTE: remember to initialize embedlog before calling it!
   ========================================================================== */


void m2md_cfg_dump
(
    void
)
{
    /* macros to make printing easier
     */

#define CONFIG_PRINT_FIELD(FIELD, MODIFIER)                                    \
    el_print(ELN, "%s%s: "MODIFIER, #FIELD, padder + strlen(#FIELD),           \
        g_m2md_cfg.FIELD)

#define CONFIG_PRINT_VAR(VAR, MODIFIER)                                        \
    el_print(ELN, "%s%s: "MODIFIER, #VAR, padder + strlen(#VAR), VAR)

#define CONFIG_PRINT_MAP(FIELD)                                                \
    el_print(ELN, "%s%s: %s", #FIELD, padder + strlen(#FIELD),                 \
        g_m2md_ ## FIELD ## _strings[g_m2md_cfg.FIELD])

    const char *padder = "....................";
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    el_print(ELN, PACKAGE_STRING);
    el_print(ELN, "m2md configuration");

    CONFIG_PRINT_FIELD(log_frotate_number, "%d");
    CONFIG_PRINT_FIELD(log_fsync_level, "%d");
    CONFIG_PRINT_FIELD(log_frotate_size, "%lu");
    CONFIG_PRINT_FIELD(log_fsync_every, "%lu");
    CONFIG_PRINT_MAP(log_level);
    CONFIG_PRINT_MAP(log_ts);
    CONFIG_PRINT_MAP(log_ts_tm);
    CONFIG_PRINT_MAP(log_ts_tm_fract);
    CONFIG_PRINT_FIELD(log_finfo, "%d");
    CONFIG_PRINT_FIELD(log_funcinfo, "%d");
    CONFIG_PRINT_FIELD(log_colors, "%d");
    CONFIG_PRINT_FIELD(log_output, "%d");
    CONFIG_PRINT_FIELD(log_prefix, "%s");
    CONFIG_PRINT_FIELD(log_path, "%s");
    CONFIG_PRINT_FIELD(mqtt_ip, "%s");
    CONFIG_PRINT_FIELD(mqtt_port, "%d");
    CONFIG_PRINT_FIELD(mqtt_topic, "%s");
    CONFIG_PRINT_FIELD(mqtt_id, "%s");
    CONFIG_PRINT_FIELD(modbus_max_re_time, "%d");
    CONFIG_PRINT_FIELD(modbus_poll_list, "%s");
    CONFIG_PRINT_FIELD(modbus_map_list, "%s");

#undef CONFIG_PRINT_FIELD
#undef CONFIG_PRINT_VAR
}
