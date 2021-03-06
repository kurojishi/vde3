/* Copyright (C) 2009 - Virtualsquare Team
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <vde3.h>

#include <vde3/module.h>
#include <vde3/engine.h>
#include <vde3/transport.h>
#include <vde3/conn_manager.h>
#include <vde3/context.h>

enum vde_conn_state {
  CONNECT_WAIT,
  AUTHORIZATION_REQ_SENT,
  AUTHORIZATION_REQ_WAIT,
  AUTHORIZATION_REPLY_SENT,
  AUTHORIZATION_REPLY_WAIT,
  NOT_AUTHORIZED,
  AUTHORIZED
};

struct pending_conn {
  vde_connection *conn;
  vde_request *lreq;
  vde_request *rreq;
  enum vde_conn_state state;
  vde_connect_success_cb success_cb;
  vde_connect_error_cb error_cb;
  void *connect_cb_arg;
};

typedef struct {
  vde_component *transport;
  vde_component *engine;
  vde_component *component;
  vde_list *pending_conns;
  int do_remote_authorization;
} conn_manager;

static struct pending_conn *lookup_pending_conn(conn_manager *cm,
                                                vde_connection *conn)
{
  struct pending_conn *pc = NULL;

  vde_list *iter = vde_list_first(cm->pending_conns);
  while (iter != NULL) {
    pc = vde_list_get_data(iter);
    if (pc->conn == conn) {
      return pc;
    }
    iter = vde_list_next(iter);
  }
  return NULL;
}


// XXX: consider having an application callback here, to be called for each new
//      connection
int conn_manager_listen(vde_component *component)
{
  conn_manager *cm = (conn_manager *)vde_component_get_priv(component);

  return vde_transport_listen(cm->transport);
}

int conn_manager_connect(vde_component *component,
                         vde_request *local_request,
                         vde_request *remote_request,
                         vde_connect_success_cb success_cb,
                         vde_connect_error_cb error_cb, void *arg)
{
  conn_manager *cm;
  vde_connection *conn;
  struct pending_conn *pc;
  int tmp_errno;

  if (vde_connection_new(&conn)) {
    tmp_errno = errno;
    vde_error("%s: cannot create connection", __PRETTY_FUNCTION__);
    errno = tmp_errno;
    return -1;
  }

  pc = (struct pending_conn *)vde_calloc(sizeof(struct pending_conn));
  if (!pc) {
    vde_connection_delete(conn);
    vde_error("%s: cannot create pending connection", __PRETTY_FUNCTION__);
    errno = ENOMEM;
    return -1;
  }

  // XXX: check requests with do_remote_authorization,
  //      what about normalization?
  //      memdup requests here?
  pc->conn = conn;
  pc->lreq = local_request;
  pc->rreq = remote_request;
  pc->state = CONNECT_WAIT;
  pc->success_cb = success_cb;
  pc->error_cb = error_cb;
  pc->connect_cb_arg = arg;

  cm = vde_component_get_priv(component);
  cm->pending_conns = vde_list_prepend(cm->pending_conns, pc);

  return vde_transport_connect(cm->transport, conn);
}

static int post_authorization(conn_manager *cm, struct pending_conn *pc)
{
  // invoke user callbacks on error/success
  if(vde_engine_new_connection(cm->engine, pc->conn, pc->lreq)) {
    if (pc->error_cb) {
      pc->error_cb(cm->component, pc->connect_cb_arg);
    }
  } else {
    if (pc->success_cb) {
      pc->success_cb(cm->component, pc->connect_cb_arg);
    }
  }
  cm->pending_conns = vde_list_remove(cm->pending_conns, pc);
  vde_free(pc); // XXX: free requests here ?
  return 0;
}

void conn_manager_connect_cb(vde_connection *conn, void *arg)
{
  struct pending_conn *pc;
  vde_component *component = (vde_component *)arg;
  conn_manager *cm = (conn_manager *)vde_component_get_priv(component);

  pc = lookup_pending_conn(cm, conn);
  if (!pc) {
    vde_connection_fini(conn);
    vde_connection_delete(conn);
    vde_error("%s: cannot lookup pending connection", __PRETTY_FUNCTION__);
    return;
  }
  if (cm->do_remote_authorization) {
    //vde_connection_set_callbacks(conn, connection_read_cb,
    //                             connection_error_cb, component);
    // - fill defaults for remote_request?
    // - search remote_request in cm->priv->pending_conns
    // - begin authorization process
    // pc->state = AUTHORIZATION_REQ_SENT
  } else {
    pc->state = AUTHORIZED;
    post_authorization(cm, pc);
  }
}

void conn_manager_accept_cb(vde_connection *conn, void *arg)
{
  struct pending_conn *pc;
  vde_component *component = (vde_component *)arg;
  conn_manager *cm = (conn_manager *)vde_component_get_priv(component);

  // XXX: safety check: conn not already added to pending_conns

  pc = (struct pending_conn *)vde_calloc(sizeof(struct pending_conn));
  if (!pc) {
    vde_connection_fini(conn);
    vde_connection_delete(conn);
    vde_error("%s: cannot create pending connection", __PRETTY_FUNCTION__);
    return;
  }

  pc->conn = conn;

  cm->pending_conns = vde_list_prepend(cm->pending_conns, pc);

  if (cm->do_remote_authorization) {
    //vde_conn_set_callbacks(conn, connection_read_cb, connection_error_cb,
    //                       component);
    // exchange authorization, eventually call post_authorization if
    // successful
    // pc->state = AUTHORIZATION_REQ_WAIT
  } else {
    // fill local_request with defaults
    pc->state = AUTHORIZED;
    post_authorization(cm, pc);
  }
}

void conn_manager_error_cb(vde_connection *conn, int tr_errno,
                           void *arg)
{
  // if conn in pending_conn and there's an application callback call it,
  // delete pending_conn and delete conn
}

// in engine.new_conn: vde_conn_set_callbacks(conn, engine_callbacks..)

// XXX: cm_read_cb / cm_error_cb, they need to be different for accept/connect
// callbacks?

int conn_manager_init(vde_component *component, vde_sobj *params)
{
  conn_manager *cm;
  int remote_auth;
  vde_component *engine, *transport;
  vde_sobj *engine_sobj, *transport_sobj, *remote_auth_sobj;
  const char *engine_name, *transport_name;
  vde_context *ctx = vde_component_get_context(component);

  vde_assert(component != NULL);

  if (!params || !vde_sobj_is_type(params, vde_sobj_type_hash)) {
    vde_error("%s: no parameters hash received", __PRETTY_FUNCTION__);
    errno = EINVAL;
    return -1;
  }

  /* pick transport */
  transport_sobj = vde_sobj_hash_lookup(params, "transport");
  if (!transport_sobj || !vde_sobj_is_type(transport_sobj,
                                           vde_sobj_type_string)) {
    vde_error("%s: no transport name specified", __PRETTY_FUNCTION__);
    errno = EINVAL;
    return -1;
  }
  transport_name = vde_sobj_get_string(transport_sobj);

  transport = vde_context_get_component(ctx, transport_name);
  if (!transport) {
    vde_error("%s: transport %s not found in context", __PRETTY_FUNCTION__,
              transport_name);
    errno = EINVAL;
    return -1;
  }

  if (vde_component_get_kind(transport) != VDE_TRANSPORT) {
    vde_error("%s: component transport is not a transport",
              __PRETTY_FUNCTION__);
    errno = EINVAL;
    return -1;
  }

  /* pick engine */
  engine_sobj = vde_sobj_hash_lookup(params, "engine");
  if (!engine_sobj || !vde_sobj_is_type(engine_sobj, vde_sobj_type_string)) {
    vde_error("%s: no engine name specified", __PRETTY_FUNCTION__);
    errno = EINVAL;
    return -1;
  }
  engine_name = vde_sobj_get_string(engine_sobj);

  engine = vde_context_get_component(ctx, engine_name);
  if (!engine) {
    vde_error("%s: engine %s not found in context", __PRETTY_FUNCTION__,
              engine_name);
    errno = EINVAL;
    return -1;
  }

  if (vde_component_get_kind(engine) != VDE_ENGINE) {
    vde_error("%s: component engine is not a engine",
              __PRETTY_FUNCTION__);
    errno = EINVAL;
    return -1;
  }

  /* remote authorization, not enabled by default */
  remote_auth_sobj = vde_sobj_hash_lookup(params, "remote_authorization");
  if (!remote_auth_sobj) {
    remote_auth = 0;
  } else {
    if (!vde_sobj_is_type(remote_auth_sobj, vde_sobj_type_bool)) {
      vde_error("%s: wrong remote authorization param", __PRETTY_FUNCTION__);
      errno = EINVAL;
      return -1;
    }
    remote_auth = vde_sobj_get_bool(remote_auth_sobj);
  }

  /* create connection manager */
  cm = (conn_manager *)vde_calloc(sizeof(conn_manager));
  if (cm == NULL) {
    vde_error("%s: could not allocate private data", __PRETTY_FUNCTION__);
    errno = ENOMEM;
    return -1;
  }

  cm->transport = transport;
  cm->engine = engine;
  cm->component = component;
  cm->do_remote_authorization = remote_auth;
  cm->pending_conns = NULL;

  /* Increase reference counter of tracked components */
  vde_component_get(transport, NULL);
  vde_component_get(engine, NULL);

  vde_transport_set_cm_callbacks(transport, &conn_manager_connect_cb,
                                 &conn_manager_accept_cb,
                                 &conn_manager_error_cb, (void *)component);

  vde_component_set_priv(component, cm);
  return 0;
}

// XXX to be defined
void conn_manager_fini(vde_component *component)
{
  conn_manager *cm = vde_component_get_priv(component);

  vde_assert(component != NULL);

  vde_component_put(cm->transport, NULL);
  vde_component_put(cm->engine, NULL);

  // XXX what to do with cm->pending_conns if not empty?
}

component_ops conn_manager_component_ops = {
  .init = conn_manager_init,
  .fini = conn_manager_fini,
  .get_configuration = NULL,
  .set_configuration = NULL,
  .get_policy = NULL,
  .set_policy = NULL,
};

vde_module VDE_MODULE_START = {
  .kind = VDE_CONNECTION_MANAGER,
  .family = "default",
  .cops = &conn_manager_component_ops,
  .cm_listen = &conn_manager_listen,
  .cm_connect = &conn_manager_connect,
};

