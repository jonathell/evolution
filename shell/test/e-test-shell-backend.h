/*
 * e-test-shell-backend.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>  
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_TEST_SHELL_BACKEND_H
#define E_TEST_SHELL_BACKEND_H

#include <shell/e-shell-backend.h>

/* Standard GObject macros */
#define E_TYPE_TEST_SHELL_BACKEND \
	(e_test_shell_backend_type)
#define E_TEST_SHELL_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_TEST_SHELL_BACKEND, ETestShellBackend))
#define E_TEST_SHELL_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_TEST_SHELL_BACKEND, ETestShellBackendClass))
#define E_IS_TEST_SHELL_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_TEST_SHELL_BACKEND))
#define E_IS_TEST_SHELL_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_TEST_SHELL_BACKEND))
#define E_TEST_SHELL_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_TEST_SHELL_BACKEND, ETestShellBackendClass))

G_BEGIN_DECLS

extern GType e_test_shell_backend_type;

typedef struct _ETestShellBackend ETestShellBackend;
typedef struct _ETestShellBackendClass ETestShellBackendClass;
typedef struct _ETestShellBackendPrivate ETestShellBackendPrivate;

struct _ETestShellBackend {
	EShellBackend parent;
	ETestShellBackendPrivate *priv;
};

struct _ETestShellBackendClass {
	EShellBackendClass parent_class;
};

GType		e_test_shell_backend_get_type	(GTypeModule *type_module);

G_END_DECLS

#endif /* E_TEST_SHELL_BACKEND_H */
