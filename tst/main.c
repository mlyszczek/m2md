/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#include "mtest.h"

#include <embedlog.h>

mt_defs();  /* definitions for mtest */

int main(void)
{
    el_init();
    el_option(EL_OUT, EL_OUT_STDERR);
    el_option(EL_COLORS, 1);
    el_option(EL_FINFO, 1);

    el_cleanup();
    mt_return();
}
