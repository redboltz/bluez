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

/* FIXME: Not defined by SIG? UUID128? */
#define MY_SVC_UUID		0xDEAD
#define MY_STATE_UUID		0xBEEF


struct gatt_my_adapter {
	struct btd_adapter	*adapter;
	GSList			*sdp_handles;
};

static GSList *adapters = NULL;

static void gatt_my_adapter_free(struct gatt_my_adapter *gadapter)
{
	while (gadapter->sdp_handles != NULL) {
		uint32_t handle = GPOINTER_TO_UINT(gadapter->sdp_handles->data);

		attrib_free_sdp(gadapter->adapter, handle);
		gadapter->sdp_handles = g_slist_remove(gadapter->sdp_handles,
						gadapter->sdp_handles->data);
	}

	if (gadapter->adapter != NULL)
		btd_adapter_unref(gadapter->adapter);

	g_free(gadapter);
}

static int adapter_cmp(gconstpointer a, gconstpointer b)
{
	const struct gatt_my_adapter *gatt_adapter = a;
	const struct btd_adapter *adapter = b;

	if (gatt_adapter->adapter == adapter)
		return 0;

	return -1;
}

#define MY_STATE_MAX 100
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
	e = attrib_db_update(adapter, a->handle, NULL, g_buf, sizeof(g_buf), NULL);
	printf("%d\n",e);
	return 0;
}

static gboolean register_my_service(struct btd_adapter *adapter)
{
	bt_uuid_t uuid;

	bt_uuid16_create(&uuid, MY_SVC_UUID);

	return gatt_service_add(
		adapter,
		GATT_PRIM_SVC_UUID,
		&uuid,
		/* my characteristic */
		GATT_OPT_CHR_UUID16,
		MY_STATE_UUID,
		GATT_OPT_CHR_PROPS,
		ATT_CHAR_PROPER_READ | ATT_CHAR_PROPER_WRITE | ATT_CHAR_PROPER_NOTIFY,
		GATT_OPT_CHR_VALUE_CB,
		ATTRIB_READ,
		my_state_read,
		adapter,
		GATT_OPT_CHR_VALUE_CB,
		ATTRIB_WRITE,
		my_state_write,
		adapter,
		GATT_OPT_INVALID);
}

static int gatt_my_adapter_probe(struct btd_adapter *adapter)
{
	struct gatt_my_adapter *gadapter;

	gadapter = g_new0(struct gatt_my_adapter, 1);
	gadapter->adapter = btd_adapter_ref(adapter);

	if (!register_my_service(adapter)) {
		DBG("Myservice could not be registered");
		gatt_my_adapter_free(gadapter);
		return -EIO;
	}

	return 0;
}

static void gatt_my_adapter_remove(struct btd_adapter *adapter)
{
	struct gatt_my_adapter *gadapter;
	GSList *l;

	l = g_slist_find_custom(adapters, adapter, adapter_cmp);
	if (l == NULL)
		return;

	gadapter = l->data;
	adapters = g_slist_remove(adapters, gadapter);
	gatt_my_adapter_free(gadapter);
}

static struct btd_adapter_driver gatt_my_adapter_driver = {
	.name	= "gatt-my-adapter-driver",
	.probe	= gatt_my_adapter_probe,
	.remove	= gatt_my_adapter_remove,
};

static int gatt_my_init(void)
{
	return btd_register_adapter_driver(&gatt_my_adapter_driver);
}

static void gatt_my_exit(void)
{
	btd_unregister_adapter_driver(&gatt_my_adapter_driver);
}

BLUETOOTH_PLUGIN_DEFINE(gatt_my, VERSION, BLUETOOTH_PLUGIN_PRIORITY_LOW,
					gatt_my_init, gatt_my_exit)

