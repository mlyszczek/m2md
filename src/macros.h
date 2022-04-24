/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */


#ifndef BOFC_MACROS_H
#define BOFC_MACROS_H 1


#define return_print(R, E, ...) { el_print(__VA_ARGS__); errno = E; return R; }

#define return_errno(E)     { errno = E; return -1; }
#define return_perror(...)  { el_perror(__VA_ARGS__); return -1; }

#define goto_perror(L, ...) { el_perror(__VA_ARGS__); goto L; }

#define continue_print(...) { el_print(__VA_ARGS__); continue; }
#define continue_perror(...){ el_perror(__VA_ARGS__); continue; }
#define break_print(...)    { el_print(__VA_ARGS__); break; }


#endif
