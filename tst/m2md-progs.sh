#!/usr/bin/env bash

. "$(dirname "${0}")"/mtest.sh

## ==========================================================================
#                                  _                __
#                    ____   _____ (_)_   __ ____ _ / /_ ___
#                   / __ \ / ___// /| | / // __ `// __// _ \
#                  / /_/ // /   / / | |/ // /_/ // /_ /  __/
#                 / .___//_/   /_/  |___/ \__,_/ \__/ \___/
#                /_/
#              ____                     __   _
#             / __/__  __ ____   _____ / /_ (_)____   ____   _____
#            / /_ / / / // __ \ / ___// __// // __ \ / __ \ / ___/
#           / __// /_/ // / / // /__ / /_ / // /_/ // / / /(__  )
#          /_/   \__,_//_/ /_/ \___/ \__//_/ \____//_/ /_//____/
#
## ==========================================================================


## ==========================================================================
## ==========================================================================


mt_prepare_test()
{
    echo "start"
}


## ==========================================================================
## ==========================================================================


mt_cleanup_test()
{
    echo "cleanup"
}


## ==========================================================================
#   __               __                                    __   _
#  / /_ ___   _____ / /_   ___   _  __ ___   _____ __  __ / /_ (_)____   ____
# / __// _ \ / ___// __/  / _ \ | |/_// _ \ / ___// / / // __// // __ \ / __ \
#/ /_ /  __/(__  )/ /_   /  __/_>  < /  __// /__ / /_/ // /_ / // /_/ // / / /
#\__/ \___//____/ \__/   \___//_/|_| \___/ \___/ \__,_/ \__//_/ \____//_/ /_/
#
## ==========================================================================


mt_return
