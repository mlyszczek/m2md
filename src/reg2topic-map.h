/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#ifndef M2MD_REG2TOPIC_MAP_H
#define M2MD_REG2TOPIC_MAP_H 1

#include <stddef.h>

enum m2md_mfr
{
    M2MD_MFR_VICTRON,

    M2MD_MFR_MAX
};

struct m2md_reg2topic_map_element
{
    const int    reg;
    const char  *topic;
};

struct m2md_reg2topic_map
{
    const struct m2md_reg2topic_map_element  *elements;
    const size_t                              num_elements;
};

extern const struct m2md_reg2topic_map m2md_reg2topic_map[];
int m2md_reg2topic_find(int mfr, int reg);

#endif
