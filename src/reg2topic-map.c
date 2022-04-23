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


#include "reg2topic-map.h"
#include "valid.h"
#include "macros.h"


/* ==========================================================================
          __             __                     __   _
     ____/ /___   _____ / /____ _ _____ ____ _ / /_ (_)____   ____   _____
    / __  // _ \ / ___// // __ `// ___// __ `// __// // __ \ / __ \ / ___/
   / /_/ //  __// /__ / // /_/ // /   / /_/ // /_ / // /_/ // / / /(__  )
   \__,_/ \___/ \___//_/ \__,_//_/    \__,_/ \__//_/ \____//_/ /_//____/

   ========================================================================== */


#define m2md_array_size(a) (sizeof(victron)/sizeof(*(a)))

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!! KEEP THEM MAPS SORTED! !!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * maps should be sorted by 'reg' field */
const struct m2md_reg2topic_map_element victron[] =
{
    { 0.1,  840, "battery/voltage" },
    { 0.1,  841, "battery/current" },
    {   1,  842, "battery/power" },
    {   1,  843, "battery/soc" },
    {   1, 3420, "in/digital/count" }
};

/* order of mfrs here must match order of mfrs in enum m2md_mfr */
const struct m2md_reg2topic_map m2md_reg2topic_map[] =
{
	{ victron, m2md_array_size(victron) }
};


/* ==========================================================================
                       __     __ _          ____
        ____   __  __ / /_   / /(_)_____   / __/__  __ ____   _____ _____
       / __ \ / / / // __ \ / // // ___/  / /_ / / / // __ \ / ___// ___/
      / /_/ // /_/ // /_/ // // // /__   / __// /_/ // / / // /__ (__  )
     / .___/ \__,_//_.___//_//_/ \___/  /_/   \__,_//_/ /_/ \___//____/
    /_/
   ==========================================================================
    Finds index in map of register for given mfr manufacture
   ========================================================================== */
int m2md_reg2topic_find
(
	int  mfr,   /* manufacture to search */
	int  reg    /* register to find */
)
{
	int  begin;
	int  end;
	int  i;
	const struct m2md_reg2topic_map           *map;
	const struct m2md_reg2topic_map_element   *element;
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/


	VALID(EINVAL, mfr < M2MD_MFR_MAX);

	map = m2md_reg2topic_map + mfr;

	begin = 0;
	end = map->num_elements - 1;

	while (begin <= end)
	{
		i = (begin + end) / 2;

		if (reg == map->elements[i].reg)
			/* matchin register found in position 'i' */
			return i;

		if (reg < map->elements[i].reg)
			end = i - 1;
		else
			begin = i + 1;
	}

	/* we get here only when register wasa not found in the list */
	return -1;
}
