/* ==========================================================================
    Licensed under BSD 2clause license See LICENSE file for more information
    Author: Michał Łyszczek <michal.lyszczek@bofc.pl>
   ========================================================================== */

#ifndef M2MD_MQTT_H
#define M2MD_MQTT_H 1

int m2md_mqtt_init(const char *ip, int port);
int m2md_mqtt_cleanup(void);
int m2md_mqtt_publish(const char *topic, const void *payload, int paylen);
int m2md_mqtt_loop_start(void);
int m2md_mqtt_publish_add_ack(struct m2md_pl_data *pdata,
        const char *ip, int port);
int m2md_mqtt_publish_delete_ack(struct m2md_pl_data *pdata,
        const char *ip, int port);

#endif
