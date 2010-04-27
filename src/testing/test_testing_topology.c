/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/
/**
 * @file testing/test_testing_topology.c
 * @brief base testcase for testing all the topologies provided
 */
#include "platform.h"
#include "gnunet_testing_lib.h"
#include "gnunet_core_service.h"

#define VERBOSE GNUNET_YES

/**
 * How long until we fail the whole testcase?
 */
#define TEST_TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 600)

/**
 * How long until we give up on connecting the peers?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 60)

#define DEFAULT_NUM_PEERS 4

#define MAX_OUTSTANDING_CONNECTIONS 300

static float fail_percentage = 0.05;

static int ok;

static unsigned long long num_peers;

static unsigned int total_connections;

static unsigned int failed_connections;

static unsigned int total_server_connections;

static unsigned int total_messages_received;

static unsigned int expected_messages;

static unsigned int expected_connections;

static unsigned long long peers_left;

static struct GNUNET_TESTING_PeerGroup *pg;

static struct GNUNET_SCHEDULER_Handle *sched;

const struct GNUNET_CONFIGURATION_Handle *main_cfg;

GNUNET_SCHEDULER_TaskIdentifier die_task;

static char *dotOutFileName = "topology.dot";

static FILE *dotOutFile;

static char *topology_string;

static int transmit_ready_scheduled;

static int transmit_ready_failed;

static int transmit_ready_called;

#define MTYPE 12345

struct GNUNET_TestMessage
{
  /**
   * Header of the message
   */
  struct GNUNET_MessageHeader header;

  /**
   * Unique identifier for this message.
   */
  uint32_t uid;
};

struct TestMessageContext
{
  /* This is a linked list */
  struct TestMessageContext *next;

  /* Handle to the sending peer core */
  struct GNUNET_CORE_Handle *peer1handle;

  /* Handle to the receiving peer core */
  struct GNUNET_CORE_Handle *peer2handle;

  /* Handle to the sending peer daemon */
  struct GNUNET_TESTING_Daemon *peer1;

  /* Handle to the receiving peer daemon */
  struct GNUNET_TESTING_Daemon *peer2;

  /* Identifier for this message, so we don't disconnect other peers! */
  uint32_t uid;

};

static struct TestMessageContext *test_messages;

static void
finish_testing ()
{
  GNUNET_assert (pg != NULL);
  struct TestMessageContext *pos;
  struct TestMessageContext *free_pos;
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Called finish testing, stopping daemons.\n");
#endif
  int count;
  count = 0;
  pos = test_messages;
  while (pos != NULL)
    {
      if (pos->peer1handle != NULL)
        {
          GNUNET_CORE_disconnect(pos->peer1handle);
          pos->peer1handle = NULL;
        }
      if (pos->peer2handle != NULL)
        {
          GNUNET_CORE_disconnect(pos->peer2handle);
          pos->peer2handle = NULL;
        }
      free_pos = pos;
      pos = pos->next;
      GNUNET_free(free_pos);
    }
#if VERBOSE
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "transmit_ready's scheduled %d, failed %d, transmit_ready's called %d\n", transmit_ready_scheduled, transmit_ready_failed, transmit_ready_called);
#endif
  sleep(1);
#if VERBOSE
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "Calling daemons_stop\n");
#endif
  GNUNET_TESTING_daemons_stop (pg);
#if VERBOSE
          GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                      "daemons_stop finished\n");
#endif
  if (dotOutFile != NULL)
    {
      fprintf(dotOutFile, "}");
      fclose(dotOutFile);
    }

  ok = 0;
}


static void
disconnect_cores (void *cls, const struct GNUNET_SCHEDULER_TaskContext * tc)
{
  struct TestMessageContext *pos = cls;

  /* Disconnect from the respective cores */
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Disconnecting from peer 1 `%4s'\n", GNUNET_i2s (&pos->peer1->id));
#endif
  if (pos->peer1handle != NULL)
    GNUNET_CORE_disconnect(pos->peer1handle);
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Disconnecting from peer 2 `%4s'\n", GNUNET_i2s (&pos->peer2->id));
#endif
  if (pos->peer2handle != NULL)
    GNUNET_CORE_disconnect(pos->peer2handle);
  /* Set handles to NULL so test case can be ended properly */
  pos->peer1handle = NULL;
  pos->peer2handle = NULL;
  /* Decrement total connections so new can be established */
  total_server_connections -= 2;
}

static int
process_mtype (void *cls,
               const struct GNUNET_PeerIdentity *peer,
               const struct GNUNET_MessageHeader *message,
               struct GNUNET_TIME_Relative latency,
               uint32_t distance)
{
  struct TestMessageContext *pos = cls;
  struct GNUNET_TestMessage *msg = (struct GNUNET_TestMessage *)message;
  if (pos->uid != ntohl(msg->uid))
    return GNUNET_OK;

  total_messages_received++;
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Received message from `%4s', type %d.\n", GNUNET_i2s (peer), ntohs(message->type));
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Total messages received %d, expected %d.\n", total_messages_received, expected_messages);
#endif

  if (total_messages_received == expected_messages)
    {
      GNUNET_SCHEDULER_cancel (sched, die_task);
      GNUNET_SCHEDULER_add_now (sched, &finish_testing, NULL);
    }
  else
    {
      GNUNET_SCHEDULER_add_now(sched, &disconnect_cores, pos);
    }

  return GNUNET_OK;
}

static void
end_badly (void *cls, const struct GNUNET_SCHEDULER_TaskContext * tc)
{
  char *msg = cls;
  GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
              "End badly was called (%s)... stopping daemons.\n", msg);
  struct TestMessageContext *pos;
  struct TestMessageContext *free_pos;

  pos = test_messages;
  while (pos != NULL)
    {
      if (pos->peer1handle != NULL)
        {
          GNUNET_CORE_disconnect(pos->peer1handle);
          pos->peer1handle = NULL;
        }
      if (pos->peer2handle != NULL)
        {
          GNUNET_CORE_disconnect(pos->peer2handle);
          pos->peer2handle = NULL;
        }
      free_pos = pos;
      pos = pos->next;
      GNUNET_free(free_pos);
    }

  if (pg != NULL)
    {
      GNUNET_TESTING_daemons_stop (pg);
      ok = 7331;                /* Opposite of leet */
    }
  else
    ok = 401;                   /* Never got peers started */

  if (dotOutFile != NULL)
    {
      fprintf(dotOutFile, "}");
      fclose(dotOutFile);
    }
}



static size_t
transmit_ready (void *cls, size_t size, void *buf)
{
  struct GNUNET_TestMessage *m;
  struct TestMessageContext *pos = cls;

  GNUNET_assert (buf != NULL);
  m = (struct GNUNET_TestMessage *) buf;
  m->header.type = htons (MTYPE);
  m->header.size = htons (sizeof (struct GNUNET_TestMessage));
  m->uid = htonl(pos->uid);
  transmit_ready_called++;
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "transmit ready for peer %s\ntransmit_ready's scheduled %d, transmit_ready's called %d\n", GNUNET_i2s(&pos->peer1->id), transmit_ready_scheduled, transmit_ready_called);
#endif
  return sizeof (struct GNUNET_TestMessage);
}


static struct GNUNET_CORE_MessageHandler no_handlers[] = {
  {NULL, 0, 0}
};

static struct GNUNET_CORE_MessageHandler handlers[] = {
  {&process_mtype, MTYPE, sizeof (struct GNUNET_TestMessage)},
  {NULL, 0, 0}
};

static void
init_notify_peer2 (void *cls,
             struct GNUNET_CORE_Handle *server,
             const struct GNUNET_PeerIdentity *my_identity,
             const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded *publicKey)
{
  struct TestMessageContext *pos = cls;

#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Core connection to `%4s' established, scheduling message send\n",
              GNUNET_i2s (my_identity));
#endif
  total_server_connections++;

  if (NULL == GNUNET_CORE_notify_transmit_ready (pos->peer1handle,
                                                 0,
                                                 TIMEOUT,
                                                 &pos->peer2->id,
                                                 sizeof (struct GNUNET_TestMessage),
                                                 &transmit_ready, pos))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "RECEIVED NULL when asking core (1) for transmission to peer `%4s'\n",
                  GNUNET_i2s (&pos->peer2->id));
      transmit_ready_failed++;
    }
  else
    {
      transmit_ready_scheduled++;
    }
}


static void
init_notify_peer1 (void *cls,
             struct GNUNET_CORE_Handle *server,
             const struct GNUNET_PeerIdentity *my_identity,
             const struct GNUNET_CRYPTO_RsaPublicKeyBinaryEncoded *publicKey)
{
  struct TestMessageContext *pos = cls;
  total_server_connections++;

#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Core connection to `%4s' established, setting up handles\n",
              GNUNET_i2s (my_identity));
#endif

  /*
   * Connect to the receiving peer
   */
  pos->peer2handle = GNUNET_CORE_connect (sched,
                       pos->peer2->cfg,
                       TIMEOUT,
                       pos,
                       &init_notify_peer2,
                       NULL,
                       NULL,
                       NULL,
                       GNUNET_YES, NULL, GNUNET_YES, handlers);

}


static void
send_test_messages (void *cls, const struct GNUNET_SCHEDULER_TaskContext * tc)
{
  struct TestMessageContext *pos = cls;

  if ((tc->reason == GNUNET_SCHEDULER_REASON_SHUTDOWN) || (cls == NULL))
    return;

  if (die_task == GNUNET_SCHEDULER_NO_TASK)
    {
      die_task = GNUNET_SCHEDULER_add_delayed (sched,
                                               TEST_TIMEOUT,
                                               &end_badly, "from create topology (timeout)");
    }

  if (total_server_connections >= MAX_OUTSTANDING_CONNECTIONS)
    {
      GNUNET_SCHEDULER_add_delayed (sched, GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 1),
                                    &send_test_messages, pos);
      return; /* Otherwise we'll double schedule messages here! */
    }

  /*
   * Connect to the sending peer
   */
  pos->peer1handle = GNUNET_CORE_connect (sched,
                                          pos->peer1->cfg,
                                          TIMEOUT,
                                          pos,
                                          &init_notify_peer1,
                                          NULL,
                                          NULL,
                                          NULL,
                                          GNUNET_NO, NULL, GNUNET_NO, no_handlers);

  GNUNET_assert(pos->peer1handle != NULL);

  if (total_server_connections < MAX_OUTSTANDING_CONNECTIONS)
    {
      GNUNET_SCHEDULER_add_now (sched,
                                &send_test_messages, pos->next);
    }
  else
    {
      GNUNET_SCHEDULER_add_delayed (sched, GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 1),
                                    &send_test_messages, pos->next);
    }
}


void
topology_callback (void *cls,
                   const struct GNUNET_PeerIdentity *first,
                   const struct GNUNET_PeerIdentity *second,
                   const struct GNUNET_CONFIGURATION_Handle *first_cfg,
                   const struct GNUNET_CONFIGURATION_Handle *second_cfg,
                   struct GNUNET_TESTING_Daemon *first_daemon,
                   struct GNUNET_TESTING_Daemon *second_daemon,
                   const char *emsg)
{
  struct TestMessageContext *temp_context;
  if (emsg == NULL)
    {
      total_connections++;
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "connected peer %s to peer %s\n",
               first_daemon->shortname,
               second_daemon->shortname);
#endif
      temp_context = GNUNET_malloc(sizeof(struct TestMessageContext));
      temp_context->peer1 = first_daemon;
      temp_context->peer2 = second_daemon;
      temp_context->next = test_messages;
      temp_context->uid = total_connections;
      test_messages = temp_context;

      expected_messages++;
      if (dotOutFile != NULL)
        fprintf(dotOutFile, "\tn%s -- n%s;\n", first_daemon->shortname, second_daemon->shortname);
    }
#if VERBOSE
  else
    {
      failed_connections++;
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Failed to connect peer %s to peer %s with error :\n%s\n",
               first_daemon->shortname,
               second_daemon->shortname, emsg);
    }
#endif

  if (total_connections == expected_connections)
    {
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Created %d total connections, which is our target number!  Calling send messages.\n",
                  total_connections);
#endif

      GNUNET_SCHEDULER_cancel (sched, die_task);
      die_task = GNUNET_SCHEDULER_NO_TASK;
      GNUNET_SCHEDULER_add_delayed (sched, GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 1), &send_test_messages, test_messages);
    }
  else if (total_connections + failed_connections == expected_connections)
    {
      if (failed_connections < (unsigned int)(fail_percentage * total_connections))
        {
          GNUNET_SCHEDULER_cancel (sched, die_task);
          die_task = GNUNET_SCHEDULER_NO_TASK;
          GNUNET_SCHEDULER_add_delayed (sched, GNUNET_TIME_relative_multiply(GNUNET_TIME_UNIT_SECONDS, 1), &send_test_messages, test_messages);
        }
      else
        {
          GNUNET_SCHEDULER_cancel (sched, die_task);
          die_task = GNUNET_SCHEDULER_add_now (sched,
                                               &end_badly, "from topology_callback (too many failed connections)");
        }
    }
  else
    {
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Have %d total connections, %d failed connections, Want %d (at least %d)\n",
                  total_connections, failed_connections, expected_connections, expected_connections - (unsigned int)(fail_percentage * expected_connections));
#endif
    }
}


static void
create_topology ()
{
  expected_connections = -1;
  if ((pg != NULL) && (peers_left == 0))
    {
      /* create_topology will read the topology information from
         the config already contained in the peer group, so should
         we have create_topology called from start peers?  I think
         maybe this way is best so that the client can know both
         when peers are started, and when they are connected.
       */
      expected_connections = GNUNET_TESTING_create_topology (pg);
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "Have %d expected connections\n", expected_connections);
#endif
    }

  GNUNET_SCHEDULER_cancel (sched, die_task);
  if (expected_connections == GNUNET_SYSERR)
    {
      die_task = GNUNET_SCHEDULER_add_now (sched,
                                           &end_badly, "from create topology (bad return)");
    }
  die_task = GNUNET_SCHEDULER_add_delayed (sched,
                                           TEST_TIMEOUT,
                                           &end_badly, "from create topology (timeout)");
}


static void
my_cb (void *cls,
       const struct GNUNET_PeerIdentity *id,
       const struct GNUNET_CONFIGURATION_Handle *cfg,
       struct GNUNET_TESTING_Daemon *d, const char *emsg)
{
  GNUNET_assert (id != NULL);
#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Started daemon %llu out of %llu\n",
              (num_peers - peers_left) + 1, num_peers);
#endif
  peers_left--;
  if (peers_left == 0)
    {
#if VERBOSE
      GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                  "All %d daemons started, now creating topology!\n",
                  num_peers);
#endif
      GNUNET_SCHEDULER_cancel (sched, die_task);
      /* Set up task in case topology creation doesn't finish
       * within a reasonable amount of time */
      die_task = GNUNET_SCHEDULER_add_delayed (sched,
                                               GNUNET_TIME_relative_multiply
                                               (GNUNET_TIME_UNIT_MINUTES, 5),
                                               &end_badly, "from my_cb");
      create_topology ();
      ok = 0;
    }
}


static void
run (void *cls,
     struct GNUNET_SCHEDULER_Handle *s,
     char *const *args,
     const char *cfgfile, const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  sched = s;
  ok = 1;

  dotOutFile = fopen (dotOutFileName, "w");
  if (dotOutFile != NULL)
    {
      fprintf (dotOutFile, "strict graph G {\n");
    }

#if VERBOSE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Starting daemons based on config file %s\n", cfgfile);
#endif
  if (GNUNET_SYSERR ==
      GNUNET_CONFIGURATION_get_value_number (cfg, "testing", "num_peers",
                                             &num_peers))
    num_peers = DEFAULT_NUM_PEERS;

  main_cfg = cfg;

  peers_left = num_peers;

  /* Set up a task to end testing if peer start fails */
  die_task = GNUNET_SCHEDULER_add_delayed (sched,
                                           GNUNET_TIME_relative_multiply
                                           (GNUNET_TIME_UNIT_MINUTES, 5),
                                           &end_badly, "didn't start all daemons in reasonable amount of time!!!");

  pg = GNUNET_TESTING_daemons_start (sched, cfg,
                                     peers_left, &my_cb, NULL,
                                     &topology_callback, NULL, NULL);

}

static int
check ()
{
  char *binary_name;
  char *config_file_name;
  GNUNET_asprintf(&binary_name, "test-testing-topology-%s", topology_string);
  GNUNET_asprintf(&config_file_name, "test_testing_data_topology_%s.conf", topology_string);
  char *const argv[] = {binary_name,
    "-c",
    config_file_name,
#if VERBOSE
    "-L", "DEBUG",
#endif
    NULL
  };
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  GNUNET_PROGRAM_run ((sizeof (argv) / sizeof (char *)) - 1,
                      argv, binary_name, "nohelp",
                      options, &run, &ok);
  GNUNET_free(binary_name);
  GNUNET_free(config_file_name);
  return ok;
}

int
main (int argc, char *argv[])
{
  int ret;
  char *binary_start_pos;
  char *our_binary_name;

  binary_start_pos = rindex(argv[0], '/');
  topology_string = strstr (binary_start_pos,
			    "_topology");
  GNUNET_assert (topology_string != NULL);
  topology_string++;
  topology_string = strstr (topology_string, "_");
  GNUNET_assert (topology_string != NULL);
  topology_string++;

  GNUNET_asprintf(&our_binary_name, "test-testing-topology_%s", topology_string);
  GNUNET_log_setup (our_binary_name,
#if VERBOSE
                    "DEBUG",
#else
                    "WARNING",
#endif
                    NULL);
  ret = check ();

  /**
   * Need to remove base directory, subdirectories taken care
   * of by the testing framework.
   */
  GNUNET_DISK_directory_remove ("/tmp/test-gnunet-testing");
  GNUNET_free(our_binary_name);
  return ret;
}

/* end of test_testing_group.c */
