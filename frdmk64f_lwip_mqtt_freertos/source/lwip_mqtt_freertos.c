/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2020 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "lwip/opt.h"

#if LWIP_IPV4 && LWIP_RAW && LWIP_NETCONN && LWIP_DHCP && LWIP_DNS

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_phy.h"

#include "lwip/api.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dhcp.h"
#include "lwip/netdb.h"
#include "lwip/netifapi.h"
#include "lwip/prot/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "enet_ethernetif.h"
#include "lwip_mqtt_id.h"

#include "ctype.h"
#include "stdio.h"

#include "fsl_phyksz8081.h"
#include "fsl_enet_mdio.h"
#include "fsl_device_registers.h"
#include "mqtt.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* @TEST_ANCHOR */

/* MAC address configuration. */
#ifndef configMAC_ADDR
#define configMAC_ADDR                     \
    {                                      \
        0x02, 0x12, 0x13, 0x10, 0x15, 0x17 \
    }
#endif

/* Address of PHY interface. */
#define EXAMPLE_PHY_ADDRESS BOARD_ENET0_PHY_ADDRESS

/* MDIO operations. */
#define EXAMPLE_MDIO_OPS enet_ops

/* PHY operations. */
#define EXAMPLE_PHY_OPS phyksz8081_ops

/* ENET clock frequency. */
#define EXAMPLE_CLOCK_FREQ CLOCK_GetFreq(kCLOCK_CoreSysClk)

/* GPIO pin configuration. */
#define BOARD_LED_GPIO       BOARD_LED_RED_GPIO
#define BOARD_LED_GPIO_PIN   BOARD_LED_RED_GPIO_PIN
#define BOARD_SW_GPIO        BOARD_SW3_GPIO
#define BOARD_SW_GPIO_PIN    BOARD_SW3_GPIO_PIN
#define BOARD_SW_PORT        BOARD_SW3_PORT
#define BOARD_SW_IRQ         BOARD_SW3_IRQ
#define BOARD_SW_IRQ_HANDLER BOARD_SW3_IRQ_HANDLER


#ifndef EXAMPLE_NETIF_INIT_FN
/*! @brief Network interface initialization function. */
#define EXAMPLE_NETIF_INIT_FN ethernetif0_init
#endif /* EXAMPLE_NETIF_INIT_FN */

/*! @brief MQTT server host name or IP address. */
#define EXAMPLE_MQTT_SERVER_HOST "driver.cloudmqtt.com"
#define MQTT_USERNAME "Josue"
#define MQTT_PASSWORD "clase2023"
#define SIZE_TOPIC_GLOBAL 4

/*! @brief MQTT server port number. */
#define EXAMPLE_MQTT_SERVER_PORT 18591

/*! @brief Stack size of the temporary lwIP initialization thread. */
#define INIT_THREAD_STACKSIZE 1024

/*! @brief Priority of the temporary lwIP initialization thread. */
#define INIT_THREAD_PRIO DEFAULT_THREAD_PRIO

/*! @brief Stack size of the temporary initialization thread. */
#define APP_THREAD_STACKSIZE 1024

/*! @brief Priority of the temporary initialization thread. */
#define APP_THREAD_PRIO DEFAULT_THREAD_PRIO

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static void connect_to_mqtt(void *ctx);

typedef struct topic_temperature {
     uint8_t active;
     char string[20];
     float temperature;
 }topic_temperature;

struct topic_temperature topics[4] = {
     {0,"josue/temp/frijol", 0.0},
     {0,"josue/temp/zan", 0.0},
     {0,"josue/temp/calabaza", 0.0},
	 {0,"josue/temp/maiz", 0.0}
 };


static char incoming_data[256];


/*******************************************************************************
 * Variables
 ******************************************************************************/

static mdio_handle_t mdioHandle = {.ops = &EXAMPLE_MDIO_OPS};
static phy_handle_t phyHandle   = {.phyAddr = EXAMPLE_PHY_ADDRESS, .mdioHandle = &mdioHandle, .ops = &EXAMPLE_PHY_OPS};

/*! @brief MQTT client data. */
static mqtt_client_t *mqtt_client;

/*! @brief MQTT client ID string. */
static char client_id[40];

/*! @brief MQTT client information. */
static const struct mqtt_connect_client_info_t mqtt_client_info = {
    .client_id   = (const char *)&client_id[0],
    .client_user = MQTT_USERNAME,
    .client_pass = MQTT_PASSWORD,
    .keep_alive  = 100,
    .will_topic  = NULL,
    .will_msg    = NULL,
    .will_qos    = 0,
    .will_retain = 0,
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    .tls_config = NULL,
#endif
};

/*! @brief MQTT broker IP address. */
static ip_addr_t mqtt_addr;

/*! @brief Indicates connection to MQTT broker. */
static volatile bool connected = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Called when subscription request finishes.
 */
static void mqtt_topic_subscribed_cb(void *arg, err_t err)
{
    const char *topic = (const char *)arg;

    if (err == ERR_OK)
    {
        PRINTF("Subscribed to the topic \"%s\".\r\n", topic);
    }
    else
    {
        PRINTF("Failed to subscribe to the topic \"%s\": %d.\r\n", topic, err);
    }
}

/*!
 * @brief Called when there is a message on a subscribed topic.
 */
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{

    LWIP_UNUSED_ARG(arg);
    PRINTF("Received %u bytes from the topic \"%s\": \"", tot_len, topic);

    if(strncmp(topic,"josue/temp/frijol",strlen(topic)) == 0)
       {
       	PRINTF("Campo de frijol detectado\r\n");
       	topics[0].active = 1;
       }

    if(strncmp(topic,"josue/temp/zan",strlen(topic)) == 0)
       {
       	PRINTF("Campo de zanahoria detectado\r\n");
       	topics[1].active = 1;
       }

    if(strncmp(topic,"josue/temp/calabaza",strlen(topic)) == 0)
       {
       	PRINTF("Campo de calabaza detectado\r\n");
       	topics[2].active = 1;
       }

    if(strncmp(topic,"josue/temp/maiz",strlen(topic)) == 0)
       {
       	PRINTF("Campo de maiz detectado\r\n");
       	topics[3].active = 1;
       }

}

/*!
 * @brief Called when recieved incoming published message fragment.
 */
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{

    int i;
    LWIP_UNUSED_ARG(arg);
    float temp = atof(data);

    if(topics[0].active){
    	if(temp > 10.0){
    		PRINTF("---FRIJOL ACCION---\r\n");
    		PRINTF("ACCION REQUERIDA: REGAR\r\n");
    		PRINTF("---FRIJOL ACCION COMPLETADA---\r\n");

    	}
    	if(temp < 10.0){
    		PRINTF("ACCION REQUERIDA: APAGAR\r\n");
    		topics[0].active = 0;
    		publish_message_riego_frijol_off();


    	}

    }

    if(topics[1].active){
    	if(temp > 10.0){
    		PRINTF("---ZANAHORIA ACCION---\r\n");
    		PRINTF("ACCION REQUERIDA: REGAR\r\n");
    		PRINTF("---ZANAHORIA ACCION COMPLETADA---\r\n");

    	}
    	if(temp < 10.0){
    		topics[1].active = 0;
    		publish_message_riego_zanahoria_off();


    	}


    }

    if(topics[2].active){
    	if(temp > 10.0){
    		PRINTF("---CALABAZA ACCION---\r\n");
    		PRINTF("ACCION REQUERIDA: REGAR\r\n");
    		PRINTF("---CALABAZA ACCION COMPLETADA---\r\n");

    	}
    	if(temp < 10.0){
    		topics[2].active = 0;
    		publish_message_riego_calabaza_off();
    		//sys_msleep(3000U);

    	}

    }

    if(topics[3].active){
    	if(temp > 10.0){
    		PRINTF("---MAIZ ACCION---\r\n");
    		PRINTF("ACCION REQUERIDA: REGAR\r\n");
    		PRINTF("---MAIZ ACCION COMPLETADA---\r\n");

    	}
    	if(temp < 10.0){
    		topics[3].active = 0;
    		publish_message_riego_maiz_off();
    		//sys_msleep(3000U);

    	}

    }

    for (i = 0; i < len; i++)
    {
        if (isprint(data[i]))
        {
            PRINTF("%c", (char)data[i]);

        }

        else
        {
            PRINTF("\\x%02x", data[i]);
        }
    }

    if (flags & MQTT_DATA_FLAG_LAST)
    {

    	PRINTF("\"\r\n");

    }

}

/*!
 * @brief Subscribe to MQTT topics.
 */
static void mqtt_subscribe_topics(mqtt_client_t *client)
{
    static const char *topics[] = {"josue/temp/#", "smart_harvest/riego/#"};
    int qos[]                   = {1, 1}; //1 EXACTAMENTE UNA VEZ
    err_t err;
    int i;

    mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb,
                            LWIP_CONST_CAST(void *, &mqtt_client_info));

    for (i = 0; i < ARRAY_SIZE(topics); i++)
    {
        err = mqtt_subscribe(client, topics[i], qos[i], mqtt_topic_subscribed_cb, LWIP_CONST_CAST(void *, topics[i]));

        if (err == ERR_OK)
        {
            PRINTF("Subscribing to the topic \"%s\" with QoS %d...\r\n", topics[i], qos[i]);
        }
        else
        {
            PRINTF("Failed to subscribe to the topic \"%s\" with QoS %d: %d.\r\n", topics[i], qos[i], err);
        }
    }
}

/*!
 * @brief Called when connection state changes.
 */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;

    connected = (status == MQTT_CONNECT_ACCEPTED);

    switch (status)
    {
        case MQTT_CONNECT_ACCEPTED:
            PRINTF("MQTT client \"%s\" connected.\r\n", client_info->client_id);
            mqtt_subscribe_topics(client);
            break;

        case MQTT_CONNECT_DISCONNECTED:
            PRINTF("MQTT client \"%s\" not connected.\r\n", client_info->client_id);
            /* Try to reconnect 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_TIMEOUT:
            PRINTF("MQTT client \"%s\" connection timeout.\r\n", client_info->client_id);
            /* Try again 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION:
        case MQTT_CONNECT_REFUSED_IDENTIFIER:
        case MQTT_CONNECT_REFUSED_SERVER:
        case MQTT_CONNECT_REFUSED_USERNAME_PASS:
        case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:
            PRINTF("MQTT client \"%s\" connection refused: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;

        default:
            PRINTF("MQTT client \"%s\" connection status: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;
    }
}

/*!
 * @brief Starts connecting to MQTT broker. To be called on tcpip_thread.
 */
static void connect_to_mqtt(void *ctx)
{
    LWIP_UNUSED_ARG(ctx);

    PRINTF("Connecting to MQTT broker at %s...\r\n", ipaddr_ntoa(&mqtt_addr));

    mqtt_client_connect(mqtt_client, &mqtt_addr, EXAMPLE_MQTT_SERVER_PORT, mqtt_connection_cb,
                        LWIP_CONST_CAST(void *, &mqtt_client_info), &mqtt_client_info);
}

/*!
 * @brief Called when publish request finishes.
 */
static void mqtt_message_published_cb(void *arg, err_t err)
{
    const char *topic = (const char *)arg;

    if (err == ERR_OK)
    {
        PRINTF("Published to the topic \"%s\".\r\n", topic);
    }
    else
    {
        PRINTF("Failed to publish to the topic \"%s\": %d.\r\n", topic, err);
    }
}

/*!
 * @brief Publishes a message. To be called on tcpip_thread.
 */
static void publish_message(void *ctx)
{
    static const char *topic   = "smart_harvest/connected";
    static const char *message = "12";

    LWIP_UNUSED_ARG(ctx);

    PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

    mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);


}
/*Connected Off*/
static void publish_message_connected_off(void *ctx)
{
    static const char *topic   = "smart_harvest/connected";
    static const char *message = "14";

    LWIP_UNUSED_ARG(ctx);

    PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

    mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);


}

/*Indicates that friejol riego is on*/

static void publish_message_riego_frijol(void *ctx)
{
	static const char *topic = "smart_harvest/riego/frijol";
	static const char *message = "11";

	LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that frijol riego is off*/
void publish_message_riego_frijol_off()
{
	static const char *topic = "smart_harvest/riego/frijol";
	static const char *message = "12";

	//LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that zan riego is on*/
static void publish_message_riego_zanahoria(void *ctx)
{
	static const char *topic = "smart_harvest/riego/zan";
	static const char *message = "12";

	LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that zan riego is off*/
void publish_message_riego_zanahoria_off()
{
	static const char *topic = "smart_harvest/riego/zan";
	static const char *message = "14";

	//LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that calabaza riego is on*/
static void publish_message_riego_calabaza(void *ctx)
{
	static const char *topic = "smart_harvest/riego/calabaza";
	static const char *message = "13";

	LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that calabaza riego is off*/
void publish_message_riego_calabaza_off()
{
	static const char *topic = "smart_harvest/riego/calabaza";
	static const char *message = "14";

	//LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that maiz riego is on*/
static void publish_message_riego_maiz(void *ctx)
{
	static const char *topic = "smart_harvest/riego/maiz";
	static const char *message = "15";

	LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*Indicatest that maiz riego is off*/
void publish_message_riego_maiz_off()
{
	static const char *topic = "smart_harvest/riego/maiz";
	static const char *message = "14";

	//LWIP_UNUSED_ARG(ctx);

	PRINTF("Going to publish to the topic \"%s\"...\r\n", topic);

	mqtt_publish(mqtt_client, topic, message, strlen(message), 1, 0, mqtt_message_published_cb, (void *)topic);

}

/*!
 * @brief Application thread.
 */
static void app_thread(void *arg)
{
    struct netif *netif = (struct netif *)arg;
    struct dhcp *dhcp;
    err_t err;
    int i;

    /* Wait for address from DHCP */

    PRINTF("Getting IP address from DHCP...\r\n");

    do
    {
        if (netif_is_up(netif))
        {
            dhcp = netif_dhcp_data(netif);
        }
        else
        {
            dhcp = NULL;
        }

        sys_msleep(20U);

    } while ((dhcp == NULL) || (dhcp->state != DHCP_STATE_BOUND));

    PRINTF("\r\nIPv4 Address     : %s\r\n", ipaddr_ntoa(&netif->ip_addr));
    PRINTF("IPv4 Subnet mask : %s\r\n", ipaddr_ntoa(&netif->netmask));
    PRINTF("IPv4 Gateway     : %s\r\n\r\n", ipaddr_ntoa(&netif->gw));

    /*
     * Check if we have an IP address or host name string configured.
     * Could just call netconn_gethostbyname() on both IP address or host name,
     * but we want to print some info if goint to resolve it.
     */
    if (ipaddr_aton(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr) && IP_IS_V4(&mqtt_addr))
    {
        /* Already an IP address */
        err = ERR_OK;
    }
    else
    {
        /* Resolve MQTT broker's host name to an IP address */
        PRINTF("Resolving \"%s\"...\r\n", EXAMPLE_MQTT_SERVER_HOST);
        err = netconn_gethostbyname(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr);
    }

    if (err == ERR_OK)
    {
        /* Start connecting to MQTT broker from tcpip_thread */
        err = tcpip_callback(connect_to_mqtt, NULL);
        if (err != ERR_OK)
        {
            PRINTF("Failed to invoke broker connection on the tcpip_thread: %d.\r\n", err);
        }
    }
    else
    {
        PRINTF("Failed to obtain IP address: %d.\r\n", err);
    }

    tcpip_callback(publish_message_connected_off, NULL);
    /* Publish some messages */
    for (i = 0; i < 1000;)
    {
        if (connected)
        {
            err = tcpip_callback(publish_message, NULL);
            if (err != ERR_OK)
            {
                PRINTF("Failed to invoke publishing of a message on the tcpip_thread: %d.\r\n", err);
            }
            i++;
        }

        if(topics[0].active)
        {

        	err = tcpip_callback(publish_message_riego_frijol, NULL);
        	//sys_msleep(5000U);
        	topics[0].active = 0;
            if (err != ERR_OK)
            {
                PRINTF("Failed to invoke publish_message_riego_frijol on the tcpip_thread: %d.\r\n", err);
            }
            i++;


        }

        if(topics[1].active)
        {
        	err = tcpip_callback(publish_message_riego_zanahoria, NULL);
        	//sys_msleep(5000U);
        	topics[1].active = 0;
            if (err != ERR_OK)
            {
                PRINTF("Failed to invoke publish_message_riego_zanahoria on the tcpip_thread: %d.\r\n", err);
            }
            i++;

        }

        if(topics[2].active)
        {
        	err = tcpip_callback(publish_message_riego_calabaza, NULL);
        	//sys_msleep(5000U);
        	topics[2].active = 0;
            if (err != ERR_OK)
            {
                PRINTF("Failed to invoke publish_message_riego_calabaza on the tcpip_thread: %d.\r\n", err);
            }
            i++;

        }

        if(topics[3].active)
        {
        	err = tcpip_callback(publish_message_riego_maiz, NULL);
        	//sys_msleep(5000U);
        	topics[3].active = 0;
            if (err != ERR_OK)
            {
                PRINTF("Failed to invoke publish_message_riego_maiz on the tcpip_thread: %d.\r\n", err);
            }
            i++;

        }

        sys_msleep(1000U);
    }

    vTaskDelete(NULL);
}

static void generate_client_id(void)
{
    uint32_t mqtt_id[MQTT_ID_SIZE];
    int res;

    get_mqtt_id(&mqtt_id[0]);

    res = snprintf(client_id, sizeof(client_id), "nxp_%08lx%08lx%08lx%08lx", mqtt_id[3], mqtt_id[2], mqtt_id[1],
                   mqtt_id[0]);
    if ((res < 0) || (res >= sizeof(client_id)))
    {
        PRINTF("snprintf failed: %d\r\n", res);
        while (1)
        {
        }
    }
}

/*!
 * @brief Initializes lwIP stack.
 *
 * @param arg unused
 */
static void stack_init(void *arg)
{
    static struct netif netif;
    ip4_addr_t netif_ipaddr, netif_netmask, netif_gw;
    ethernetif_config_t enet_config = {
        .phyHandle  = &phyHandle,
        .macAddress = configMAC_ADDR,
    };

    LWIP_UNUSED_ARG(arg);
    generate_client_id();

    mdioHandle.resource.csrClock_Hz = EXAMPLE_CLOCK_FREQ;

    IP4_ADDR(&netif_ipaddr, 0U, 0U, 0U, 0U);
    IP4_ADDR(&netif_netmask, 0U, 0U, 0U, 0U);
    IP4_ADDR(&netif_gw, 0U, 0U, 0U, 0U);

    tcpip_init(NULL, NULL);

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    if (mqtt_client == NULL)
    {
        PRINTF("mqtt_client_new() failed.\r\n");
        while (1)
        {
        }
    }

    netifapi_netif_add(&netif, &netif_ipaddr, &netif_netmask, &netif_gw, &enet_config, EXAMPLE_NETIF_INIT_FN,
                       tcpip_input);
    netifapi_netif_set_default(&netif);
    netifapi_netif_set_up(&netif);

    netifapi_dhcp_start(&netif);

    PRINTF("\r\n************************************************\r\n");
    PRINTF(" MQTT client\r\n");
    PRINTF("************************************************\r\n");

    if (sys_thread_new("app_task", app_thread, &netif, APP_THREAD_STACKSIZE, APP_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("stack_init(): Task creation failed.", 0);
    }

    vTaskDelete(NULL);
}

/*!
 * @brief Main function
 */
int main(void)
{
    SYSMPU_Type *base = SYSMPU;
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    /* Disable SYSMPU. */
    base->CESR &= ~SYSMPU_CESR_VLD_MASK;

    /* Initialize lwIP from thread */
    if (sys_thread_new("main", stack_init, NULL, INIT_THREAD_STACKSIZE, INIT_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("main(): Task creation failed.", 0);
    }

    vTaskStartScheduler();

    /* Will not get here unless a task calls vTaskEndScheduler ()*/
    return 0;
}
#endif
