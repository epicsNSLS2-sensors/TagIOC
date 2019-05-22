
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

// object for bluetooth connection to device
gatt_connection_t *gatt_connection = 0;
pthread_mutex_t connlock = PTHREAD_MUTEX_INITIALIZER;

// current 2-byte value of motion configuration UUID
uint8_t motion_config[2];
pthread_mutex_t motionlock = PTHREAD_MUTEX_INITIALIZER;

// period of motion sensor scanning in 10s of ms
// minimum: 0x0A (100ms)
// max: 0xFF (2.55s)
#define MOTION_PERIOD 0x0A

#define MOTION_PERIOD_UUID "83"

#define TEMP_HUMIDITY_UUID "21"
#define TEMP_PRESSURE_UUID "41"
#define LIGHT_UUID "71"
#define MOTION_UUID "81"
#define BUTTON_UUID "-1"
#define BATTERY_UUID "-2"

static void disconnect();

// data for notification threads
typedef struct {
	char uuid_str[35];
	aSubRecord *pv;
	uuid_t uuid;
	int activate;
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
	int choice;
	float x = -1;

	if (strcmp(uuid, TEMP_HUMIDITY_UUID) == 0) {
		memcpy(&choice, pv->b, sizeof(int));
		// humidity
		if (choice == 1) {
			uint16_t raw = (data[2]) | (data[3] << 8);
			raw &= ~0x0003;
			x = ((double)raw / 65536)*100;
		}		
		// temperature
		else if (choice == 2) {
			uint16_t raw = (data[0]) | (data[1] << 8);
			x = ((double)(int16_t)raw / 65536)*165 - 40;
		}
		else {
			printf("Invaldi CHOICE for %s: %d\n", pv->name, choice);
			return;
		}
	}
	else if (strcmp(uuid, TEMP_PRESSURE_UUID) == 0) {
		memcpy(&choice, pv->b, sizeof(int));
		uint32_t raw;
		// pressure
		if (choice == 1) {
			raw = (data[3]) | (data[4] << 8) | (data[5] << 16);
		}
		// temperature
		else if (choice == 2) {
			raw = (data[0]) | (data[1] << 8) | (data[2] << 16);
		}
		else {
			printf("Invalid CHOICE for %s: %d\n", pv->name, choice);
			return;
		}
		x = raw / 100.0f;
	}
	else if (strcmp(uuid, LIGHT_UUID) == 0) {
		uint16_t raw = (data[0]) | (data[1] << 8);
		uint16_t m = raw & 0x0FFF;
		uint16_t e = (raw & 0xF000) >> 12;
		e = (e == 0) ? 1 : 2 << (e - 1);
		x = m * (0.01 * e);
	}
	else if (strcmp(uuid, MOTION_UUID) == 0) {
		memcpy(&choice, pv->b, sizeof(int));
		int16_t raw;
		// gyroscope
		if (choice >= 1 && choice <= 3) {
			// 1->4 2->2 3->0
			choice = abs(choice-3) * 2;
			raw = (data[choice]) | (data[choice+1] << 8);
			x = (raw * 1.0) / (65536 / 500);
		}
		// accelerometer
		else if (choice >= 4 && choice <= 6) {
			// 4->10 5->8 6->6
			choice = abs(choice-9) * 2;
			raw = (data[choice]) | (data[choice+1] << 8);
			x = (raw * 1.0) / (32768/2);
		}
		// magnetometer
		else if (choice >= 7 && choice <= 9) {
			// 7->12 8->14 9->16
			choice = (choice-1)*2;
			raw = (data[choice]) | (data[choice+1] << 8);
			x = 1.0 * raw;
		}
		else {
			printf("Invalid CHOICE for %s: %d\n", pv->name, choice);
			return;
		}
	}
	else if (strcmp(uuid, BUTTON_UUID) == 0) {
		int choice;
		memcpy(&choice, pv->b, sizeof(int));
		// button: 0=none, 1=user, 2=power, 3=both
		if (data[0]==choice || data[0]==3)
			x = 1;
		else
			x = 0;
	}
	else if (strcmp(uuid, BATTERY_UUID) == 0) {
		x = data[0];
	}
	memcpy(pv->vala, &x, sizeof(float));
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
	gatt_connection_t *conn = get_connection();
	pthread_mutex_lock(&connlock);
	
	// enable data collection
	// enable UUID = data UUID + 1
	if (args->activate == 1) {
		char input[40];
		int x = atoi(args->uuid_str);
		x += 1;
		snprintf(input, sizeof(input), "%d", x);
		uuid_t enable = sensorTagUUID(input);

		int ret;
		// motion sensors are enabled individually
		if (strcmp(args->uuid_str, MOTION_UUID) == 0) {
			int choice;
			memcpy(&choice, args->pv->b, sizeof(int));
			if (choice > 7)
				choice = 7;
			pthread_mutex_lock(&motionlock);
			uint8_t val = motion_config[0];
			uint8_t old = val;
			val |= (1 << (choice-1));
			motion_config[0] = val;
			ret = gattlib_write_char_by_uuid(conn, &enable, motion_config, sizeof(motion_config));
			if (ret == -1) {
				printf("Failed to activate pv %s\n", args->pv->name);
				motion_config[0] = old;
				pthread_mutex_unlock(&motionlock);
				pthread_mutex_unlock(&connlock);
				free(args);
				return;
			}

			// change scan period from default of 1 second
			uint8_t values[1];
			values[0] = MOTION_PERIOD;
			uuid_t period = sensorTagUUID(MOTION_PERIOD_UUID);
			gattlib_write_char_by_uuid(conn, &period, values, sizeof(values));

			pthread_mutex_unlock(&motionlock);
		}
		else {
			uint8_t values[1];
			values[0] = 1;
			ret = gattlib_write_char_by_uuid(conn, &enable, values, sizeof(values));
			if (ret == -1) {
				printf("Failed to activate pv %s\n", args->pv->name);
				free(args);
				pthread_mutex_unlock(&connlock);
				return;
			}
		}
	}
	// subscribe to UUID
	gattlib_register_notification(conn, writePV_callback, args);
	if (gattlib_notification_start(conn, &(args->uuid))) {
		printf("ERROR: Failed to start notifications for UUID %s (pv %s)\n", args->uuid_str, args->pv->name);
		free(args);
		pthread_mutex_unlock(&connlock);
		return;
	}
	printf("Starting notifications for pv %s\n", args->pv->name);

	// add notification to list for cleanup on shutdown
	NotificationNode *node = malloc(sizeof(NotificationNode));
	node->uuid = &(args->uuid);
	node->next = 0;
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

// read a single-byte UUID
static long readByte(uuid_t *uuid, char *name) {
	printf("reading %s\n", name);
	gatt_connection_t *conn = get_connection();
	pthread_mutex_lock(&connlock);
	printf("still reading\n");
	char data[100];
	char out_buf[100];
	memset(out_buf, 0, sizeof(out_buf));
	size_t len = sizeof(data);
	if (gattlib_read_char_by_uuid(conn, uuid, data, &len) == -1) {
		printf("Read of uuid %s failed.\n", name);
		pthread_mutex_unlock(&connlock);
		return -1;
	}
	pthread_mutex_unlock(&connlock);
	return data[0];
}

static long subscribeUUID(aSubRecord *pv) {
	// create subscriber thread
	int activate = 1;
	uuid_t uuid;
	if (strcmp(pv->a, BUTTON_UUID) == 0) {
		activate = 0;
		uuid_t button = CREATE_UUID16(0xffe1);
		uuid = button;
	}
	else if (strcmp(pv->a, BATTERY_UUID) == 0) {
		activate = 0;
		uuid_t battery = CREATE_UUID16(0x2a19);
		uuid = battery;

		// read current battery level
		// b/c notifications may be very infrequent
		//int level = readByte(&uuid, pv->name);
		//pv->vala = level;
	}
	else
		uuid = sensorTagUUID(pv->a);
	NotifyArgs *args = malloc(sizeof(NotifyArgs));
	args->uuid = uuid;
	args->pv = pv;
	args->activate = activate;
	strcpy(args->uuid_str, pv->a);
	pthread_t thread_id;
	pthread_create(&thread_id, NULL, &notificationListener, (void *)args);

	return 0;
}


/* Register these symbols for use by IOC code: */
epicsRegisterFunction(subscribeUUID);
