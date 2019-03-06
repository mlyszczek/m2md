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
    /* scale factory, data will be multiplicated by this value to
     * get expected value. For example, consider battery voltage.
     * On modbus there are values ranging from 0 to 65535, and
     * value 100 is 1V. In such case scale should be 0.01, because
     * 100 * 0.01V is 1V, and 65535 will be 655.35V. If we wanted
     * to represent same battery voltage but in milliseconds, we
     * would set factor to 10. So value 100 would be 100 * 10mV =
     * 1000mV.
     *
     * So scale basically says how much of real unit is for one
     * imaginary, as in scale 0.01 (let's say volts) tells us that
     * there is 0.01V per single imaginary value received on modbus
     */
    const float  scale;
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
