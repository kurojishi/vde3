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

#ifndef __VDE3_PRIV_MODULE_H__
#define __VDE3_PRIV_MODULE_H__

// es. unixtransport
unixtransport_accept(struct component * c, ...) {
  struct unixtransport_state *instance = (struct unixtransport_state*)c->priv;
}

struct transport_ops unixtransport_ops = {
  .accept = unixtransport_accept,
  ...
};

struct unixtransport_state {
};

new_unixtransport(struct component *c) {
  c->ops->transport = unixtransport_ops;
  t->priv = inizializzi uno state e glielo butti dentro
  // register commands
}

struct vde3_module {
  char* .kind = "transport",
  char* .family = "unix",
  struct component_ops *ops, // verra' copiato dal contesto nel componente
                             // vero e proprio quando viene creato
} unixtransport_module;

// function name needs to be the same because we need to dlsym
int vde3_module_init(vde3_context *ctx) {
  vde3_context_register_module(ctx, unixtransport_module);
}

#endif /* __VDE3_PRIV_MODULE_H__ */
