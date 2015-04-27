/*
 * Copyright (c) 2014 - Qeo LLC
 *
 * The source code form of this Qeo Open Source Project component is subject
 * to the terms of the Clear BSD license.
 *
 * You can redistribute it and/or modify it under the terms of the Clear BSD
 * License (http://directory.fsf.org/wiki/License:ClearBSD). See LICENSE file
 * for more details.
 *
 * The Qeo Open Source Project also includes third party Open Source Software.
 * See LICENSE file for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <poll.h>
#include "thread.h"
#include "libx.h"
#include "tty.h"
#include "chat_msg.h"
#ifdef DDS_SECURITY
#include "dds/dds_security.h"
#ifdef DDS_NATIVE_SECURITY
#include "nsecplug/nsecplug.h"
#else
#include "msecplug/msecplug.h"
#include "assert.h"
#include "../../plugins/secplug/xmlparse.h"
#endif
#include "../../plugins/security/engine_fs.h"
#endif
#include "dds/dds_aux.h"
#include "dds/dds_debug.h"

#define	WAITSETS		/* Set this to use the WaitSet mechanism. */
/*#define TRANSIENT_LOCAL	** Set to use Transient-local Durability. */
/*#define RELIABLE		** Set this for Reliable transfers. */
/*#define KEEP_ALL		** Set this for infinite history. */
#define HISTORY		1	/* # of samples buffered. */
#define	DISPLAY_SELF		/* Define this to display own messages. */

const char		*progname;
char			chatroom [64] = "DDS";		/* Chatroom name. */
char			user_name [64];			/* User name. */
unsigned		domain_id;			/* Domain identifier. */
int			verbose, aborting;
thread_t		rt;
#ifdef DDS_SECURITY
char                    *engine_id;		/* Engine id. */
char                    *cert_path;		/* Certificates path. */
char                    *key_path;		/* Private key path. */
char                    *realm_name;		/* Realm name. */
#endif

DDS_DomainParticipant	part;
DDS_DynamicTypeSupport	ts;
DDS_Publisher		pub;
DDS_Subscriber		sub;
DDS_Topic		topic;
DDS_TopicDescription	td;
DDS_DynamicDataWriter	dw;
DDS_DynamicDataReader	dr;


void do_chat (DDS_DataWriter dw)
{
	ChatMsg_t		m;
	DDS_InstanceHandle_t	h;
	char			buf [256];

	tty_init ();
	DDS_Handle_attach (tty_stdin,
			   POLLIN | POLLPRI | POLLERR | POLLHUP | POLLNVAL,
			   tty_input,
			   NULL);

	printf ("Welcome to the Technicolor chatroom.\r\n");
	printf ("Anything you type will be sent to all chatroom attendees.\r\n");
	printf ("Type '!help' for chatroom options.\r\n");
	m.chatroom = chatroom;
	m.from = user_name;
	h = 0;
	unsigned int counter = 0;
	while (!aborting) {
		sprintf(buf, "%d", counter++);
		h = ChatMsg_register (dw, &m);
		m.message = buf;
		ChatMsg_write (dw, &m, h);
		printf("%s\n", buf);
		usleep(200000);
	}
}

int main (int argc, const char **argv)
{
	DDS_DataWriterQos 	wr_qos;
	DDS_DataReaderQos	rd_qos;
	DDS_ReturnCode_t	error;


	DDS_entity_name ("Technicolor Chatroom");

	part = DDS_DomainParticipantFactory_create_participant (domain_id, NULL, NULL, 0);
	if (!part) {
		printf ("Can't create participant!\r\n");
		exit (1);
	}
	if (verbose)
		printf ("DDS Domain Participant created.\r\n");

	ts = ChatMsg_type_new ();
	if (!ts) {
		printf ("Can't create chat message type!\r\n");
		exit (1);
	}
	error = DDS_DynamicTypeSupport_register_type (ts, part, "ChatMsg");
	if (error) {
		printf ("Can't register chat message type.\r\n");
		exit (1);
	}
	if (verbose)
		printf ("DDS Topic type ('%s') registered.\r\n", "ChatMsg");

	topic = DDS_DomainParticipant_create_topic (part, "chatter", "ChatMsg", NULL, NULL, 0);
	if (!topic) {
		printf ("Can't register chat message type.\r\n");
		exit (1);
	}
	if (verbose)
		printf ("DDS ChatMsg Topic created.\r\n");

	td = DDS_DomainParticipant_lookup_topicdescription (part, "chatter");
	if (!td) {
		printf ("Can't get topicdescription.\r\n");
		exit (1);
	}
	pub = DDS_DomainParticipant_create_publisher (part, NULL, NULL, 0);
	if (!pub) {
		printf ("DDS_DomainParticipant_create_publisher () failed!\r\n");
		exit (1);
	}
	DDS_Publisher_get_default_datawriter_qos (pub, &wr_qos);
#ifdef TRANSIENT_LOCAL
	wr_qos.durability.kind = DDS_TRANSIENT_LOCAL_DURABILITY_QOS;
#endif
#ifdef RELIABLE
	wr_qos.reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;
#endif
#ifdef KEEP_ALL
	wr_qos.history.kind = DDS_KEEP_ALL_HISTORY_QOS;
	wr_qos.history.depth = DDS_LENGTH_UNLIMITED;
	wr_qos.resource_limits.max_samples_per_instance = HISTORY;
	wr_qos.resource_limits.max_instances = HISTORY * 10;
	wr_qos.resource_limits.max_samples = HISTORY * 4;
#else
	wr_qos.history.kind = DDS_KEEP_LAST_HISTORY_QOS;
	wr_qos.history.depth = HISTORY;
#endif
	/* Create a Data Writer. */
	dw = DDS_Publisher_create_datawriter (pub, topic, &wr_qos, NULL, 0);
	if (!dw) {
		printf ("Unable to create chat message writer.\r\n");
		exit (1);
	}
	if (verbose)
		printf ("DDS Chat message writer created.\r\n");
	do_chat(dw);
	DDS_Publisher_delete_datawriter (pub, dw);
	usleep (200000);
	error = DDS_DomainParticipant_delete_contained_entities (part);
	if (verbose)
		printf ("DDS Entities deleted (error = %u).\r\n", error);

	ChatMsg_type_free (ts);
	if (verbose)
		printf ("Chat Type deleted.\r\n");

	error = DDS_DomainParticipantFactory_delete_participant (part);
	if (verbose)
		printf ("DDS Participant deleted (error = %u).\r\n", error);
	return (0);
}

