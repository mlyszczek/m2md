/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#ifndef M2MD_POLL_LIST_H
#define M2MD_POLL_LIST_H 1

#include <time.h>


/* struct describes what register and how often to pool it */
struct m2md_pl_data
{
	/* fields used to determin uniqueness of poll */
	int     func;                  /* modbus function to use on given reg */
	int     reg;                   /* register to poll */
	int     uid;                   /* unit id */
	/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

	char            *topic;        /* topic to publish register on */
	float            scale;        /* scale factor for the field */
	unsigned char    is_signed;    /* 1 - field is signed; 0 - unsigned */
	unsigned char    field_width;  /* field withd in bytes */
	struct timespec  poll_time;    /* poll register every this time */
	struct timespec  next_read;    /* absolute time of next poll */
};

/* struct describing single node in the list */
struct m2md_pl
{
	struct m2md_pl_data  data;
	struct m2md_pl      *next;
};

int m2md_pl_add(struct m2md_pl **head, const struct m2md_pl_data *data);
int m2md_pl_delete(struct m2md_pl **head, const struct m2md_pl_data *data);
int m2md_pl_destroy(struct m2md_pl *head);

#endif
