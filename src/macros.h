/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */


#ifndef BOFC_MACROS_H
#define BOFC_MACROS_H 1


#define return_print(R, E, ...) do { \
	el_print(__VA_ARGS__); errno = E; return R; } while(0)

#define return_errno(E)     do { errno = E; return -1; } while(0)
#define return_perror(...)  do { el_perror(__VA_ARGS__); return -1; } while(0)

#define goto_perror(L, ...) do { el_perror(__VA_ARGS__); goto L; } while(0)

#define continue_print(...) do { el_print(__VA_ARGS__); continue; } while(0)
#define continue_perror(...)do { el_perror(__VA_ARGS__); continue; } while(0)
#define break_print(...)    do { el_print(__VA_ARGS__); break; } while(0)


#endif
