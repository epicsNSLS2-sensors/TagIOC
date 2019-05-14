
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <dbAccess.h>
#include <dbDefs.h>
#include <dbFldTypes.h>
#include <dbScan.h>

#include <registryFunction.h>
#include <aSubRecord.h>
#include <waveformRecord.h>
#include <epicsExport.h>
#include <epicsTime.h>
#include <callback.h>

#include <glib.h>
#include "gattlib.h"

#include "tag.h"

gatt_connection_t *gatt_connection = 0;
pthread_mutex_t connlock = PTHREAD_MUTEX_INITIALIZER;

#define TEMP_HUMIDITY_UUID "21"
#define TEMP_PRESSURE_UUID "41"
#define LIGHT_UUID "71"

static void disconnect();

// data for notification threads
typedef struct {
	char uuid_str[35];
	aSubRecord *pv;
	uuid_t uuid;
} NotifyArgs;

// linked list of subscribed UUIDs for cleanup
typedef struct {
	uuid_t *uuid;
	struct NotificationNode *next;
} NotificationNode;

NotificationNode *firstNode = 0;

// TODO: protect connection with lock without making everything hang
static gatt_connection_t *get_connection() {
	if (gatt_connection != 0) {
		return gatt_connection;
	}
	//pthread_mutex_lock(&connlock);
	if (gatt_connection != 0) {
		//pthread_mutex_unlock(&connlock);
		return gatt_connection;
	}
	//printf("Connecting to device %s...\n", mac_address);
	gatt_connection = gattlib_connect(NULL, mac_address, BDADDR_LE_PUBLIC, BT_SEC_LOW, 0, 0);
	signal(SIGINT, disconnect);
	//pthread_mutex_unlock(&connlock);
	//printf("Connected.\n");
	return gatt_connection;
}

static void disconnect() {
	gatt_connection_t *conn = gatt_connection;
	printf("Stopping notifications...\n");
	if (firstNode != 0) {
		NotificationNode *curr = firstNode; 
		NotificationNode *next;
		while (curr->next != 0) {
			next = curr->next;
			gattlib_notification_stop(conn, curr->uuid);
			free(curr);
			curr = next;
		}
		gattlib_notification_stop(conn, curr->uuid);
		free(curr);
	}
	gattlib_disconnect(conn);
	printf("Disconnected from device.\n");
	exit(1);
}

// parse notification and save to PV
// source for conversion formulas: http://processors.wiki.ti.com/index.php/CC2650_SensorTag_User's_Guide
static void writePV_callback(const uuid_t *uuidObject, const uint8_t *data, size_t len, void *user_data) {
	NotifyArgs *args = (NotifyArgs *) user_data;
	aSubRecord *pv = args->pv;
	char *uuid = args->uuid_str;

	if (strcmp(uuid, TEMP_HUMIDITY_UUID) == 0) {
		int choice;
		memcpy(&choice, pv->b, sizeof(int));
		float x;
		// temperature
		if (choice == 1) {
			uint16_t raw = (data[0]) | (data[1] << 8);
			x = ((double)(int16_t)raw / 65536)*165 - 40;
		}
		// humidity
		else if (choice == 2) {
			uint16_t raw = (data[2]) | (data[3] << 8);
			raw &= ~0x003;
			x = ((double)raw / 65536)*100;
		}		
		memcpy(pv->vala, &x, sizeof(float));
	}
	else if (strcmp(uuid, TEMP_PRESSURE_UUID) == 0) {
		int choice;
		memcpy(&choice, pv->b, sizeof(int));
		float x;
		// temperature
		if (choice == 1) {
			uint32_t raw = (data[0]) | (data[1] << 8) | (data[2] << 16);
			x = raw / 100.0f;
		}
		// pressure
		else if (choice == 2) {
			uint32_t raw = (data[3]) | (data[4] << 8) | (data[5] << 16);
			x = raw / 100.0f;
		}
		memcpy(pv->vala, &x, sizeof(float));
	}
	else if (strcmp(uuid, LIGHT_UUID) == 0) {
		uint16_t raw = (data[0]) | (data[1] << 8);
		uint16_t m = raw & 0x0FFF;
		uint16_t e = (raw & 0xF000) >> 12;
		e = (e == 0) ? 1 : 2 << (e - 1);
		float x = m * (0.01 * e);
		memcpy(pv->vala, &x, sizeof(float));
	}

	// for (int i=0; i < len; i++) {
	// 	printf("%d ", data[i]);
	// }
	// printf("\n");

	// uint8_t button = data[0];
	// if (button == 0)
	// 	printf("none\n");
	// else if (button == 1)
	// 	printf("left\n");
	// else if (button == 2)
	// 	printf("power\n");
	// else
	// 	printf("both\n");
	
	scanOnce(pv);
}

// taken from gattlib; convert string to 128 bit UUID object
static uint128_t str_to_128t(const char *string) {
	uint32_t data0, data4;
	uint16_t data1, data2, data3, data5;
	uint128_t u128;
	uint8_t *val = (uint8_t *) &u128;

	if(sscanf(string, "%08x-%04hx-%04hx-%04hx-%08x%04hx",
				&data0, &data1, &data2,
				&data3, &data4, &data5) != 6) {
		printf("Parse of UUID %s failed\n", string);
		memset(&u128, 0, sizeof(uint128_t));
		return u128;
	}

	data0 = htonl(data0);
	data1 = htons(data1);
	data2 = htons(data2);
	data3 = htons(data3);
	data4 = htonl(data4);
	data5 = htons(data5);

	memcpy(&val[0], &data0, 4);
	memcpy(&val[4], &data1, 2);
	memcpy(&val[6], &data2, 2);
	memcpy(&val[8], &data3, 2);
	memcpy(&val[10], &data4, 4);
	memcpy(&val[14], &data5, 2);

	return u128;
}

// construct a 128 bit UUID for a TI SensorTag
// given its unique UUID component
static uuid_t sensorTagUUID(const char *id) {
	char buf[40];
	strcpy(buf, "F000AA");
	strcat(buf, id);
	strcat(buf, "-0451-4000-B000-000000000000");
	//printf("%s\n", buf);
	uint128_t uuid_val = str_to_128t(buf);
	uuid_t uuid = {.type=SDP_UUID128, .value.uuid128=uuid_val};
	return uuid;
}

// thread function to begin listening for UUID notifications from device
static void *notificationListener(void *vargp) {
	NotifyArgs *args = (NotifyArgs *) vargp;
	
	// enable data collection
	char input[40];
	int x = atoi(args->pv->a);
	x += 1;
	snprintf(input, sizeof(input), "%d", x);
	gatt_connection_t *conn = get_connection();
	uuid_t enable = sensorTagUUID(input);
	uint8_t values[1];
	values[0] = 1;
	gattlib_write_char_by_uuid(conn, &enable, values, sizeof(values));

	// subscribe to UUID
	gattlib_register_notification(conn, writePV_callback, args);
	if (gattlib_notification_start(conn, &(args->uuid))) {
		printf("ERROR: Failed to start notifications for UUID %s (pv %s)\n", args->uuid_str, args->pv->name);
		free(args);
		return;
	}
	printf("Starting notifications for pv %s\n", args->pv->name);

	// add notification to list for cleanup on shutdown
	NotificationNode *node = malloc(sizeof(NotificationNode));
	node->uuid = &(args->uuid);
	node->next = 0;
	pthread_mutex_lock(&connlock);
	if (firstNode == 0)
		firstNode = node;
	else {
		NotificationNode *curr = firstNode;
		while (curr->next != 0)
			curr = curr->next;
		curr->next = node;
	}
	pthread_mutex_unlock(&connlock);

	// wait for notifications
	GMainLoop *loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(loop);
	printf("ERROR: pv %s thread exited\n", args->pv->name);
	return;
}

static long subscribeUUID(aSubRecord *pv) {
	// create subscriber thread
	uuid_t uuid = sensorTagUUID(pv->a);
	NotifyArgs *args = malloc(sizeof(NotifyArgs));
	args->uuid = uuid;
	args->pv = pv;
	strcpy(args->uuid_str, pv->a);
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, &notificationListener, (void *)args);

	return 0;
}

static long readUUID(aSubRecord *pv) {
	gatt_connection_t *conn = get_connection();
	uuid_t enable = sensorTagUUID("42");
	//uuid_t enable = CREATE_UUID16(0x2902);
	uint8_t values[1];
	values[0] = 1;
	int ret = gattlib_write_char_by_uuid(conn, &enable, values, sizeof(values));	

	char input[40];
	strcpy(input, pv->a);
	//strcat(input, pv->b);
	uuid_t uuid;
	uuid = sensorTagUUID(input);
	//gatt_connection_t *conn = get_connection();
	//printf("input: %s\n", input);

	int i;
	char byte[4];
	char data[100];
	char out_buf[100];
	memset(out_buf, 0, sizeof(out_buf));
	size_t len = sizeof(data);
	if (gattlib_read_char_by_uuid(conn, &uuid, data, &len) == -1) {
		printf("Read of uuid %s (pv %s) failed.\n", input, pv->name);
		return 1;
	}
	else {
		if (strcmp(input, "018015") == 0) {
			int level = data[0];
			char buf[5];
			snprintf(buf, sizeof(buf), "%d%%", level);
			strncpy(pv->vala, buf, strlen(buf));
		}
		else {
			for (i=0; i < len; i++) {
				snprintf(byte, sizeof(byte), "%02x ", data[i]);
				strcat(out_buf, byte);
			}
			printf("%s\n", out_buf);
			strncpy(pv->vala, out_buf, strlen(out_buf));
		}
	}
	return 0;
}


/* Register these symbols for use by IOC code: */
epicsRegisterFunction(subscribeUUID);
epicsRegisterFunction(readUUID);