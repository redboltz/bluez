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
};

static GSList *my_adapters = NULL;

#define MY_STATE_MAX 0x200
uint8_t g_buf[MY_STATE_MAX];

static uint8_t my_state_read(struct attribute *a,
				  struct btd_device *device, gpointer user_data)
{
	struct btd_adapter *adapter = user_data;
	int e;
	printf("read called\n");
	e = attrib_db_update(adapter, a->handle, NULL, g_buf, sizeof(g_buf), NULL);
	printf("%d\n",e);
	return 0;
}

static uint8_t my_state_write(struct attribute *a,
				  struct btd_device *device, gpointer user_data)
{
	struct btd_adapter *adapter = user_data;
	int e;
	size_t i = 0;
	printf("write called\n");
	for (; i < a->len; ++i) {
		printf("%02x", a->data[i]);
		g_buf[i] = a->data[i];
	}
	printf("\n");
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
		GATT_CHR_PROP_READ | GATT_CHR_PROP_WRITE | GATT_CHR_PROP_NOTIFY,

		GATT_OPT_CHR_VALUE_CB,
		ATTRIB_READ,
		my_state_read,
		my_adapter->adapter,

		GATT_OPT_CHR_VALUE_CB,
		ATTRIB_WRITE,
		my_state_write,
		my_adapter->adapter,

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

struct notify_data {
	struct my_adapter *my_adapter;
	uint8_t *value;
	size_t len;
};

struct notify_callback {
	struct notify_data *notify_data;
	struct btd_device *device;
	guint id;
};

static void destroy_notify_callback(guint8 status, const guint8 *pdu, guint16 len,
							gpointer user_data)
{
	struct notify_callback *cb = user_data;

	DBG("status=%#x", status);

	btd_device_remove_attio_callback(cb->device, cb->id);
	btd_device_unref(cb->device);
	g_free(cb->notify_data->value);
	g_free(cb->notify_data);
	g_free(cb);
}

static void attio_connected_cb(GAttrib *attrib, gpointer user_data)
{
	struct notify_callback *cb = user_data;
	struct notify_data *nd = cb->notify_data;
	struct my_adapter *my_adapter = nd->my_adapter;
	size_t len;
	uint8_t *pdu = g_attrib_get_buffer(attrib, &len);
	len = enc_notification(my_adapter->hnd_value,
						   nd->value, nd->len, pdu, len);

	DBG("Send notification for handle: 0x%04x, ccc: 0x%04x",
					my_adapter->hnd_value,
					my_adapter->hnd_ccc);

	g_attrib_send(attrib, 0, pdu, len, destroy_notify_callback, cb, NULL);
}

static gboolean is_notifiable_device(struct btd_device *device, uint16_t ccc)
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
	if (!(val & 0x0001)) {
		result = FALSE;
		goto end;
	}

	result = TRUE;
end:
	g_free(str);
	g_free(filename);
	g_key_file_free(key_file);

	return result;
}

static void filter_devices_notify(struct btd_device *device, void *user_data)
{
	struct notify_data *notify_data = user_data;
	struct my_adapter *my_adapter = notify_data->my_adapter;
	struct notify_callback *cb;

	if (!is_notifiable_device(device, my_adapter->hnd_ccc))
		return;

	cb = g_new0(struct notify_callback, 1);
	cb->notify_data = notify_data;
	cb->device = btd_device_ref(device);
	cb->id = btd_device_add_attio_callback(device,
						attio_connected_cb, NULL, cb);
}


static void notify_devices(struct my_adapter *my_adapter,
			uint8_t *value, size_t len)
{
	struct notify_data *notify_data;

	notify_data = g_new0(struct notify_data, 1);
	notify_data->my_adapter = my_adapter;
	DBG("notify");
	{
	  size_t i = 0;
	  for (; i < len; ++i) {
		printf("%02x ", value[i]);
	  }
	  printf("\n");
	}
	notify_data->value = g_memdup(value, len);
	notify_data->len = len;
	btd_adapter_for_each_device(my_adapter->adapter, filter_devices_notify,
					notify_data);
}

static DBusMessage *new_my(DBusConnection *conn, DBusMessage *msg,
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
		struct btd_adapter *adapter = my_adapter->adapter;
		printf("size:%d\n", size);
		attrib_db_update(adapter, my_adapter->hnd_value, NULL,
						 bytes, size, NULL);
		notify_devices(my_adapter, bytes, size);
		return dbus_message_new_method_return(msg);
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
	{ GDBUS_METHOD("NewMy",
			GDBUS_ARGS(
				   { "bytes", "ay" }), NULL,
				   new_my) },
	{ }
};

static void my_destroy(gpointer user_data)
{
}

static int gatt_my_init(void)
{
  {
	size_t i = 0;
	for (; i < MY_STATE_MAX; ++i) g_buf[i] = i;
  }
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

