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
DDS_DynamicDataReader	dr;



void read_msg (DDS_DataReaderListener *l, DDS_DataReader dr, void (*callback)(const char* msg))
{
	ChatMsg_t		msg;
	DDS_InstanceStateKind	kind;
	int			valid;
	DDS_ReturnCode_t	ret;

	ARG_NOT_USED (l)

	memset (&msg, 0, sizeof (msg));
	ret = ChatMsg_read_or_take (dr, &msg, DDS_NOT_READ_SAMPLE_STATE, 
					      DDS_ANY_VIEW_STATE,
					      DDS_ANY_INSTANCE_STATE, 1,
					      &valid, &kind);
	if (ret == DDS_RETCODE_OK)
		do {
#ifndef DISPLAY_SELF
			if (!strcmp (msg.from, user_name) &&
			    !strcmp (msg.chatroom, chatroom))
				break;
#endif
			if (valid) 
			{
				//printf ("%s: %s\r\n", msg.from, msg.message);
				//printf ("Message: %s\r\n", msg.message);
				callback(msg.message);
			}
			else if (kind == DDS_NOT_ALIVE_DISPOSED_INSTANCE_STATE)
				printf ("%s is busy!\r\n", msg.from);
			else if (kind == DDS_NOT_ALIVE_NO_WRITERS_INSTANCE_STATE)
				printf ("%s has left!\r\n", msg.from);
		}
		while (0);

	ChatMsg_cleanup (&msg);
}


int c_main (void (*callback)(const char* msg))
{

	DDS_DataWriterQos 	wr_qos;
	DDS_DataReaderQos	rd_qos;
	DDS_ReturnCode_t	error;

	sprintf (user_name, ".pid.%u", getpid ());


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

	sub = DDS_DomainParticipant_create_subscriber (part, NULL, NULL, 0); 
	if (!sub) {
		printf ("DDS_DomainParticipant_create_subscriber () returned an error!\r\n");
		exit (1);
	}
	if (verbose)
		printf ("DDS Subscriber created.\r\n");

	DDS_Subscriber_get_default_datareader_qos (sub, &rd_qos);
#ifdef TRANSIENT_LOCAL
	rd_qos.durability.kind = DDS_TRANSIENT_LOCAL_DURABILITY_QOS;
#endif
#ifdef RELIABLE
	rd_qos.reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;
#endif
#ifdef KEEP_ALL
	rd_qos.history.kind = DDS_KEEP_ALL_HISTORY_QOS;
	rd_qos.history.depth = DDS_LENGTH_UNLIMITED;
	rd_qos.resource_limits.max_samples_per_instance = HISTORY;
	rd_qos.resource_limits.max_instances = HISTORY * 10;
	rd_qos.resource_limits.max_samples = HISTORY * 4;
#else
	rd_qos.history.kind = DDS_KEEP_LAST_HISTORY_QOS;
	rd_qos.history.depth = HISTORY;
#endif
	dr = DDS_Subscriber_create_datareader (sub, td, &rd_qos,
#ifndef WAITSETS
			 &msg_listener, DDS_DATA_AVAILABLE_STATUS);
#else
			 NULL, 0);
#endif
	if (!dr) {
		printf ("DDS_DomainParticipant_create_datareader () returned an error!\r\n");
		exit (1);
	}
	if (verbose)
		printf ("DDS Chat message reader created.\r\n");

	DDS_WaitSet		ws;
	DDS_SampleStateMask	ss = DDS_NOT_READ_SAMPLE_STATE;
	DDS_ViewStateMask	vs = DDS_ANY_VIEW_STATE;
	DDS_InstanceStateMask	is = DDS_ANY_INSTANCE_STATE;
	DDS_ReadCondition	rc;
	DDS_ConditionSeq	conds = DDS_SEQ_INITIALIZER (DDS_Condition);
	DDS_Duration_t		to;
	DDS_ReturnCode_t	ret;

	ws = DDS_WaitSet__alloc ();
	if (!ws)
		fatal ("Unable to allocate a WaitSet!");

	if (verbose)
		printf ("DDS Waitset allocated.\r\n");

	rc = DDS_DataReader_create_readcondition (dr, ss, vs, is);
	if (!rc)
		fatal ("DDS_DataReader_create_readcondition () returned an error!");

	if (verbose)
		printf ("DDS Readcondition created.\r\n");

	ret = DDS_WaitSet_attach_condition (ws, rc);
	if (ret)
		fatal ("Unable to attach condition to a WaitSet!");

	while (!aborting) {
		to.sec = 0;
		to.nanosec = 200000000;	/* Timeout after 200ms. */
		ret = DDS_WaitSet_wait (ws, &conds, &to);
		if (ret == DDS_RETCODE_TIMEOUT)
			continue;

		read_msg (NULL, dr, callback);
	}
	ret = DDS_WaitSet_detach_condition (ws, rc);
	if (ret)
		fatal ("Unable to detach condition from WaitSet (%s)!", DDS_error (ret));

	DDS_WaitSet__free (ws);



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

