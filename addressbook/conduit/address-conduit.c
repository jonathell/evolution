/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution addressbook - Address Conduit
 *
 * Copyright (C) 1998 Free Software Foundation
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Authors: Eskil Heyn Olsen <deity@eskil.dk> 
 *          JP Rosevear <jpr@helixcode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <errno.h>

#include <liboaf/liboaf.h>
#include <bonobo.h>
#include <gnome-xml/parser.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-file.h>
#include <pi-dlp.h>
#include <pi-version.h>
#include <ebook/e-book.h>
#include <ebook/e-card-types.h>
#include <ebook/e-card-cursor.h>
#include <ebook/e-card.h>
#include <ebook/e-card-simple.h>

#define ADDR_CONFIG_LOAD 1
#define ADDR_CONFIG_DESTROY 1
#include <address-conduit-config.h>
#undef ADDR_CONFIG_LOAD
#undef ADDR_CONFIG_DESTROY

#include <address-conduit.h>

GnomePilotConduit * conduit_get_gpilot_conduit (guint32);
void conduit_destroy_gpilot_conduit (GnomePilotConduit*);

#define CONDUIT_VERSION "0.1.0"
#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "eaddrconduit"

#define DEBUG_CONDUIT 1
/* #undef DEBUG_CONDUIT */

#ifdef DEBUG_CONDUIT
#define LOG(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)
#else
#define LOG(e...)
#endif 

#define WARN(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, e)
#define INFO(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)

typedef struct {
	EBookStatus status;
	char *id;
} add_card_cons;

typedef enum {
	CARD_ADDED,
	CARD_MODIFIED,
	CARD_DELETED
} CardObjectChangeType;

typedef struct 
{
	char *uid;
	CardObjectChangeType type;
} CardObjectChange;

/* Debug routines */
static char *
print_local (EAddrLocalRecord *local)
{
	static char buff[ 4096 ];

	if (local == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	if (local->addr) {
		sprintf (buff, "['%s' '%s' '%s']",
			 local->addr->entry[entryLastname],
			 local->addr->entry[entryFirstname],
			 local->addr->entry[entryCompany]);
		return buff;
	}

	return "";
}

static char *print_remote (GnomePilotRecord *remote)
{
	static char buff[ 4096 ];
	struct Address addr;

	if (remote == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	memset (&addr, 0, sizeof (struct Address));
	unpack_Address (&addr, remote->record, remote->length);

	sprintf (buff, "['%s' '%s' '%s']",
		 addr.entry[entryLastname],
		 addr.entry[entryFirstname],
		 addr.entry[entryCompany]);

	return buff;
}

/* Context Routines */
static void
e_addr_context_new (EAddrConduitContext **ctxt, guint32 pilot_id) 
{
	*ctxt = g_new0 (EAddrConduitContext,1);
	g_assert (ctxt!=NULL);

	addrconduit_load_configuration (&(*ctxt)->cfg, pilot_id);
}

static void
e_addr_context_destroy (EAddrConduitContext **ctxt)
{
	g_return_if_fail (ctxt!=NULL);
	g_return_if_fail (*ctxt!=NULL);

	if ((*ctxt)->cfg != NULL)
		addrconduit_destroy_configuration (&(*ctxt)->cfg);

	g_free (*ctxt);
	*ctxt = NULL;
}

/* Addressbok Server routines */
static void
add_card_cb (EBook *ebook, EBookStatus status, const char *id, gpointer closure)
{
	add_card_cons *cons = (add_card_cons*)closure;

	cons->status = status;
	cons->id = g_strdup (id);

	gtk_main_quit();
}

static void
status_cb (EBook *ebook, EBookStatus status, gpointer closure)
{
	(*(EBookStatus*)closure) = status;
	gtk_main_quit();
}

static void
cursor_cb (EBook *book, EBookStatus status, ECardCursor *cursor, gpointer closure)
{
	EAddrConduitContext *ctxt = (EAddrConduitContext*)closure;

	if (status == E_BOOK_STATUS_SUCCESS) {
		long length;
		int i;

		ctxt->address_load_success = TRUE;

		length = e_card_cursor_get_length (cursor);
		ctxt->cards = NULL;
		for (i = 0; i < length; i ++)
			ctxt->cards = g_list_append (ctxt->cards, e_card_cursor_get_nth (cursor, i));

		gtk_main_quit(); /* end the sub event loop */
	}
	else {
		WARN (_("Cursor could not be loaded\n"));
		gtk_main_quit(); /* end the sub event loop */
	}
}

static void
book_open_cb (EBook *book, EBookStatus status, gpointer closure)
{
	EAddrConduitContext *ctxt = (EAddrConduitContext*)closure;

	if (status == E_BOOK_STATUS_SUCCESS) {
		e_book_get_cursor (book, "(contains \"full_name\" \"\")", cursor_cb, ctxt);
	} else {
		WARN (_("EBook not loaded\n"));
		gtk_main_quit(); /* end the sub event loop */
	}
}

static int
start_addressbook_server (EAddrConduitContext *ctxt)
{
	gchar *uri, *path;

	g_return_val_if_fail(ctxt!=NULL,-2);

	ctxt->ebook = e_book_new ();

	path = g_concat_dir_and_file (g_get_home_dir (),
				      "evolution/local/Contacts/addressbook.db");
	uri = g_strdup_printf ("file://%s", path);
	g_free (path);

	e_book_load_uri (ctxt->ebook, uri, book_open_cb, ctxt);

	/* run a sub event loop to turn ebook's async loading into a
           synchronous call */
	gtk_main ();

	g_free (uri);

	if (ctxt->address_load_success)
		return 0;

	return -1;
}

/* Utility routines */
static char *
map_name (EAddrConduitContext *ctxt) 
{
	char *filename = NULL;
	
	filename = g_strdup_printf ("%s/evolution/local/Contacts/pilot-map-%d.xml", g_get_home_dir (), ctxt->cfg->pilot_id);

	return filename;
}

static void
compute_status (EAddrConduitContext *ctxt, EAddrLocalRecord *local, const char *uid)
{
	local->local.archived = FALSE;
	local->local.secret = FALSE;
	local->local.attr = GnomePilotRecordNothing;
}

static GnomePilotRecord
local_record_to_pilot_record (EAddrLocalRecord *local,
			      EAddrConduitContext *ctxt)
{
	GnomePilotRecord p;
	
	g_assert (local->addr != NULL );
	
	LOG ("local_record_to_pilot_record\n");

	p.ID = local->local.ID;
	p.category = 0;
	p.attr = local->local.attr;
	p.archived = local->local.archived;
	p.secret = local->local.secret;

	/* Generate pilot record structure */
	p.record = g_new0 (char,0xffff);
	p.length = pack_Address (local->addr, p.record, 0xffff);

	return p;	
}

static void
local_record_from_ecard (EAddrLocalRecord *local, ECard *ecard, EAddrConduitContext *ctxt)
{
	ECardSimple *simple;
	const ECardDeliveryAddress *delivery;
	int phone = entryPhone1;
	int i;
	
	g_return_if_fail (local != NULL);
	g_return_if_fail (ecard != NULL);

	local->ecard = ecard;
	simple = e_card_simple_new (ecard);
	
	local->local.ID = e_pilot_map_lookup_pid (ctxt->map, ecard->id);

	compute_status (ctxt, local, ecard->id);

	local->addr = g_new0 (struct Address, 1);

	if (ecard->name) {
		if (ecard->name->given)
			local->addr->entry[entryFirstname] = strdup (ecard->name->given);
		if (ecard->name->family)
			local->addr->entry[entryLastname] = strdup (ecard->name->family);
		if (ecard->org)
			local->addr->entry[entryCompany] = strdup (ecard->org);
		if (ecard->title)
			local->addr->entry[entryTitle] = strdup (ecard->title);
	}

	delivery = e_card_simple_get_delivery_address (simple, E_CARD_SIMPLE_ADDRESS_ID_HOME);
	if (delivery) {
		local->addr->entry[entryAddress] = strdup (delivery->street);
		local->addr->entry[entryCity] = strdup (delivery->city);
		local->addr->entry[entryState] = strdup (delivery->region);
		local->addr->entry[entryZip] = strdup (delivery->code);
		local->addr->entry[entryCountry] = strdup (delivery->country);
	}
	
	for (i = 0; i <= 7; i++) {
		const char *phone_str = NULL;
		char *phonelabel = ctxt->ai.phoneLabels[i];
		
		if (!strcmp (phonelabel, "E-mail"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_EMAIL);
		else if (!strcmp (phonelabel, "Home"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_HOME);
		else if (!strcmp (phonelabel, "Work"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS);
		else if (!strcmp (phonelabel, "Fax"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_FAX);
		else if (!strcmp (phonelabel, "Other"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_OTHER);
		else if (!strcmp (phonelabel, "Main"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_PRIMARY);
		else if (!strcmp (phonelabel, "Pager"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_PAGER);
		else if (!strcmp (phonelabel, "Mobile"))
			phone_str = e_card_simple_get_const (simple, E_CARD_SIMPLE_FIELD_PHONE_MOBILE);
		
		if (phone_str) {
			local->addr->entry[phone] = strdup (phone_str);
			local->addr->phoneLabel[phone - entryPhone1] = i;
			phone++;
		}
		
	}

	gtk_object_unref (GTK_OBJECT (simple));
}

static void 
local_record_from_uid (EAddrLocalRecord *local,
		       char *uid,
		       EAddrConduitContext *ctxt)
{
	ECard *ecard;

	g_assert(local!=NULL);

	ecard = e_book_get_card (ctxt->ebook, uid);

	if (ecard != NULL) {
		local_record_from_ecard (local, ecard, ctxt);
	} else {
		ecard = e_card_new ("");
		local_record_from_ecard (local, ecard, ctxt);
	}
}

static ECard *
ecard_from_remote_record(EAddrConduitContext *ctxt,
			 GnomePilotRecord *remote,
			 ECard *in_card)
{
	struct Address address;
	ECard *ecard;
	ECardSimple *simple;
	ECardDeliveryAddress delivery;
	char *string;
	char *stringparts[4];
	int i;

	g_return_val_if_fail(remote!=NULL,NULL);
	memset (&address, 0, sizeof (struct Address));
	unpack_Address (&address, remote->record, remote->length);

	if (in_card == NULL) {
		ecard = e_card_new("");
	} else {
		ecard = e_card_duplicate (in_card);
	}
	simple = e_card_simple_new (ecard);

#define get(pilotprop) \
        (address.entry [(pilotprop)])
#define check(pilotprop) \
        (address.entry [(pilotprop)] && *address.entry [(pilotprop)])

	i = 0;
	if (check(entryFirstname))
		stringparts[i++] = get(entryFirstname);
	if (check(entryLastname))
		stringparts[i++] = get(entryLastname);
	stringparts[i] = NULL;
	string = g_strjoinv(" ", stringparts);
	e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_FULL_NAME, string);
	g_free(string);

	if (check (entryTitle))
		e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_TITLE, get (entryTitle));

	if (check (entryCompany))
		e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_ORG, get (entryCompany));

	memset (&delivery, 0, sizeof (ECardDeliveryAddress));
	delivery.flags = E_CARD_ADDR_HOME;
	if (check (entryAddress))
		delivery.street = get (entryAddress);
	if (check (entryCity))
		delivery.city = get (entryCity);
	if (check (entryState))
		delivery.region = get (entryState);
	if (check (entryCountry))
		delivery.country = get (entryCountry);
	if (check (entryZip))
		delivery.code = get (entryZip);
	string = e_card_delivery_address_to_string (&delivery);
	e_card_simple_set (simple,  E_CARD_SIMPLE_FIELD_ADDRESS_HOME, string);
	g_free (string);
	
	for (i = entryPhone1; i <= entryPhone5; i++) {
		char *phonelabel = ctxt->ai.phoneLabels[address.phoneLabel[i - entryPhone1]];

		if (!strcmp (phonelabel, "E-mail"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_EMAIL, address.entry[i] ? address.entry[i] : "");
		else if (!strcmp (phonelabel, "Home"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_HOME,address.entry[i] ? address.entry[i] : "" );
		else if (!strcmp (phonelabel, "Work"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS, address.entry[i] ? address.entry[i] : "");
		else if (!strcmp (phonelabel, "Fax"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_FAX, address.entry[i] ? address.entry[i] : "");
		else if (!strcmp (phonelabel, "Other"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_OTHER, address.entry[i] ? address.entry[i] : "");
		else if (!strcmp (phonelabel, "Main"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_PRIMARY, address.entry[i] ? address.entry[i] : "");
		else if (!strcmp (phonelabel, "Pager"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_PAGER, address.entry[i] ? address.entry[i] : "");
		else if (!strcmp (phonelabel, "Mobile"))
			e_card_simple_set(simple, E_CARD_SIMPLE_FIELD_PHONE_MOBILE, address.entry[i] ? address.entry[i] : "");
	}
#undef get
#undef check

	e_card_simple_sync_card (simple);
	gtk_object_unref(GTK_OBJECT(simple));

	free_Address(&address);

	return ecard;
}

static void
check_for_slow_setting (GnomePilotConduit *c, EAddrConduitContext *ctxt)
{
	int count, map_count;

  	count = g_list_length (ctxt->cards);

	map_count = g_hash_table_size (ctxt->map->pid_map);
	
  	/* If there are no objects or objects but no log */
	if ((count == 0) || (count > 0 && map_count == 0)) {
		GnomePilotConduitStandard *conduit;
		LOG ("    doing slow sync\n");
		conduit = GNOME_PILOT_CONDUIT_STANDARD (c);
		gnome_pilot_conduit_standard_set_slow (conduit);
	} else {
		LOG ("    doing fast sync\n");
	}
}

static void
card_added (EBookView *book_view, const GList *cards, EAddrConduitContext *ctxt)
{
	const GList *l;

	for (l = cards; l != NULL; l = l->next) {
		ECard *card = l->data;
		CardObjectChange *coc = g_new0 (CardObjectChange, 1);
		
		coc->uid = g_strdup (e_card_get_id (card));
		coc->type = CARD_ADDED;

		ctxt->changed = g_list_prepend (ctxt->changed, coc);
		g_hash_table_insert (ctxt->changed_hash, coc->uid, coc);
	}
}

static void
card_changed (EBookView *book_view, const GList *cards, EAddrConduitContext *ctxt)
{
	const GList *l;

	for (l = cards; l != NULL; l = l->next) {
		ECard *card = l->data;
		CardObjectChange *coc = g_new0 (CardObjectChange, 1);
	
		coc->uid = g_strdup (e_card_get_id (card));
		coc->type = CARD_MODIFIED;
		g_print ("UID **** %s\n", coc->uid);
		ctxt->changed = g_list_prepend (ctxt->changed, coc);
		g_hash_table_insert (ctxt->changed_hash, coc->uid, coc);
	}	
}


static void
card_removed (EBookView *book_view, const char *id, EAddrConduitContext *ctxt)
{
	CardObjectChange *coc = g_new0 (CardObjectChange, 1);
	
	coc->uid = g_strdup (id);
	coc->type = CARD_DELETED;

	ctxt->changed = g_list_prepend (ctxt->changed, coc);
	g_hash_table_insert (ctxt->changed_hash, coc->uid, coc);
}

static void
sequence_complete (EBookView *book_view, EAddrConduitContext *ctxt)
{
	gtk_object_unref (GTK_OBJECT (book_view));
  	gtk_main_quit ();
}

static void
view_cb (EBook *book, EBookStatus status, EBookView *book_view, gpointer data)
{
	EAddrConduitContext *ctxt = data;
	
	gtk_object_ref (GTK_OBJECT (book_view));
	
  	gtk_signal_connect (GTK_OBJECT (book_view), "card_added", 
			    (GtkSignalFunc) card_added, ctxt);
	gtk_signal_connect (GTK_OBJECT (book_view), "card_changed", 
			    (GtkSignalFunc) card_changed, ctxt);
	gtk_signal_connect (GTK_OBJECT (book_view), "card_removed", 
			    (GtkSignalFunc) card_removed, ctxt);
  	gtk_signal_connect (GTK_OBJECT (book_view), "sequence_complete", 
			    (GtkSignalFunc) sequence_complete, ctxt);

}

/* Pilot syncing callbacks */
static gint
pre_sync (GnomePilotConduit *conduit,
	  GnomePilotDBInfo *dbi,
	  EAddrConduitContext *ctxt)
{
	GnomePilotConduitSyncAbs *abs_conduit;
/*    	GList *l; */
	int len;
	unsigned char *buf;
	char *filename;
	char *change_id;
/*  	gint num_records; */

	abs_conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit);

	LOG ("---------------------------------------------------------\n");
	LOG ("pre_sync: Addressbook Conduit v.%s", CONDUIT_VERSION);
	g_message ("Addressbook Conduit v.%s", CONDUIT_VERSION);

	ctxt->ebook = NULL;
	
	if (start_addressbook_server (ctxt) != 0) {
		WARN(_("Could not start wombat server"));
		gnome_pilot_conduit_error (conduit, _("Could not start wombat"));
		return -1;
	}

	/* Load the uid <--> pilot id mappings */
	filename = map_name (ctxt);
	e_pilot_map_read (filename, &ctxt->map);
	g_free (filename);

	/* Count and hash the changes */
	change_id = g_strdup_printf ("pilot-sync-evolution-addressbook-%d", ctxt->cfg->pilot_id);
	ctxt->changed_hash = g_hash_table_new (g_str_hash, g_str_equal);
	e_book_get_changes (ctxt->ebook, change_id, view_cb, ctxt);

	/* Force the view loading to be synchronous */
	gtk_main ();
	g_free (change_id);
	
	/* Set the count information */
/*  	num_records = cal_client_get_n_objects (ctxt->client, CALOBJ_TYPE_TODO); */
/*  	gnome_pilot_conduit_sync_abs_set_num_local_records(abs_conduit, num_records); */
/*  	gnome_pilot_conduit_sync_abs_set_num_new_local_records (abs_conduit, add_records); */
/*  	gnome_pilot_conduit_sync_abs_set_num_updated_local_records (abs_conduit, mod_records); */
/*  	gnome_pilot_conduit_sync_abs_set_num_deleted_local_records(abs_conduit, del_records); */

	gtk_object_set_data (GTK_OBJECT (conduit), "dbinfo", dbi);

	buf = (unsigned char*)g_malloc (0xffff);
	len = dlp_ReadAppBlock (dbi->pilot_socket, dbi->db_handle, 0,
			      (unsigned char *)buf, 0xffff);
	
	if (len < 0) {
		WARN (_("Could not read pilot's Address application block"));
		WARN ("dlp_ReadAppBlock(...) = %d", len);
		gnome_pilot_conduit_error (conduit,
					   _("Could not read pilot's Address application block"));
		return -1;
	}
	unpack_AddressAppInfo (&(ctxt->ai), buf, len);
	g_free (buf);

  	check_for_slow_setting (conduit, ctxt);

	return 0;
}

static gint
post_sync (GnomePilotConduit *conduit,
	   GnomePilotDBInfo *dbi,
	   EAddrConduitContext *ctxt)
{
	gchar *filename;
	
	LOG ("post_sync: Address Conduit v.%s", CONDUIT_VERSION);
	LOG ("---------------------------------------------------------\n");

	filename = map_name (ctxt);
	e_pilot_map_write (filename, ctxt->map);
	g_free (filename);
	
	return 0;
}

static gint
set_pilot_id (GnomePilotConduitSyncAbs *conduit,
	      EAddrLocalRecord *local,
	      guint32 ID,
	      EAddrConduitContext *ctxt)
{
	LOG ("set_pilot_id: setting to %d\n", ID);
	
	e_pilot_map_insert (ctxt->map, ID, local->ecard->id, FALSE);

        return 0;
}

static gint
set_status_cleared (GnomePilotConduitSyncAbs *conduit,
		    EAddrLocalRecord *local,
		    EAddrConduitContext *ctxt)
{
	LOG ("set_status_cleared: clearing status\n");
	
        return 0;
}

static gint
for_each (GnomePilotConduitSyncAbs *conduit,
	  EAddrLocalRecord **local,
	  EAddrConduitContext *ctxt)
{
  	static GList *cards, *iterator;
  	static int count;

  	g_return_val_if_fail (local != NULL, -1);

	if (*local == NULL) {
		LOG ("beginning for_each");

		cards = ctxt->cards;
		count = 0;
		
		if (cards != NULL) {
			LOG ("iterating over %d records", g_list_length (cards));

			*local = g_new0 (EAddrLocalRecord, 1);
  			local_record_from_ecard (*local, cards->data, ctxt);

			iterator = cards;
		} else {
			LOG ("no events");
			(*local) = NULL;
			return 0;
		}
	} else {
		count++;
		if (g_list_next (iterator)) {
			iterator = g_list_next (iterator);

			*local = g_new0 (EAddrLocalRecord, 1);
			local_record_from_ecard (*local, iterator->data, ctxt);
		} else {
			LOG ("for_each ending");

  			/* Tell the pilot the iteration is over */
			*local = NULL;

			return 0;
		}
	}

	return 0;
}

static gint
for_each_modified (GnomePilotConduitSyncAbs *conduit,
		   EAddrLocalRecord **local,
		   EAddrConduitContext *ctxt)
{
	static GList *changes, *iterator;
	static int count;

	g_return_val_if_fail (local != NULL, 0);

	if (*local == NULL) {
		LOG ("beginning for_each_modified: beginning\n");
		
		changes = ctxt->changed;
		
		count = 0;
		
		if (changes != NULL) {
			CardObjectChange *coc = changes->data;
			
			LOG ("iterating over %d records", g_list_length (changes));
			 
			*local = g_new0 (EAddrLocalRecord, 1);
			local_record_from_uid (*local, coc->uid, ctxt);

			iterator = changes;
		} else {
			LOG ("no events");
			(*local) = NULL;
			return 0;
		}
	} else {
		count++;
		if (g_list_next (iterator)) {
			CardObjectChange *coc;

			iterator = g_list_next (iterator);
			coc = iterator->data;

			*local = g_new0 (EAddrLocalRecord, 1);
			local_record_from_uid (*local, coc->uid, ctxt);
		} else {
			LOG ("for_each_modified ending");

    			/* Tell the pilot the iteration is over */
			(*local) = NULL;

			return 0;
		}
	}

	return 0;
}

static gint
compare (GnomePilotConduitSyncAbs *conduit,
	 EAddrLocalRecord *local,
	 GnomePilotRecord *remote,
	 EAddrConduitContext *ctxt)
{
	/* used by the quick compare */
	GnomePilotRecord local_pilot;
	int retval = 0;

	LOG ("compare: local=%s remote=%s...\n",
	     print_local (local), print_remote (remote));

	g_return_val_if_fail (local!=NULL,-1);
	g_return_val_if_fail (remote!=NULL,-1);

  	local_pilot = local_record_to_pilot_record (local, ctxt);

	if (remote->length != local_pilot.length
	    || memcmp (local_pilot.record, remote->record, remote->length))
		retval = 1;

	if (retval == 0)
		LOG ("    equal");
	else
		LOG ("    not equal");
	
	return retval;
}

static gint
add_record (GnomePilotConduitSyncAbs *conduit,
	    GnomePilotRecord *remote,
	    EAddrConduitContext *ctxt)
{
	ECard *ecard;
	add_card_cons cons;
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1);

	LOG ("add_record: adding %s to desktop\n", print_remote (remote));

	ecard = ecard_from_remote_record (ctxt, remote, NULL);

	/* add the ecard to the server */
	e_book_add_card (ctxt->ebook, ecard, add_card_cb, &cons);

	gtk_main(); /* enter sub mainloop */
	
	if (cons.status != E_BOOK_STATUS_SUCCESS) {
		WARN ("add_record: failed to add card to ebook\n");
		return -1;
	}

	ctxt->cards = g_list_append (ctxt->cards,
				     e_book_get_card (ctxt->ebook, cons.id));
	g_free (cons.id);

	e_pilot_map_insert (ctxt->map, remote->ID, ecard->id, FALSE);

	return retval;
}

static gint
replace_record (GnomePilotConduitSyncAbs *conduit,
		EAddrLocalRecord *local,
		GnomePilotRecord *remote,
		EAddrConduitContext *ctxt)
{
	ECard *new_ecard;
	EBookStatus commit_status;
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1);

	LOG ("replace_record: replace %s with %s\n",
	     print_local (local), print_remote (remote));

	new_ecard = ecard_from_remote_record (ctxt, remote, local->ecard);
	gtk_object_unref (GTK_OBJECT (local->ecard));
	local->ecard = new_ecard;

	e_book_commit_card (ctxt->ebook, local->ecard, status_cb, &commit_status);
	
	gtk_main (); /* enter sub mainloop */
	
	if (commit_status != E_BOOK_STATUS_SUCCESS)
		WARN ("replace_record: failed to update card in ebook\n");

	gtk_object_unref (GTK_OBJECT (new_ecard));

	return retval;
}

static gint
delete_record (GnomePilotConduitSyncAbs *conduit,
	       EAddrLocalRecord *local,
	       EAddrConduitContext *ctxt)
{
	EBookStatus commit_status;
	int retval = 0;
	
	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (local->ecard != NULL, -1);

	LOG ("delete_record: delete %s\n", print_local (local));

	e_book_remove_card_by_id (ctxt->ebook, local->ecard->id, status_cb, &commit_status);
	
	gtk_main (); /* enter sub mainloop */
	
	if (commit_status != E_BOOK_STATUS_SUCCESS)
		WARN ("delete_record: failed to delete card in ebook\n");

	return retval;
}

static gint
archive_record (GnomePilotConduitSyncAbs *conduit,
		EAddrLocalRecord *local,
		gboolean archive,
		EAddrConduitContext *ctxt)
{
	int retval = 0;
	
	g_return_val_if_fail (local != NULL, -1);

	LOG ("archive_record: %s\n", archive ? "yes" : "no");

	e_pilot_map_insert (ctxt->map, local->local.ID, local->ecard->id, archive);
	
        return retval;
}

static gint
match (GnomePilotConduitSyncAbs *conduit,
       GnomePilotRecord *remote,
       EAddrLocalRecord **local,
       EAddrConduitContext *ctxt)
{
  	char *uid;
	
	LOG ("match: looking for local copy of %s\n",
	     print_remote (remote));	
	
	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (remote != NULL, -1);

	*local = NULL;
	uid = g_hash_table_lookup (ctxt->map->pid_map, &remote->ID);
	
	if (!uid)
		return 0;

	LOG ("  matched\n");
	
	*local = g_new0 (EAddrLocalRecord, 1);
	local_record_from_uid (*local, uid, ctxt);
	
	return 0;
}

static gint
free_match (GnomePilotConduitSyncAbs *conduit,
	    EAddrLocalRecord *local,
	    EAddrConduitContext *ctxt)
{
	LOG ("free_match: freeing\n");

	g_return_val_if_fail (local != NULL, -1);

	gtk_object_unref (GTK_OBJECT (local->ecard));
	g_free (local);

	return 0;
}

static gint
prepare (GnomePilotConduitSyncAbs *conduit,
	 EAddrLocalRecord *local,
	 GnomePilotRecord *remote,
	 EAddrConduitContext *ctxt)
{
	LOG ("prepare: encoding local %s\n", print_local (local));

	*remote = local_record_to_pilot_record (local, ctxt);

	return 0;
}

static ORBit_MessageValidationResult
accept_all_cookies (CORBA_unsigned_long request_id,
		    CORBA_Principal *principal,
		    CORBA_char *operation)
{
	/* allow ALL cookies */
	return ORBIT_MESSAGE_ALLOW_ALL;
}


GnomePilotConduit *
conduit_get_gpilot_conduit (guint32 pilot_id)
{
	GtkObject *retval;
	EAddrConduitContext *ctxt;

	LOG ("in address's conduit_get_gpilot_conduit\n");

	/* we need to find wombat with oaf, so make sure oaf
	   is initialized here.  once the desktop is converted
	   to oaf and gpilotd is built with oaf, this can go away */
	if (!oaf_is_initialized ()) {
		char *argv[ 1 ] = {"hi"};
		oaf_init (1, argv);

		if (bonobo_init (CORBA_OBJECT_NIL,
				 CORBA_OBJECT_NIL,
				 CORBA_OBJECT_NIL) == FALSE)
			g_error (_("Could not initialize Bonobo"));

		ORBit_set_request_validation_handler (accept_all_cookies);
	}

	retval = gnome_pilot_conduit_sync_abs_new ("AddressDB", 0x61646472);
	g_assert (retval != NULL);

	gnome_pilot_conduit_construct (GNOME_PILOT_CONDUIT (retval),
				       "e_addr_conduit");

	e_addr_context_new (&ctxt, pilot_id);
	gtk_object_set_data (GTK_OBJECT (retval), "addrconduit_context", ctxt);

	gtk_signal_connect (retval, "pre_sync", (GtkSignalFunc) pre_sync, ctxt);
	gtk_signal_connect (retval, "post_sync", (GtkSignalFunc) post_sync, ctxt);

  	gtk_signal_connect (retval, "set_pilot_id", (GtkSignalFunc) set_pilot_id, ctxt);
  	gtk_signal_connect (retval, "set_status_cleared", (GtkSignalFunc) set_status_cleared, ctxt);

  	gtk_signal_connect (retval, "for_each", (GtkSignalFunc) for_each, ctxt);
  	gtk_signal_connect (retval, "for_each_modified", (GtkSignalFunc) for_each_modified, ctxt);
  	gtk_signal_connect (retval, "compare", (GtkSignalFunc) compare, ctxt);

  	gtk_signal_connect (retval, "add_record", (GtkSignalFunc) add_record, ctxt);
  	gtk_signal_connect (retval, "replace_record", (GtkSignalFunc) replace_record, ctxt);
  	gtk_signal_connect (retval, "delete_record", (GtkSignalFunc) delete_record, ctxt);
  	gtk_signal_connect (retval, "archive_record", (GtkSignalFunc) archive_record, ctxt);

  	gtk_signal_connect (retval, "match", (GtkSignalFunc) match, ctxt);
  	gtk_signal_connect (retval, "free_match", (GtkSignalFunc) free_match, ctxt);

  	gtk_signal_connect (retval, "prepare", (GtkSignalFunc) prepare, ctxt);

	return GNOME_PILOT_CONDUIT (retval);
}

void
conduit_destroy_gpilot_conduit (GnomePilotConduit *conduit)
{ 
	EAddrConduitContext *ctxt;

	ctxt = gtk_object_get_data (GTK_OBJECT (conduit), 
				    "addrconduit_context");

	e_addr_context_destroy (&ctxt);

	gtk_object_destroy (GTK_OBJECT (conduit));
}
