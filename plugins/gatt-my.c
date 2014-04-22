/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2010  Nokia Corporation
 *  Copyright (C) 2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <errno.h>
#include <gdbus/gdbus.h>
#include <stdlib.h>

#include "lib/uuid.h"
#include "src/plugin.h"
#include "src/adapter.h"
#include "src/hcid.h"
#include "src/log.h"
#include "attrib/gattrib.h"
#include "attrib/gatt-service.h"
#include "attrib/att.h"
#include "attrib/gatt.h"
#include "attrib/att-database.h"
#include "src/attrib-server.h"
#include "src/error.h"
#include "src/dbus-common.h"
#include "src/attio.h"
#include "src/device.h"

/* FIXME: Not defined by SIG? UUID128? */
#define MY_SVC_UUID		0xDEAD
#define MY_STATE_UUID		0xBEEF


#define MY_OBJECT_PATH		"/org/bluez/my"
#define MY_INTERFACE		"org.bluez.my"


struct my_adapter {
	struct btd_adapter	*adapter;
	uint16_t hnd_ccc;
	uint16_t hnd_value;
	gboolean updated;
};

static GSList *my_adapters = NULL;

static uint8_t my_state_read(
	struct attribute *a,
	struct btd_device *device,
	gpointer user_data)
{
	struct my_adapter *my_adapter = user_data;
	if (my_adapter->updated) {
		my_adapter->updated = false;
		{
			DBusError e;
			DBusConnection* conn;
			DBusMessage* msg;
			DBusMessage* rmsg;
			dbus_error_init(&e);
			conn  = dbus_bus_get(DBUS_BUS_SYSTEM, &e);
			if (dbus_error_is_set(&e)) {
				printf("name: %s\n", e.name);
				printf("mesg: %s\n", e.message);
				return 0;
			}
			msg = dbus_message_new_method_call(
			   "org.myapp",
			   "/org/myapp/server",
			   "org.myapp.server",
			   "read");
			dbus_error_init(&e);
			rmsg = dbus_connection_send_with_reply_and_block(
				conn,
				msg,
				1000,
				&e);
			if (dbus_error_is_set(&e)) {
				printf("name: %s\n", e.name);
				printf("mesg: %s\n", e.message);
				return 0;
			}
			{
				uint32_t size;
				uint8_t* bytes;
				if (!dbus_message_get_args(
					rmsg,
					NULL,
					DBUS_TYPE_ARRAY,
					DBUS_TYPE_BYTE,
					&bytes,
					&size,
					DBUS_TYPE_INVALID)) {
					return 0;
				}
				attrib_db_update(my_adapter->adapter, a->handle, NULL, bytes, size, NULL);
			}
		}
	}
	return 0;
}

static uint8_t my_state_write(
	struct attribute *a,
	struct btd_device *device,
	gpointer user_data)
{
	struct my_adapter *my_adapter = user_data;
	printf("write called\n");
	{
		DBusError e;
		DBusConnection* conn;
		DBusMessage* msg;
		dbus_bool_t bret;
		dbus_error_init(&e);
		conn  = dbus_bus_get(DBUS_BUS_SYSTEM, &e);
		printf("conn: %p\n", conn);
		if (dbus_error_is_set(&e)) {
			printf("name: %s\n", e.name);
			printf("mesg: %s\n", e.message);
			return 0;
		}
		msg = dbus_message_new_method_call(
			"org.myapp",
			"/org/myapp/server",
			"org.myapp.server",
			"write");
		bret = dbus_message_append_args(
			msg,
			DBUS_TYPE_ARRAY,
			DBUS_TYPE_BYTE,
			&a->data,
			a->len,
			DBUS_TYPE_INVALID);
		printf("bret: %d\n", bret);
		dbus_error_init(&e);
		dbus_connection_send_with_reply_and_block(
			conn,
			msg,
			1000,
			&e);
		if (dbus_error_is_set(&e)) {
			printf("name: %s\n", e.name);
			printf("mesg: %s\n", e.message);
			return 0;
		}
	}
	my_adapter->updated = true;
	return 0;
}

static gboolean register_my_service(struct my_adapter *my_adapter)
{
	bt_uuid_t uuid;
	bt_uuid16_create(&uuid, MY_SVC_UUID);

	return gatt_service_add(
		my_adapter->adapter,
		GATT_PRIM_SVC_UUID,
		&uuid,
		/* my characteristic */
		GATT_OPT_CHR_UUID16,
		MY_STATE_UUID,
		GATT_OPT_CHR_PROPS,
		GATT_CHR_PROP_READ | GATT_CHR_PROP_WRITE | GATT_CHR_PROP_NOTIFY | GATT_CHR_PROP_INDICATE,

		GATT_OPT_CHR_VALUE_CB,
		ATTRIB_READ,
		my_state_read,
		my_adapter,

		GATT_OPT_CHR_VALUE_CB,
		ATTRIB_WRITE,
		my_state_write,
		my_adapter,

		GATT_OPT_CCC_GET_HANDLE,
		&my_adapter->hnd_ccc,

		GATT_OPT_CHR_VALUE_GET_HANDLE,
		&my_adapter->hnd_value,

		GATT_OPT_INVALID);
}

static int my_server_probe(struct btd_adapter *adapter)
{
	struct my_adapter *my_adapter;

	my_adapter = g_new0(struct my_adapter, 1);
	my_adapter->adapter = btd_adapter_ref(adapter);
	my_adapter->updated = true;
	if (!register_my_service(my_adapter)) {
		DBG("Myservice could not be registered");
		g_free(my_adapter);
		return -EIO;
	}
	my_adapters = g_slist_append(my_adapters, my_adapter);

	return 0;
}

static int adapter_cmp(gconstpointer a, gconstpointer b)
{
	const struct my_adapter *my_adapter = a;
	const struct btd_adapter *adapter = b;

	return my_adapter->adapter == adapter ? 0 : -1;
}

static struct my_adapter *find_my_adapter(struct btd_adapter *adapter)
{
	GSList *l = g_slist_find_custom(my_adapters, adapter, adapter_cmp);

	return l ? l->data : NULL;
}

static void my_server_remove(struct btd_adapter *adapter)
{
	struct my_adapter *my_adapter;

	my_adapter = find_my_adapter(adapter);
	if (!my_adapter)
		return;

	my_adapters = g_slist_remove(my_adapters, my_adapter);
	btd_adapter_unref(my_adapter->adapter);

	g_free(my_adapter);
}



struct notify_indicate_data {
	struct my_adapter *my_adapter;
	uint8_t *value;
	size_t len;
	gboolean is_indicate;
};

struct notify_indicate_callback {
	int ref_count;
	struct notify_indicate_data *data;
	struct btd_device *device;
	guint id;
};

static void decrement_callback(gpointer user_data)
{
	struct notify_indicate_callback *cb = user_data;
	btd_device_unref(cb->device);
	if (__sync_sub_and_fetch(&cb->ref_count, 1)) return;
	btd_device_remove_attio_callback(cb->device, cb->id);
	g_free(cb->data->value);
	g_free(cb->data);
	g_free(cb);
}

static void confirm_callback(guint8 status, const guint8 *pdu, guint16 plen, gpointer user_data)
{
	DBusError e;
	DBusConnection* conn;
	DBusMessage* msg;

	if (status) {
		printf("Indication NOT confirmed\n");
		printf("status:%d, pdu[0]:%d, plen:%d\n", (int)status, (int)pdu[0], (int)plen);
		return;
	}

	dbus_error_init(&e);
	conn  = dbus_bus_get(DBUS_BUS_SYSTEM, &e);
	printf("conn: %p\n", conn);
	if (dbus_error_is_set(&e)) {
		printf("name: %s\n", e.name);
		printf("mesg: %s\n", e.message);
		return;
	}
	msg = dbus_message_new_method_call(
		"org.myapp",
		"/org/myapp/server",
		"org.myapp.server",
		"confirm");
	dbus_error_init(&e);
	dbus_connection_send_with_reply_and_block(
		conn,
		msg,
		10000,
		&e);
	if (dbus_error_is_set(&e)) {
		printf("name: %s\n", e.name);
		printf("mesg: %s\n", e.message);
		return;
	}
}

static void attio_connected_cb(GAttrib *attrib, gpointer user_data)
{
	struct notify_indicate_callback *cb = user_data;
	struct notify_indicate_data *nd = cb->data;
	struct my_adapter *my_adapter = nd->my_adapter;
	size_t rest = nd->len;
	size_t offset = 0;
	while (offset < nd->len) {
		size_t len;
		uint8_t *pdu = g_attrib_get_buffer(attrib, &len);
		const uint16_t min_len = sizeof(pdu[0]) + sizeof(uint16_t);
		size_t payload_len = rest + min_len < len ? rest
												  : len - min_len;
		if (nd->is_indicate) {
			len = enc_indication(my_adapter->hnd_value,
								 nd->value + offset,
								 payload_len,
								 pdu,
								 len);
		}
		else {
			len = enc_notification(my_adapter->hnd_value,
								   nd->value + offset,
								   payload_len,
								   pdu,
								   len);
		}
		offset += payload_len;
		rest -= payload_len;
		cb->device = btd_device_ref(cb->device);
		__sync_fetch_and_add(&cb->ref_count, 1);
		DBG("Send notification for handle: 0x%04x, ccc: 0x%04x",
			my_adapter->hnd_value,
			my_adapter->hnd_ccc);
		g_attrib_send(attrib, 0, pdu, len, confirm_callback, cb, decrement_callback);
	}
	decrement_callback(cb);
}

static gboolean check_start_status(struct btd_device *device, uint16_t ccc, gboolean is_indicate)
{
	char *filename;
	GKeyFile *key_file;
	char handle[6];
	char *str;
	uint16_t val;
	gboolean result;

	sprintf(handle, "%hu", ccc);

	filename = btd_device_get_storage_path(device, "ccc");
	if (!filename) {
		warn("Unable to get ccc storage path for device");
		return FALSE;
	}

	key_file = g_key_file_new();
	g_key_file_load_from_file(key_file, filename, 0, NULL);

	str = g_key_file_get_string(key_file, handle, "Value", NULL);
	if (!str) {
		result = FALSE;
		goto end;
	}

	val = strtol(str, NULL, 16);
	if (is_indicate) {
		if (!(val & 0x0002)) {
		  result = FALSE;
		  goto end;
		}
	}
	else {
		if (!(val & 0x0001)) {
		  result = FALSE;
		  goto end;
		}
	}

	result = TRUE;
end:
	g_free(str);
	g_free(filename);
	g_key_file_free(key_file);

	return result;
}

static void filter_devices(struct btd_device *device, void *user_data)
{
	struct notify_indicate_data *data = user_data;
	struct my_adapter *my_adapter = data->my_adapter;
	struct notify_indicate_callback *cb;

	if (!check_start_status(device, my_adapter->hnd_ccc, data->is_indicate)) {
		DBG("Device is not notificable\n");
		return;
	}
	if (data->is_indicate) {
		DBG("indicate\n");
	}
	else {
		DBG("notify\n");
	}
	{
	  size_t i = 0;
	  for (; i < data->len; ++i) {
		printf("%02x ", data->value[i]);
	  }
	  printf("\n");
	}
	cb = g_new0(struct notify_indicate_callback, 1);
	cb->ref_count = 1;
	cb->data = data;
	cb->device = btd_device_ref(device);
	cb->id = btd_device_add_attio_callback(device,
						attio_connected_cb, NULL, cb);
}


static void notify_devices(struct my_adapter *my_adapter,
			uint8_t *value, size_t len)
{
	struct notify_indicate_data *data;

	data = g_new0(struct notify_indicate_data, 1);
	data->my_adapter = my_adapter;
	data->value = g_memdup(value, len);
	data->len = len;
	data->is_indicate = false;

	btd_adapter_for_each_device(my_adapter->adapter, filter_devices,
					data);
}

static DBusMessage *my_notify(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	uint32_t size;
	uint8_t* bytes;
	if (!dbus_message_get_args(msg,
							   NULL,
							   DBUS_TYPE_ARRAY,
							   DBUS_TYPE_BYTE,
							   &bytes,
							   &size,
							   DBUS_TYPE_INVALID)) {
		return btd_error_invalid_args(msg);
	}
	{
		struct my_adapter *my_adapter = my_adapters->data;
		printf("size:%d\n", size);

		notify_devices(my_adapter, bytes, size);
		return dbus_message_new_method_return(msg);
	}
}

static void indicate_devices(struct my_adapter *my_adapter,
			uint8_t *value, size_t len)
{
	struct notify_indicate_data *data;

	data = g_new0(struct notify_indicate_data, 1);
	data->my_adapter = my_adapter;
	data->value = g_memdup(value, len);
	data->len = len;
	data->is_indicate = true;

	btd_adapter_for_each_device(my_adapter->adapter, filter_devices,
					data);
}

static DBusMessage *my_indicate(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	uint32_t size;
	uint8_t* bytes;
	if (!dbus_message_get_args(msg,
							   NULL,
							   DBUS_TYPE_ARRAY,
							   DBUS_TYPE_BYTE,
							   &bytes,
							   &size,
							   DBUS_TYPE_INVALID)) {
		return btd_error_invalid_args(msg);
	}
	{
		struct my_adapter *my_adapter = my_adapters->data;
		printf("size:%d\n", size);

		indicate_devices(my_adapter, bytes, size);
		return dbus_message_new_method_return(msg);
	}
}

static void get_min_mtu(struct btd_device *device, void *user_data)
{
	size_t* mtu = user_data;
	size_t ret_mtu = min_mtu_from_device(device);
	DBG("ret_mtu:%d\n", (int)ret_mtu);
	if ((int)ret_mtu < ATT_DEFAULT_LE_MTU) {
		ret_mtu = ATT_DEFAULT_LE_MTU;
	}
	if (ret_mtu < *mtu) *mtu = ret_mtu;
}


static DBusMessage *get_mtu(DBusConnection *conn, DBusMessage *msg,
								void *data)
{
	struct my_adapter *my_adapter = my_adapters->data;
	size_t mtu = UINT_MAX;
	DBG("get_mtu\n");
	btd_adapter_for_each_device(my_adapter->adapter, get_min_mtu,
								&mtu);
	DBG("for each end. mtu:%d\n", (int)mtu);
	{
		DBusMessage* rmsg;
		dbus_bool_t bret;
		rmsg = dbus_message_new_method_return(msg);
		DBG("rmsg:%p\n", rmsg);
		bret = dbus_message_append_args(rmsg, DBUS_TYPE_UINT32, &mtu, DBUS_TYPE_INVALID);
		DBG("bret = %d\n", bret);
		DBG("mtu = %d\n", (int)mtu);
		return rmsg;
	}
}

static struct btd_adapter_driver my_server = {
	.name	= "gatt-my-server",
	.probe	= my_server_probe,
	.remove	= my_server_remove,
};

/* bytes means a label (any string is ok) */
/* ay means Array of Bytes */
static const GDBusMethodTable my_methods[] = {
	{ GDBUS_METHOD("MyNotify",
			GDBUS_ARGS(
				   { "bytes", "ay" }), NULL,
				   my_notify) },
	{ GDBUS_METHOD("MyIndicate",
			GDBUS_ARGS(
				   { "bytes", "ay" }), NULL,
				   my_indicate) },
	{ GDBUS_METHOD("GetMtu",
			NULL, GDBUS_ARGS(
				   { "mtu", "u" }),
				   get_mtu) },
	{ }
};

static void my_destroy(gpointer user_data)
{
}

static int gatt_my_init(void)
{
	if (!g_dbus_register_interface(btd_get_dbus_connection(),
					MY_OBJECT_PATH, MY_INTERFACE,
					my_methods, NULL, NULL, NULL,
					my_destroy)) {
		error("D-Bus failed to register %s interface",
							MY_INTERFACE);
		return -EIO;
	}
	return btd_register_adapter_driver(&my_server);
}

static void gatt_my_exit(void)
{
	btd_unregister_adapter_driver(&my_server);
	g_dbus_unregister_interface(btd_get_dbus_connection(),
					MY_OBJECT_PATH, MY_INTERFACE);
}

BLUETOOTH_PLUGIN_DEFINE(
	gatt_my,
	VERSION,
	BLUETOOTH_PLUGIN_PRIORITY_LOW,
	gatt_my_init,
	gatt_my_exit)

