/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-folder-utils : Utility for camel folders */


/* 
 *
 * Author : 
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include "camel-folder-utils.h"
#include "camel-mime-message.h"



/*  Active Message List utilities */

/* */
static gint
camel_mime_message_number_cmp (gconstpointer a, gconstpointer b)
{
	CamelMimeMessage *m_a = CAMEL_MIME_MESSAGE (a);
	CamelMimeMessage *m_b = CAMEL_MIME_MESSAGE (b);

	return (m_a->message_number - (m_b->message_number));
}


/**
 * camel_aml_expunge_messages: Expunge the message marked as deleted in an Active Message List
 * @aml: active message list
 * @folder: folder object
 * 
 * Expunge the message flagged as "DELETED" in an active message list. 
 * The messages are not freed nor really expunged on the disk, they
 * are just removed from the active message list and marked as 
 * "EXPUNGED". The list of the message which have been expunged is
 * return in a GList which must be freed by the caller. 
 * To be really expunged the providers must provide or call
 * folder specific methods.
 * 
 * Return value: the list of expunged messages.
 **/
static GList *
camel_aml_expunge_messages (GList *aml, 
			    CamelFolder *folder)
{
	CamelMimeMessage *message = NULL;
	GList *message_node = NULL;
	GList *next_message_node = NULL;
	GList *expunged_messages = NULL;
	

	message_node = aml;
	/* look in folder message list which messages
	 * need to be expunged  */
	while ( message_node) {
		message = CAMEL_MIME_MESSAGE (message_node->data);

		/* we may free message_node so get the next node now */
		next_message_node = message_node->next;

		if (message) {			
			if (camel_mime_message_get_flag (message, "DELETED")) {
				
				/* remove the message from active message list */
				g_list_remove_link (aml, message_node);
				g_list_free_1 (message_node);
				camel_mime_message_set_flag (message, "EXPUNGED", TRUE);
				expunged_messages = g_list_prepend (expunged_messages, message);
				
			} 
		} else {
			g_warning ("CamelFolder::expunge warning message_node "
				   "contains no message\n");
		}
		message_node = next_message_node;
	}
	
	return expunged_messages;
}
