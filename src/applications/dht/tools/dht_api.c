/*
      This file is part of GNUnet

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
 * @file tools/dht_api.c
 * @brief DHT-module's core API's implementation. 
 * @author Tomi Tukiainen, Christian Grothoff
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "gnunet_dht_lib.h"
#include "gnunet_dht.h"

/**
 * Information for each table that this client is responsible
 * for.
 */
typedef struct {
  /**
   * ID of the table.
   */
  DHT_TableId table;
  /**
   * The socket that was used to join GNUnet to receive
   * requests for this table.
   */
  GNUNET_TCP_SOCKET * sock;
  /**
   * The thread that is processing the requests received
   * from GNUnet on sock.
   */
  PTHREAD_T processor;
  /**
   * The Datastore provided by the client that performs the
   * actual storage operations.
   */
  Blockstore * store;
  /**
   * Did we receive a request to leave the table?
   */
  int leave_request;

  Mutex lock;
} TableList;

/**
 * Connections to GNUnet helt by this module.
 */
static TableList ** tables;

/**
 * Size of the tables array.
 */
static unsigned int tableCount;

/**
 * Lock for access to tables array.
 */
static Mutex lock;

/**
 * Check if the given message is an ACK.  If so,
 * return the status, otherwise SYSERR.
 */
static int checkACK(CS_HEADER * reply) {  
  LOG(LOG_DEBUG,
      "received ACK from gnunetd\n");
  if ( (sizeof(DHT_CS_REPLY_ACK) == ntohs(reply->size)) &&
       (DHT_CS_PROTO_REPLY_ACK == ntohs(reply->type)) ) 
    return ntohl(((DHT_CS_REPLY_ACK*)reply)->status);
  return SYSERR;
}

/**
 * Send an ACK message of the given value to gnunetd.
 */
static int sendAck(GNUNET_TCP_SOCKET * sock,
		   DHT_TableId * table,
		   int value) {
  DHT_CS_REPLY_ACK msg;

  LOG(LOG_DEBUG,
      "sending ACK to gnunetd\n");
  msg.header.size = htons(sizeof(DHT_CS_REPLY_ACK));
  msg.header.type = htons(DHT_CS_PROTO_REPLY_ACK);
  msg.status = htonl(value);
  msg.table = *table;
  return writeToSocket(sock,
		       &msg.header);
}

static int sendAllResults(const HashCode160 * key,
			  const DataContainer * value,
			  void * cls) {
  TableList * list = (TableList*) cls;
  DHT_CS_REPLY_RESULTS * reply;
 
  reply = MALLOC(sizeof(DHT_CS_REPLY_RESULTS) + ntohl(value->size) + sizeof(HashCode160));
  reply->header.size = htons(sizeof(DHT_CS_REPLY_RESULTS) + ntohl(value->size) + sizeof(HashCode160));
  reply->header.type = htons(DHT_CS_PROTO_REPLY_GET);
  reply->totalResults = htonl(1);
  reply->table = list->table;
  reply->key = *key;
  memcpy(&reply->data,
	 value,
	 ntohl(value->size));
  if (OK != writeToSocket(list->sock,
			  &reply->header)) {
    LOG(LOG_WARNING,
	_("Failed to send '%s'.  Closing connection.\n"),
	"DHT_CS_REPLY_RESULTS");
    MUTEX_LOCK(&list->lock);
    releaseClientSocket(list->sock);
    list->sock = NULL;
    MUTEX_UNLOCK(&list->lock);
    FREE(reply);
    return SYSERR;
  }
  FREE(reply);
  return OK;

}

/**
 * Thread that processes requests from gnunetd (by forwarding
 * them to the implementation of list->store).
 */
static void * process_thread(TableList * list) {
  CS_HEADER * buffer;
  CS_HEADER * reply;
  DHT_CS_REQUEST_JOIN req;
  int ok;
  
  req.header.size = htons(sizeof(DHT_CS_REQUEST_JOIN));
  req.header.type = htons(DHT_CS_PROTO_REQUEST_JOIN);  
  req.table = list->table;

  while (list->leave_request == NO) {
    if (list->sock == NULL) {     
      gnunet_util_sleep(500 * cronMILLIS);
      MUTEX_LOCK(&list->lock);
      if (list->leave_request == NO)
	list->sock  = getClientSocket();      
      MUTEX_UNLOCK(&list->lock);
    }
    if (list->sock == NULL)
      continue;

    ok = NO;
    /* send 'join' message via socket! */    
    if (OK == writeToSocket(list->sock,
			    &req.header)) {
      reply = NULL;
      if (OK == readFromSocket(list->sock,
			       &reply)) {
	if (OK == checkACK(reply))
	  ok = YES;
	FREENONNULL(reply);
      }
    }
    if (ok == NO) {
      MUTEX_LOCK(&list->lock);
      releaseClientSocket(list->sock);
      list->sock = NULL;
      MUTEX_UNLOCK(&list->lock);
      continue; /* retry... */
    }

    buffer = NULL;
    while (OK == readFromSocket(list->sock,
				&buffer)) {
      LOG(LOG_DEBUG,
	  "Received message of type %d from gnunetd\n",
	  ntohs(buffer->type));

      switch (ntohs(buffer->type)) {
      case DHT_CS_PROTO_REQUEST_GET: {
	DHT_CS_REQUEST_GET * req;
	int resCount;
	int keyCount;

	if (sizeof(DHT_CS_REQUEST_GET) != ntohs(buffer->size)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (size %d)\n"),
	      "GET",
	      ntohs(buffer->size));
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  FREE(buffer);
	}
	req = (DHT_CS_REQUEST_GET*) buffer;
	if (! equalsHashCode160(&req->table,
				&list->table)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (wrong table)\n"),
	      "GET");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  break;
	}
	
	keyCount = 1 + ( (ntohs(req->header.size) - sizeof(DHT_CS_REQUEST_GET)) / sizeof(HashCode160));
	resCount = list->store->get(list->store->closure,
				    ntohl(req->type),
				    ntohl(req->priority),
				    keyCount,
				    &req->keys,	
				    &sendAllResults,
				    list);
	if ( (resCount != SYSERR) &&
	     (OK != sendAck(list->sock,
			    &list->table,
			    resCount)) ) {
	  LOG(LOG_WARNING,
	      _("Failed to send '%s'.  Closing connection.\n"),
	      "ACK");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	}
	break;
      }
	
	
      case DHT_CS_PROTO_REQUEST_PUT: {
	DHT_CS_REQUEST_PUT * req;
	DataContainer * value;
	
	if (sizeof(DHT_CS_REQUEST_PUT) > ntohs(buffer->size)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (size %d)\n"),
	      "PUT",
	      ntohs(buffer->size));
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  break;
	}
	req = (DHT_CS_REQUEST_PUT*) buffer;
	if (! equalsHashCode160(&req->table,
				&list->table)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (wrong table)\n"),
	      "PUT");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  break;
	}
	value = MALLOC(sizeof(DataContainer) + 
		       ntohs(buffer->size) - sizeof(DHT_CS_REQUEST_PUT));       
	value->size = htonl(sizeof(DataContainer) + 
			    ntohs(buffer->size) - sizeof(DHT_CS_REQUEST_PUT));
	memcpy(&value[1],
	       &req[1],
	       ntohs(buffer->size) - sizeof(DHT_CS_REQUEST_PUT));
	if (OK !=
	    sendAck(list->sock,
		    &req->table,
		    list->store->put(list->store->closure,
				     &req->key,
				     value,
				     ntohl(req->priority)))) {
	  LOG(LOG_ERROR,
	      _("Failed to send '%s'.  Closing connection.\n"),
	      "ACK");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	}
	FREE(value);
	break;
      }


      case DHT_CS_PROTO_REQUEST_REMOVE: {
	DHT_CS_REQUEST_REMOVE * req;
	DataContainer * value;
	
	if (sizeof(DHT_CS_REQUEST_REMOVE) > ntohs(buffer->size)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (size %d)\n"),
	      "REMOVE",
	      ntohs(buffer->size));
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  break;
	}
	req = (DHT_CS_REQUEST_REMOVE*) buffer;
	if (! equalsHashCode160(&req->table,
				&list->table)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (wrong table)\n"),
	      "REMOVE");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  break;
	}

	value = MALLOC(sizeof(DataContainer) + 
		       ntohs(buffer->size) - sizeof(DHT_CS_REQUEST_REMOVE));       
	value->size = htonl(sizeof(DataContainer) + 
			    ntohs(buffer->size) - sizeof(DHT_CS_REQUEST_REMOVE));
	memcpy(&value[1],
	       &req[1],
	       ntohs(buffer->size) - sizeof(DHT_CS_REQUEST_REMOVE));
	if (OK !=
	    sendAck(list->sock,
		    &req->table,
		    list->store->del(list->store->closure,
				     &req->key,
				     value))) {
	  LOG(LOG_ERROR,
	      _("Failed to send '%s'.  Closing connection.\n"),
	      "ACK");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	}
	FREE(value);
	break;
      }
	
      case DHT_CS_PROTO_REQUEST_ITERATE: {
	DHT_CS_REPLY_RESULTS * reply;
	DHT_CS_REQUEST_ITERATE * req;
	int resCount;

	if (sizeof(DHT_CS_REQUEST_ITERATE) != ntohs(buffer->size)) {
	  LOG(LOG_ERROR,
	      _("Received invalid '%s' request (size %d)\n"),
	      "ITERATE",
	      ntohs(buffer->size));
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	  FREE(buffer);
	}
	req = (DHT_CS_REQUEST_ITERATE*) buffer;
	resCount = list->store->iterate(list->store->closure,
					&sendAllResults,
					list);
	if ( (resCount != SYSERR) &&
	     (OK != sendAck(list->sock,
			    &list->table,
			    resCount)) ) {
	  LOG(LOG_WARNING,
	      _("Failed to send '%s'.  Closing connection.\n"),
	      "ACK");
	  MUTEX_LOCK(&list->lock);
	  releaseClientSocket(list->sock);
	  list->sock = NULL;
	  MUTEX_UNLOCK(&list->lock);
	}
	FREE(reply);
	break;
      }


      default:
	LOG(LOG_ERROR,
	    _("Received unknown request type %d at %s:%d\n"),
	    ntohs(buffer->type),
	    __FILE__, __LINE__);
	MUTEX_LOCK(&list->lock);
	releaseClientSocket(list->sock);
	list->sock = NULL;
	MUTEX_UNLOCK(&list->lock);
      } /* end of switch */
      FREE(buffer);
      buffer = NULL;
    }
    MUTEX_LOCK(&list->lock);
    releaseClientSocket(list->sock);
    list->sock = NULL;
    MUTEX_UNLOCK(&list->lock);
  }

  return NULL;
}


/**
 * Join a table (start storing data for the table).  Join
 * fails if the node is already joint with the particular
 * table.
 *
 * @param datastore the storage callbacks to use for the table
 * @param table the ID of the table
 * @param timeout how long to wait for other peers to respond to 
 *   the join request (has no impact on success or failure)
 * @return SYSERR on error, OK on success
 */
int DHT_LIB_join(Blockstore * store,
		 DHT_TableId * table) {
  TableList * list;
  int i;

  MUTEX_LOCK(&lock);
  for (i=0;i<tableCount;i++) 
    if (equalsHashCode160(&tables[i]->table,
			  table)) {
      LOG(LOG_WARNING,
	  _("This client already participates in the given DHT!\n"));
      MUTEX_UNLOCK(&lock);
      return SYSERR;
    }
  list = MALLOC(sizeof(TableList));
  list->table = *table;
  list->store = store;
  list->leave_request = NO;
  list->sock = getClientSocket();
  if (list->sock == NULL) {
    FREE(list);
    MUTEX_UNLOCK(&lock);
    return SYSERR;
  }
  MUTEX_CREATE(&list->lock);
  if (0 != PTHREAD_CREATE(&list->processor,
			  (PThreadMain)&process_thread,
			  list,
			  16 * 1024)) {
    LOG_STRERROR(LOG_ERROR, "pthread_create");
    releaseClientSocket(list->sock);
    MUTEX_DESTROY(&list->lock);
    FREE(list);
    MUTEX_UNLOCK(&lock);
    return SYSERR;
  } 
  GROW(tables,
       tableCount,
       tableCount+1);
  tables[tableCount-1] = list;
  MUTEX_UNLOCK(&lock);
  return OK;
}


/**
 * Leave a table (stop storing data for the table).  Leave
 * fails if the node is not joint with the table.
 *
 * @param datastore the storage callbacks to use for the table
 * @param table the ID of the table
 * @param timeout how long to wait for other peers to respond to 
 *   the leave request (has no impact on success or failure);
 *   but only timeout time is available for migrating data, so
 *   pick this value with caution.
 * @return SYSERR on error, OK on success
 */
int DHT_LIB_leave(DHT_TableId * table,
		  cron_t timeout) {
  TableList * list;
  int i;
  void * unused;
  DHT_CS_REQUEST_LEAVE req;
  CS_HEADER * reply;
  int ret;
  GNUNET_TCP_SOCKET * sock;  
  
  list = NULL;
  MUTEX_LOCK(&lock);
  for (i=0;i<tableCount;i++) {
    if (equalsHashCode160(&tables[i]->table,
			  table)) {
      list = tables[i];
      tables[i] = tables[tableCount-1];
      GROW(tables,
	   tableCount,
	   tableCount-1);
      break;
    }
  }
  MUTEX_UNLOCK(&lock);
  if (list == NULL) {
    LOG(LOG_WARNING,
	_("Cannot leave DHT: table not known!"));
    return SYSERR; /* no such table! */
  }

  list->leave_request = YES;
  /* send LEAVE message! */  
  req.header.size = htons(sizeof(DHT_CS_REQUEST_LEAVE));
  req.header.type = htons(DHT_CS_PROTO_REQUEST_LEAVE);
  req.timeout = htonll(timeout);
  req.table = *table;

  ret = SYSERR;
  sock = getClientSocket();
  if (sock != NULL) {
    if (OK == writeToSocket(sock,
			    &req.header)) {
      reply = NULL;
      if (OK == readFromSocket(sock,
			       &reply)) {
	if (OK == checkACK(reply))
	  ret = OK;	
	else
	  LOG(LOG_WARNING,
	      _("gnunetd signaled error in response to '%s' message\n"),
	      "DHT_CS_REQUEST_LEAVE");      	  
	FREE(reply);
      } else {
	LOG(LOG_WARNING,
	    _("Failed to receive response to '%s' message from gnunetd\n"),
	    "DHT_CS_REQUEST_LEAVE");      
      }
    } else {
      LOG(LOG_WARNING,
	  _("Failed to send '%s' message to gnunetd\n"),
	  "DHT_CS_REQUEST_LEAVE");
    }
    releaseClientSocket(sock);
  }
  MUTEX_LOCK(&list->lock);
  if (list->sock != NULL)
    closeSocketTemporarily(list->sock); /* signal process_thread */
  MUTEX_UNLOCK(&list->lock);
  unused = NULL;
  PTHREAD_JOIN(&list->processor, &unused);
  releaseClientSocket(list->sock);
  MUTEX_DESTROY(&list->lock);
  FREE(list);
  return ret;
}


/**
 * Perform a synchronous GET operation on the DHT identified by
 * 'table' using 'key' as the key; store the result in 'result'.  If
 * result->dataLength == 0 the result size is unlimited and
 * result->data needs to be allocated; otherwise result->data refers
 * to dataLength bytes and the result is to be stored at that
 * location; dataLength is to be set to the actual size of the
 * result.
 *
 * The peer does not have to be part of the table!
 *
 * @param table table to use for the lookup
 * @param key the key to look up  
 * @param timeout how long to wait until this operation should
 *        automatically time-out
 * @param maxResults maximum number of results to obtain, size of the results array
 * @param results where to store the results (on success)
 * @return number of results on success, SYSERR on error (i.e. timeout)
 */
int DHT_LIB_get(const DHT_TableId * table,
		unsigned int type,
		unsigned int prio,
		unsigned int keyCount,
		HashCode160 * keys,
		cron_t timeout,
		DataProcessor processor,
		void * closure) {
  GNUNET_TCP_SOCKET * sock;
  DHT_CS_REQUEST_GET * req;
  DHT_CS_REPLY_RESULTS * res;
  CS_HEADER * reply;
  int ret;
  int i;
  unsigned int size;

  sock = getClientSocket();
  if (sock == NULL)
    return SYSERR;

  req = MALLOC(sizeof(DHT_CS_REQUEST_GET) + 
	       (keyCount-1) * sizeof(HashCode160));
  req->header.size = htons(sizeof(DHT_CS_REQUEST_GET) +
			   (keyCount-1) * sizeof(HashCode160));
  req->header.type = htons(DHT_CS_PROTO_REQUEST_GET);
  req->type = htonl(type);
  req->timeout = htonll(timeout);
  req->table = *table;
  req->priority = htonl(prio);
  memcpy(&req->keys,
	 keys,
	 keyCount * sizeof(HashCode160));
  if (OK != writeToSocket(sock,
			  &req->header)) {
    releaseClientSocket(sock);
    return SYSERR;
  }
  FREE(req);
  reply = NULL;
  if (OK != readFromSocket(sock,
			   &reply)) {
    releaseClientSocket(sock);
    return SYSERR;
  }

  /* FIXME here! */

  if ( (sizeof(DHT_CS_REPLY_ACK) == ntohs(reply->size)) &&
       (DHT_CS_PROTO_REPLY_ACK == ntohs(reply->type)) ) {
    releaseClientSocket(sock);
    ret = checkACK(reply);
    FREE(reply);
    return ret;
  }
  if ( (sizeof(DHT_CS_REPLY_RESULTS) > ntohs(reply->size)) ||
       (DHT_CS_PROTO_REPLY_GET != ntohs(reply->type)) ) {
    LOG(LOG_WARNING,
	_("Unexpected reply to '%s' operation.\n"),
	"GET");
    releaseClientSocket(sock);
    FREE(reply);
    return SYSERR;
  }
  /* ok, we got some replies! */

  res = (DHT_CS_REPLY_RESULTS*) reply;
  ret = ntohl(res->totalResults);
  
  size = ntohs(reply->size) - sizeof(DHT_CS_REPLY_RESULTS);
  if (results[0]->dataLength == 0)
    results[0]->data = MALLOC(size);
  else
    if (results[0]->dataLength < size)
      size = results[0]->dataLength;
  results[0]->dataLength = size;
  memcpy(results[0]->data,
	 &((DHT_CS_REPLY_RESULTS_GENERIC*)res)->data[0],
	 size);  
  FREE(reply);
  for (i=1;i<ret;i++) {
    reply = NULL;
    if (OK != readFromSocket(sock,
			     &reply)) {
      releaseClientSocket(sock);
      return i;
    }  
    if ( (sizeof(DHT_CS_REPLY_RESULTS) > ntohs(reply->size)) ||
	 (DHT_CS_PROTO_REPLY_GET != ntohs(reply->type)) ) {
      LOG(LOG_WARNING,
	  _("Unexpected reply to '%s' operation.\n"),
	  "GET");
      releaseClientSocket(sock);
      FREE(reply);
      return i;
    }
    if (i > maxResults) {
      FREE(reply);
      continue;
    }

    res = (DHT_CS_REPLY_RESULTS*) reply;
    ret = ntohl(res->totalResults);
  
    size = ntohs(reply->size) - sizeof(DHT_CS_REPLY_RESULTS);
    LOG(LOG_DEBUG,
	"'%s' processes reply '%.*s'\n",
	__FUNCTION__,
	size,
	&((DHT_CS_REPLY_RESULTS_GENERIC*)res)->data[0]);
    if (results[i]->dataLength == 0)
      results[i]->data = MALLOC(size);
    else
      if (results[i]->dataLength < size)
	size = results[i]->dataLength;
    results[i]->dataLength = size;
    memcpy(results[i]->data,
	   &((DHT_CS_REPLY_RESULTS_GENERIC*)res)->data[0],
	   size);  
    FREE(reply);
  }
  releaseClientSocket(sock);
  return ret;
}
	
/**
 * Perform a synchronous put operation.   The peer does not have
 * to be part of the table!
 *
 * @param table table to use for the lookup
 * @param key the key to store
 * @param timeout how long to wait until this operation should
 *        automatically time-out
 * @param value what to store
 * @return OK on success, SYSERR on error (or timeout)
 */
int DHT_LIB_put(const DHT_TableId * table,
		const HashCode160 * key,
		unsigned int prio,
		cron_t timeout,
		const DataContainer * value) {
  GNUNET_TCP_SOCKET * sock;
  DHT_CS_REQUEST_PUT * req;
  CS_HEADER * reply;
  int ret;

  LOG(LOG_DEBUG,
      "DHT_LIB_put called with value '%.*s'\n",
      ntohl(value->size),
      &value[1]);

  sock = getClientSocket();
  if (sock == NULL)
    return SYSERR;
  req = MALLOC(sizeof(DHT_CS_REQUEST_PUT) + 
	       ntohl(value->size) - 
	       sizeof(DataContainer));
  req->header.size 
    = htons(sizeof(DHT_CS_REQUEST_PUT) +
	    ntohl(value->size) -
	    sizeof(DataContainer));
  req->header.type 
    = htons(DHT_CS_PROTO_REQUEST_PUT);
  req->table = *table;
  req->key = *key;
  req->priority = htonl(prio);
  req->timeout = htonll(timeout);
  memcpy(&((DHT_CS_REQUEST_PUT_GENERIC*)req)->value[0],
	 &value[1],
	 ntohl(value->size) - sizeof(DataContainer));
  ret = SYSERR;
  if (OK == writeToSocket(sock,
			  &req->header))
    reply = NULL;
    if (OK == readFromSocket(sock,
			     &reply)) {
      if (OK == checkACK(reply))
	ret = OK;
      FREE(reply);
    }
  releaseClientSocket(sock);
  return ret;
}

/**
 * Perform a synchronous remove operation.  The peer does not have
 * to be part of the table!
 *
 * @param table table to use for the lookup
 * @param key the key to store
 * @param timeout how long to wait until this operation should
 *        automatically time-out
 * @param value what to remove; NULL for all values matching the key
 * @return OK on success, SYSERR on error (or timeout)
 */
int DHT_LIB_remove(const DHT_TableId * table,
		   const HashCode160 * key,
		   cron_t timeout,
		   const DataContainer * value) {
  GNUNET_TCP_SOCKET * sock;
  DHT_CS_REQUEST_REMOVE * req;
  CS_HEADER * reply;
  int ret;
  size_t n;

  sock = getClientSocket();
  if (sock == NULL)
    return SYSERR;
  n = sizeof(DHT_CS_REQUEST_REMOVE);
  if (value != NULL)
    n += ntohl(value->size) - sizeof(DataContainer);
  req = MALLOC(n);
  req->header.size = htons(n);
  req->header.type = htons(DHT_CS_PROTO_REQUEST_REMOVE);
  req->table = *table;
  req->key = *key;
  req->timeout = htonll(timeout);
  if (value != NULL)
    memcpy(&req[1],
	   &value[1],
	   ntohl(value->size) - sizeof(DataContainer));
  ret = SYSERR;
  if (OK == writeToSocket(sock,
			  &req->header))
    reply = NULL;
    if (OK == readFromSocket(sock,
			     &reply)) {
      if (OK == checkACK(reply))
	ret = OK;
      FREE(reply);
    }
  releaseClientSocket(sock);
  return ret;
}


/**
 * Initialize DHT_LIB. Call first.
 */
void DHT_LIB_init() {
  MUTEX_CREATE(&lock);
}

/**
 * Initialize DHT_LIB. Call after leaving all tables!
 */
void DHT_LIB_done() {
  MUTEX_DESTROY(&lock);
}


/* end of dht_api.c */
