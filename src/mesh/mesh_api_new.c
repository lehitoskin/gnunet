/*
     This file is part of GNUnet.
     (C) 2011 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
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
 * @file mesh/mesh_api_new.c
 * @brief mesh api: client implementation of mesh service
 * @author Bartlomiej Polot
 *
 * STRUCTURE:
 * - CONSTANTS
 * - DATA STRUCTURES
 * - AUXILIARY FUNCTIONS
 * - RECEIVE HANDLERS
 * - SEND FUNCTIONS
 * - API CALL DEFINITIONS
 */

#ifdef __cplusplus

extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif


#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_client_lib.h"
#include "gnunet_util_lib.h"
#include "gnunet_peer_lib.h"
#include "gnunet_mesh_service_new.h"
#include "mesh.h"
#include "mesh_protocol.h"

#define MESH_API_MAX_QUEUE 10

/******************************************************************************/
/************************      DATA STRUCTURES     ****************************/
/******************************************************************************/

/**
 * Transmission queue to the service
 */
struct GNUNET_MESH_TransmitHandle
{
    /**
     * Double Linked list
     */
  struct GNUNET_MESH_TransmitHandle *next;

    /**
     * Double Linked list
     */
  struct GNUNET_MESH_TransmitHandle *prev;

    /**
     * Data itself, currently points to the end of this struct if 
     * we have a message already, NULL if the message is to be 
     * obtained from the callback.
     */
  const struct GNUNET_MessageHeader *data;

  /**
   * Tunnel this message is sent over (may be NULL for control messages).
   */
  struct GNUNET_MESH_Tunnel *tunnel;

  /**
   * Callback to obtain the message to transmit, or NULL if we
   * got the message in 'data'.  Notice that messages built
   * by 'notify' need to be encapsulated with information about
   * the 'target'.
   */
  GNUNET_CONNECTION_TransmitReadyNotify notify;

  /**
   * Closure for 'notify'
   */
  void *notify_cls;

  /**
   * Priority of the message.  The queue is sorted by priority,
   * control messages have the maximum priority (UINT32_MAX).
   */
  uint32_t priority;
  
  /**
   * How long is this message valid.  Once the timeout has been
   * reached, the message must no longer be sent.  If this 
   * is a message with a 'notify' callback set, the 'notify'
   * function should be called with 'buf' NULL and size 0.
   */
  struct GNUNET_TIME_Absolute timeout;
 
  /**
   * Target of the message, 0 for broadcast.  This field
   * is only valid if 'notify' is non-NULL.
   */
  GNUNET_PEER_Id target;
                                 
  /**
   * Size of 'data' -- or the desired size of 'notify' if 'data' is NULL.
   */
  size_t size;
};


/**
 * Opaque handle to the service.
 */
struct GNUNET_MESH_Handle
{
    /**
     * Handle to the server connection, to send messages later
     */
  struct GNUNET_CLIENT_Connection *client;

    /**
     * Set of handlers used for processing incoming messages in the tunnels
     */
  const struct GNUNET_MESH_MessageHandler *message_handlers;

    /**
     * Set of applications that should be claimed to be offered at this node.
     * Note that this is just informative, the appropiate handlers must be
     * registered independently and the mapping is up to the developer of the
     * client application.
     */
  const GNUNET_MESH_ApplicationType *applications; 

    /**
     * Double linked list of the tunnels this client is connected to.
     */
  struct GNUNET_MESH_Tunnel *tunnels_head;
  struct GNUNET_MESH_Tunnel *tunnels_tail;

    /**
     * Callback for tunnel disconnection
     */
  GNUNET_MESH_TunnelEndHandler *cleaner;

    /**
     * Handle to cancel pending transmissions in case of disconnection
     */
  struct GNUNET_CLIENT_TransmitHandle *th;

    /**
     * Closure for all the handlers given by the client
     */
  void *cls;

    /**
     * Messages to send to the service
     */
  struct GNUNET_MESH_TransmitHandle *queue_head;
  struct GNUNET_MESH_TransmitHandle *queue_tail;

    /**
     * tid of the next tunnel to create (to avoid reusing IDs often)
     */
  MESH_TunnelNumber next_tid;

  unsigned int n_handlers;

  unsigned int n_applications;

  unsigned int max_queue_size;

  /**
   * Have we started the task to receive messages from the service
   * yet? We do this after we send the 'MESH_LOCAL_CONNECT' message.
   */
  int in_receive;
};

/**
 * Opaque handle to a tunnel.
 */
struct GNUNET_MESH_Tunnel
{

    /**
     * DLL
     */
  struct GNUNET_MESH_Tunnel *next;
  struct GNUNET_MESH_Tunnel *prev;

    /**
     * Callback to execute when peers connect to the tunnel
     */
  GNUNET_MESH_TunnelConnectHandler connect_handler;

    /**
     * Callback to execute when peers disconnect to the tunnel
     */
  GNUNET_MESH_TunnelDisconnectHandler disconnect_handler;

    /**
     * All peers added to the tunnel
     */
  GNUNET_PEER_Id *peers;

    /**
     * Closure for the connect/disconnect handlers
     */
  void *cls;

    /**
     * Handle to the mesh this tunnel belongs to
     */
  struct GNUNET_MESH_Handle *mesh;

    /**
     * Local ID of the tunnel
     */
  MESH_TunnelNumber tid;

    /**
     * Owner of the tunnel
     */
  GNUNET_PEER_Id owner;

    /**
     * Number of peer added to the tunnel
     */
  unsigned int npeers;
};


/******************************************************************************/
/***********************     AUXILIARY FUNCTIONS      *************************/
/******************************************************************************/

/**
 * Get the tunnel handler for the tunnel specified by id from the given handle
 * @param h Mesh handle
 * @param tid ID of the wanted tunnel
 * @return handle to the required tunnel or NULL if not found
 */
static struct GNUNET_MESH_Tunnel *
retrieve_tunnel (struct GNUNET_MESH_Handle *h, MESH_TunnelNumber tid)
{
  struct GNUNET_MESH_Tunnel *t;

  t = h->tunnels_head;
  while (t != NULL)
  {
    if (t->tid == tid)
      return t;
    t = t->next;
  }
  return NULL;
}

/**
 * Get the length of the transmission queue
 * @param h mesh handle whose queue is to be measured
 */
static unsigned int
get_queue_length (struct GNUNET_MESH_Handle *h)
{
  struct GNUNET_MESH_TransmitHandle *q;
  unsigned int i;

  /* count */
  for (q = h->queue_head, i = 0; NULL != q; q = q->next, i++) ;

  return i;
}


/******************************************************************************/
/***********************      RECEIVE HANDLERS     ****************************/
/******************************************************************************/

/**
 * Process the new tunnel notification and add it to the tunnels in the handle
 *
 * @param h     The mesh handle
 * @param msg   A message with the details of the new incoming tunnel
 */
static void
process_tunnel_create (struct GNUNET_MESH_Handle *h,
                       const struct GNUNET_MESH_TunnelMessage *msg)
{
  struct GNUNET_MESH_Tunnel *t;
  MESH_TunnelNumber tid;

  tid = ntohl (msg->tunnel_id);
  if (tid >= GNUNET_MESH_LOCAL_TUNNEL_ID_MARK)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                "MESH: received an incoming tunnel with tid in local range (%X)\n",
                tid);
    GNUNET_break_op (0);
    return;                     //FIXME abort? reconnect?
  }
  t = GNUNET_malloc (sizeof (struct GNUNET_MESH_Tunnel));
  t->cls = h->cls;
  t->mesh = h;
  t->tid = tid;

  return;
}


/**
 * Process the new peer event and notify the upper level of it
 *
 * @param h     The mesh handle
 * @param msg   A message with the details of the peer event
 */
static void
process_peer_event (struct GNUNET_MESH_Handle *h,
                    const struct GNUNET_MESH_PeerControl *msg)
{
  struct GNUNET_MESH_Tunnel *t;
  uint16_t size;

  size = ntohs (msg->header.size);
  if (size != sizeof (struct GNUNET_MESH_PeerControl))
  {
    GNUNET_break_op (0);
    return;
  }
  t = retrieve_tunnel (h, ntohl (msg->tunnel_id));
  if (NULL == t)
  {
    GNUNET_break_op (0);
    return;
  }
  if (GNUNET_MESSAGE_TYPE_MESH_LOCAL_PEER_CONNECTED == msg->header.type)
  {
    if (NULL != t->connect_handler)
    {
      t->connect_handler (t->cls, &msg->peer, NULL);    /* FIXME atsi */
    }
  }
  else
  {
    if (NULL != t->disconnect_handler)
    {
      t->disconnect_handler (t->cls, &msg->peer);
    }
  }
}


/**
 * Process the incoming data packets
 *
 * @param h     The mesh handle
 * @param msh   A message encapsulating the data
 */
static void
process_incoming_data (struct GNUNET_MESH_Handle *h,
                       const struct GNUNET_MessageHeader *message)
{
  const struct GNUNET_MessageHeader *payload;
  const struct GNUNET_MESH_MessageHandler *handler;
  const struct GNUNET_PeerIdentity *peer;
  struct GNUNET_MESH_Unicast *ucast;
  struct GNUNET_MESH_Multicast *mcast;
  struct GNUNET_MESH_ToOrigin *to_orig;
  struct GNUNET_MESH_Tunnel *t;
  uint16_t type;
  int i;

  type = ntohs (message->type);
  switch (type)
  {
  case GNUNET_MESSAGE_TYPE_MESH_UNICAST:
    ucast = (struct GNUNET_MESH_Unicast *) message;
    t = retrieve_tunnel (h, ntohl (ucast->tid));
    payload = (struct GNUNET_MessageHeader *) &ucast[1];
    peer = &ucast->oid;
    break;
  case GNUNET_MESSAGE_TYPE_MESH_MULTICAST:
    mcast = (struct GNUNET_MESH_Multicast *) message;
    t = retrieve_tunnel (h, ntohl (mcast->tid));
    payload = (struct GNUNET_MessageHeader *) &mcast[1];
    peer = &mcast->oid;
    break;
  case GNUNET_MESSAGE_TYPE_MESH_TO_ORIGIN:
    to_orig = (struct GNUNET_MESH_ToOrigin *) message;
    t = retrieve_tunnel (h, ntohl (to_orig->tid));
    payload = (struct GNUNET_MessageHeader *) &to_orig[1];
    peer = &to_orig->sender;
    break;
  default:
    GNUNET_break_op (0);
    return;
  }
  if (NULL == t)
  {
    GNUNET_break_op (0);
    return;
  }
  for (i = 0; i < h->n_handlers; i++)
  {
    handler = &h->message_handlers[i];
    if (handler->type == type)
    {
      if (GNUNET_OK == handler->callback (h->cls, t, NULL,      /* FIXME ctx */
                                          peer, payload, NULL)) /* FIXME atsi */
      {
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                    "MESH: callback completed successfully\n");
      }
      else
      {
        GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
                    "MESH: callback caused disconnection\n");
        GNUNET_MESH_disconnect (h);
      }
    }
  }
}


/**
 * Function to process all messages received from the service
 *
 * @param cls closure
 * @param msg message received, NULL on timeout or fatal error
 */
static void
msg_received (void *cls, const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_MESH_Handle *h = cls;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: received a message from MESH\n");
  if (msg == NULL)
  {
    GNUNET_break (0);
    h->in_receive = GNUNET_NO;
    // rather: do_reconnect () -- and set 'in_receive' to NO there...
    // FIXME: service disconnect, handle!
    return;
  }

  switch (ntohs (msg->type))
  {
    /* Notify of a new incoming tunnel */
  case GNUNET_MESSAGE_TYPE_MESH_LOCAL_TUNNEL_CREATE:
    process_tunnel_create (h, (struct GNUNET_MESH_TunnelMessage *) msg);
    break;
    /* Notify of a new peer or a peer disconnect in the tunnel */
  case GNUNET_MESSAGE_TYPE_MESH_LOCAL_PEER_CONNECTED:
  case GNUNET_MESSAGE_TYPE_MESH_LOCAL_PEER_DISCONNECTED:
    process_peer_event (h, (struct GNUNET_MESH_PeerControl *) msg);
    break;
    /* Notify of a new data packet in the tunnel */
  case GNUNET_MESSAGE_TYPE_MESH_UNICAST:
  case GNUNET_MESSAGE_TYPE_MESH_MULTICAST:
  case GNUNET_MESSAGE_TYPE_MESH_TO_ORIGIN:
    process_incoming_data (h, msg);
    break;
    /* We shouldn't get any other packages, log and ignore */
  default:
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "MESH: unsolicited message form service (type %d)\n",
                ntohs (msg->type));
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: message processed\n");
  GNUNET_CLIENT_receive (h->client, &msg_received, h,
                         GNUNET_TIME_UNIT_FOREVER_REL);
}


/******************************************************************************/
/************************       SEND FUNCTIONS     ****************************/
/******************************************************************************/

/**
 * Function called to send a message to the service.
 * "buf" will be NULL and "size" zero if the socket was closed for writing in
 * the meantime.
 *
 * @param cls closure, the mesh handle
 * @param size number of bytes available in buf
 * @param buf where the callee should write the connect message
 * @return number of bytes written to buf
 */
static size_t
send_raw (void *cls, size_t size, void *buf)
{
  struct GNUNET_MESH_Handle *h = cls;
  struct GNUNET_MESH_TransmitHandle *q;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: Send packet() Buffer %u\n", size);
  h->th = NULL;
  if (0 == size || NULL == buf)
  {
    // FIXME: disconnect, reconnect, retry?
    // do_reconnect ();
    return 0;
  }
  q = h->queue_head;
  if (sizeof (struct GNUNET_MessageHeader) > size)
  {
    GNUNET_break (0);
    GNUNET_assert (sizeof (struct GNUNET_MessageHeader) > ntohs (q->data->size));
    h->th =
        GNUNET_CLIENT_notify_transmit_ready (h->client, q->size,
                                             GNUNET_TIME_UNIT_FOREVER_REL,
                                             GNUNET_YES, &send_raw, h);
    return 0;
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh:   type: %i\n",
              ntohs (q->data->type));
  if (NULL == q->data)
    {
      GNUNET_assert (NULL != q->notify);
      if (q->target == 0)
	{
	  /* multicast */
	  struct GNUNET_MESH_Multicast mc; 
	  char *cbuf;

	  GNUNET_assert (size >= sizeof (mc) + q->size);
	  cbuf = buf;
	  q->size = q->notify (q->notify_cls,
			       size - sizeof (mc), 
			       &cbuf[sizeof(mc)]);
	  if (q->size == 0)
	    {
	      size = 0;	      
	    }
	  else
	    {
	      mc.header.size = htons (sizeof (mc) + q->size);
	      mc.header.type = htons (GNUNET_MESSAGE_TYPE_MESH_MULTICAST);
	      mc.tid = htonl (q->tunnel->tid);
	      memset (&mc.oid, 0, sizeof (struct GNUNET_PeerIdentity)); /* myself */
	      memcpy (buf, &mc, sizeof (mc));
	      size = q->size + sizeof (mc);
	    }
	}
      else
	{
	  /* unicast */
	  struct GNUNET_MESH_Unicast uc; 
	  char *cbuf;

	  GNUNET_assert (size >= sizeof (uc) + q->size);
	  cbuf = buf;
	  q->size = q->notify (q->notify_cls,
			       size - sizeof (uc), 
			       &cbuf[sizeof(uc)]);
	  if (q->size == 0)
	    {
	      size = 0;	      
	    }
	  else
	    {
	      uc.header.size = htons (sizeof (uc) + q->size);
	      uc.header.type = htons (GNUNET_MESSAGE_TYPE_MESH_UNICAST);
	      uc.tid = htonl (q->tunnel->tid);
	      memset (&uc.oid, 0, sizeof (struct GNUNET_PeerIdentity)); /* myself */
	      GNUNET_PEER_resolve (q->target, &uc.destination);
	      memcpy (buf, &uc, sizeof (uc));
	      size = q->size + sizeof (uc);
	    }	  
	}
    }
  else
    {
      memcpy (buf, q->data, q->size);
      size = q->size;
    }
  GNUNET_CONTAINER_DLL_remove (h->queue_head, h->queue_tail, q);
  GNUNET_free (q);
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh:   size: %u\n", size);

  if (NULL != h->queue_head)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh:   next size: %u\n",
                h->queue_head->size);
    h->th =
        GNUNET_CLIENT_notify_transmit_ready (h->client, h->queue_head->size,
                                             GNUNET_TIME_UNIT_FOREVER_REL,
                                             GNUNET_YES, &send_raw, h);
  }
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: Send packet() END\n");
  if (GNUNET_NO == h->in_receive)
    {
      h->in_receive = GNUNET_YES;
      GNUNET_CLIENT_receive (h->client, &msg_received, h,
			     GNUNET_TIME_UNIT_FOREVER_REL);
    }
  return size;
}


/**
 * Add a transmit handle to the transmission queue (by priority).
 * Also manage timeout.
 *
 * @param h mesh handle with the queue head and tail
 * @param q handle to add
 */
static void
queue_transmit_handle (struct GNUNET_MESH_Handle *h,
		       struct GNUNET_MESH_TransmitHandle *q)
{
  struct GNUNET_MESH_TransmitHandle *p;

  p = h->queue_head;
  while ( (NULL != p) && (q->priority < p->priority) )
    p = p->next;
  GNUNET_CONTAINER_DLL_insert_after (h->queue_head, h->queue_tail, p->prev, q);
}


/**
 * Auxiliary function to send a packet to the service
 * Takes care of creating a new queue element and calling the tmt_rdy function
 * if necessary.
 * @param h mesh handle
 * @param msg message to transmit
 */
static void
send_packet (struct GNUNET_MESH_Handle *h, 
	     const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_MESH_TransmitHandle *q;
  size_t msize;

  msize = ntohs (msg->size);
  q = GNUNET_malloc (sizeof (struct GNUNET_MESH_TransmitHandle) + msize);
  q->priority = UINT32_MAX;
  q->timeout = GNUNET_TIME_UNIT_FOREVER_ABS;  
  q->size = msize;
  q->data = (void*) &q[1];
  memcpy (&q[1], msg, msize);
  queue_transmit_handle (h, q);
  if (NULL != h->th)
    return;
  h->th =
    GNUNET_CLIENT_notify_transmit_ready (h->client, msize,
					 GNUNET_TIME_UNIT_FOREVER_REL,
					 GNUNET_YES, &send_raw, h);
}

/******************************************************************************/
/**********************      API CALL DEFINITIONS     *************************/
/******************************************************************************/

/**
 * Connect to the mesh service.
 *
 * @param cfg configuration to use
 * @param cls closure for the various callbacks that follow
 *            (including handlers in the handlers array)
 * @param cleaner function called when an *inbound* tunnel is destroyed
 * @param handlers callbacks for messages we care about, NULL-terminated
 *                 note that the mesh is allowed to drop notifications about
 *                 inbound messages if the client does not process them fast
 *                 enough (for this notification type, a bounded queue is used)
 * @param stypes Application Types the client claims to offer
 * @return handle to the mesh service
 *         NULL on error (in this case, init is never called)
 */
struct GNUNET_MESH_Handle *
GNUNET_MESH_connect (const struct GNUNET_CONFIGURATION_Handle *cfg, void *cls,
                     GNUNET_MESH_TunnelEndHandler cleaner,
                     const struct GNUNET_MESH_MessageHandler *handlers,
                     const GNUNET_MESH_ApplicationType *stypes)
{
  struct GNUNET_MESH_Handle *h;
  struct GNUNET_MESH_ClientConnect *msg;
  GNUNET_MESH_ApplicationType *apps;
  uint16_t napps;
  uint16_t *types;
  uint16_t ntypes;
  size_t size;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: GNUNET_MESH_connect()\n");
  h = GNUNET_malloc (sizeof (struct GNUNET_MESH_Handle));
  h->max_queue_size = MESH_API_MAX_QUEUE; /* FIXME: add to arguments to 'GNUNET_MESH_connect' */
  h->cleaner = cleaner;
  h->client = GNUNET_CLIENT_connect ("mesh", cfg);
  if (h->client == NULL)
  {
    GNUNET_break (0);
    GNUNET_free (h);
    return NULL;
  }

  h->cls = cls;
  h->message_handlers = handlers;
  h->applications = stypes;
  h->next_tid = 0x80000000;

  /* count handlers and apps, calculate size */
  for (h->n_handlers = 0; handlers[h->n_handlers].type; h->n_handlers++) ;
  for (h->n_applications = 0; stypes[h->n_applications]; h->n_applications++) ;

  size = sizeof (struct GNUNET_MESH_ClientConnect);
  size += h->n_handlers * sizeof (uint16_t);
  size += h->n_applications * sizeof (GNUNET_MESH_ApplicationType);

  {
    char buf[size];

    /* build connection packet */
    msg = (struct GNUNET_MESH_ClientConnect *) buf;
    msg->header.type = htons (GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT);
    msg->header.size = htons (size);
    types = (uint16_t *) & msg[1];
    for (ntypes = 0; ntypes < h->n_handlers; ntypes++)
      types[ntypes] = h->message_handlers[ntypes].type;      
    apps = (GNUNET_MESH_ApplicationType *) &types[ntypes];
    for (napps = 0; napps < h->n_applications; napps++)
      apps[napps] = h->applications[napps];      
    msg->applications = htons (napps);
    msg->types = htons (ntypes);

    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
		"mesh: Sending %lu bytes long message %d types and %d apps\n",
		ntohs (msg->header.size), ntypes, napps);
    
    send_packet (h, &msg->header);
  }

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: GNUNET_MESH_connect() END\n");

  return h;
}


/**
 * Disconnect from the mesh service.
 *
 * @param handle connection to mesh to disconnect
 */
void
GNUNET_MESH_disconnect (struct GNUNET_MESH_Handle *handle)
{
  if (NULL != handle->th)
  {
    GNUNET_CLIENT_notify_transmit_ready_cancel (handle->th);
  }
  if (NULL != handle->client)
  {
    GNUNET_CLIENT_disconnect (handle->client, GNUNET_NO);
  }
  GNUNET_free (handle);
}


/**
 * Create a new tunnel (we're initiator and will be allowed to add/remove peers
 * and to broadcast).
 *
 * @param h mesh handle
 * @param connect_handler function to call when peers are actually connected
 * @param disconnect_handler function to call when peers are disconnected
 * @param handler_cls closure for connect/disconnect handlers
 */
struct GNUNET_MESH_Tunnel *
GNUNET_MESH_tunnel_create (struct GNUNET_MESH_Handle *h,
                           GNUNET_MESH_TunnelConnectHandler connect_handler,
                           GNUNET_MESH_TunnelDisconnectHandler
                           disconnect_handler, void *handler_cls)
{
  struct GNUNET_MESH_Tunnel *t;
  struct GNUNET_MESH_TunnelMessage msg;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: Creating new tunnel\n");
  t = GNUNET_malloc (sizeof (struct GNUNET_MESH_Tunnel));

  t->connect_handler = connect_handler;
  t->disconnect_handler = disconnect_handler;
  t->cls = handler_cls;
  t->mesh = h;
  t->tid = h->next_tid++;
  h->next_tid |= GNUNET_MESH_LOCAL_TUNNEL_ID_MARK;      // keep in range

  msg.header.type = htons (GNUNET_MESSAGE_TYPE_MESH_LOCAL_TUNNEL_CREATE);
  msg.header.size = htons (sizeof (struct GNUNET_MESH_TunnelMessage));
  msg.tunnel_id = htonl (t->tid);
  send_packet (h, &msg.header);
  return t;
}


/**
 * Destroy an existing tunnel.
 *
 * @param tun tunnel handle
 */
void
GNUNET_MESH_tunnel_destroy (struct GNUNET_MESH_Tunnel *tun)
{
  struct GNUNET_MESH_Handle *h;
  struct GNUNET_MESH_TunnelMessage *msg;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "mesh: Destroying tunnel\n");

  h = tun->mesh;
  msg = GNUNET_malloc (sizeof (struct GNUNET_MESH_TunnelMessage));
  msg->header.type = htons (GNUNET_MESSAGE_TYPE_MESH_LOCAL_TUNNEL_DESTROY);
  msg->header.size = htons (sizeof (struct GNUNET_MESH_TunnelMessage));
  msg->tunnel_id = htonl (tun->tid);

  GNUNET_free (tun);

  send_packet (h, &msg->header);
}


/**
 * Request that a peer should be added to the tunnel.  The existing
 * connect handler will be called ONCE with either success or failure.
 *
 * @param tunnel handle to existing tunnel
 * @param timeout how long to try to establish a connection
 * @param peer peer to add
 */
void
GNUNET_MESH_peer_request_connect_add (struct GNUNET_MESH_Tunnel *tunnel,
                                      struct GNUNET_TIME_Relative timeout,
                                      const struct GNUNET_PeerIdentity *peer)
{
  struct GNUNET_MESH_PeerControl *msg;
  GNUNET_PEER_Id peer_id;
  unsigned int i;
  
  peer_id = GNUNET_PEER_intern (peer);
  for (i = 0; i < tunnel->npeers; i++)
  {
    if (tunnel->peers[i] == peer_id)
    {
      GNUNET_PEER_change_rc (peer_id, -1);
      return;
    }
  }
  tunnel->npeers++;
  tunnel->peers =
      GNUNET_realloc (tunnel->peers, tunnel->npeers * sizeof (GNUNET_PEER_Id));
  tunnel->peers[tunnel->npeers - 1] = peer_id;

  msg = GNUNET_malloc (sizeof (struct GNUNET_MESH_PeerControl));
  msg->header.size = htons (sizeof (struct GNUNET_MESH_PeerControl));
  msg->header.type = htons (GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT_PEER_ADD);
  msg->tunnel_id = htonl (tunnel->tid);
  memcpy (&msg->peer, peer, sizeof (struct GNUNET_PeerIdentity));

  send_packet (tunnel->mesh, &msg->header);

//   tunnel->connect_handler (tunnel->cls, peer, NULL); FIXME call this later
//   TODO: remember timeout
  return;
}


/**
 * Request that a peer should be removed from the tunnel.  The existing
 * disconnect handler will be called ONCE if we were connected.
 *
 * @param tunnel handle to existing tunnel
 * @param peer peer to remove
 */
void
GNUNET_MESH_peer_request_connect_del (struct GNUNET_MESH_Tunnel *tunnel,
                                      const struct GNUNET_PeerIdentity *peer)
{
  struct GNUNET_MESH_PeerControl msg;
  GNUNET_PEER_Id peer_id;
  unsigned int i;

  peer_id = GNUNET_PEER_search (peer);
  if (0 == peer_id)
    {
      GNUNET_break (0);
      return;
    }
  for (i = 0; i < tunnel->npeers; i++)
    if (tunnel->peers[i] == peer_id)
      break;
  if (i == tunnel->npeers)
    {
      GNUNET_break (0);
      return;
    }
  GNUNET_PEER_change_rc (peer_id, -1);
  tunnel->peers[i] = tunnel->peers[tunnel->npeers-1];
  GNUNET_array_grow (tunnel->peers,
		     tunnel->npeers,
		     tunnel->npeers - 1);
  msg.header.size = htons (sizeof (struct GNUNET_MESH_PeerControl));
  msg.header.type = htons (GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT_PEER_DEL);
  msg.tunnel_id = htonl (tunnel->tid);
  memcpy (&msg.peer, peer, sizeof (struct GNUNET_PeerIdentity));
  send_packet (tunnel->mesh, &msg.header);
}


/**
 * Request that the mesh should try to connect to a peer supporting the given
 * message type.
 *
 * @param tunnel handle to existing tunnel
 * @param timeout how long to try to establish a connection
 * @param app_type application type that must be supported by the peer (MESH
 *                 should discover peer in proximity handling this type)
 */
void
GNUNET_MESH_peer_request_connect_by_type (struct GNUNET_MESH_Tunnel *tunnel,
                                          struct GNUNET_TIME_Relative timeout,
                                          GNUNET_MESH_ApplicationType app_type)
{
  struct GNUNET_MESH_ConnectPeerByType msg;

  msg.header.size = htons (sizeof (struct GNUNET_MESH_ConnectPeerByType));
  msg.header.type =  htons (GNUNET_MESSAGE_TYPE_MESH_LOCAL_CONNECT_PEER_BY_TYPE);
  msg.tunnel_id = htonl (tunnel->tid);
  msg.type = htonl (app_type);
  send_packet (tunnel->mesh, &msg.header);
  //   TODO: remember timeout
}


/**
 * Ask the mesh to call "notify" once it is ready to transmit the
 * given number of bytes to the specified "target".  If we are not yet
 * connected to the specified peer, a call to this function will cause
 * us to try to establish a connection.
 *
 * @param tunnel tunnel to use for transmission
 * @param cork is corking allowed for this transmission?
 * @param priority how important is the message?
 * @param maxdelay how long can the message wait?
 * @param target destination for the message,
 *               NULL for multicast to all tunnel targets
 * @param notify_size how many bytes of buffer space does notify want?
 * @param notify function to call when buffer space is available;
 *        will be called with NULL on timeout or if the overall queue
 *        for this peer is larger than queue_size and this is currently
 *        the message with the lowest priority
 * @param notify_cls closure for notify
 * @return non-NULL if the notify callback was queued,
 *         NULL if we can not even queue the request (insufficient
 *         memory); if NULL is returned, "notify" will NOT be called.
 */
struct GNUNET_MESH_TransmitHandle *
GNUNET_MESH_notify_transmit_ready (struct GNUNET_MESH_Tunnel *tunnel, int cork,
                                   uint32_t priority,
                                   struct GNUNET_TIME_Relative maxdelay,
                                   const struct GNUNET_PeerIdentity *target,
                                   size_t notify_size,
                                   GNUNET_CONNECTION_TransmitReadyNotify notify,
                                   void *notify_cls)
{
  struct GNUNET_MESH_TransmitHandle *q;

  if (get_queue_length (tunnel->mesh) >= tunnel->mesh->max_queue_size)
    return NULL; /* queue full */

  q = GNUNET_malloc (sizeof (struct GNUNET_MESH_TransmitHandle));
  q->tunnel = tunnel;
  q->priority = priority;
  q->timeout = GNUNET_TIME_relative_to_absolute (maxdelay);
  q->target = GNUNET_PEER_intern (target);
  q->size = notify_size;
  q->notify = notify;
  q->notify_cls = notify_cls;
  queue_transmit_handle (tunnel->mesh, q);
  return q;
}


/**
 * Cancel the specified transmission-ready notification.
 *
 * @param th handle that was returned by "notify_transmit_ready".
 */
void
GNUNET_MESH_notify_transmit_ready_cancel (struct GNUNET_MESH_TransmitHandle *th)
{
  GNUNET_CONTAINER_DLL_remove (th->tunnel->mesh->queue_head, 
			       th->tunnel->mesh->queue_tail,
                               th);
  GNUNET_free (th);
}


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif
