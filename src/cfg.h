/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#ifndef M2MD_CFG_H
#define M2MD_CFG_H 1

#if HAVE_CONFIG_H
#   include "m2md-config.h"
#endif

#include <limits.h>
#include <stddef.h>

struct m2md_cfg
{

    /* log section options
     */

    int           log_frotate_number;
    int           log_fsync_level;
    size_t        log_frotate_size;
    size_t        log_fsync_every;
    int           log_level;
    int           log_ts;
    int           log_ts_tm;
    int           log_ts_tm_fract;
    int           log_finfo;
    int           log_funcinfo;
    int           log_colors;
    int           log_output;
    char          log_prefix[32 + 1];
    char          log_path[PATH_MAX + 1];

    /* mqtt section options
     */

    char          mqtt_ip[15 + 1];
    int           mqtt_port;
    char          mqtt_topic[1024 + 1];
    char          mqtt_id[128 + 1];

    /* modbus section options
     */

    int           modbus_max_re_time;
    char          modbus_poll_list[PATH_MAX + 1];
    char          modbus_map_list[PATH_MAX + 1];
};

extern const struct m2md_cfg  *m2md_cfg;
int m2md_cfg_init(int argc, char *argv[]);
void m2md_cfg_dump();

#endif
