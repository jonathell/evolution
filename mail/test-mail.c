/*
 * Tests the mail summary display bonobo component
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 2000 Helix Code, Inc.
 */
#include <gnome.h>
#include <bonobo.h>

static void
create_container (void)
{
	GtkWidget *window, *control;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_show (window);

	control = bonobo_widget_new_control ("GOADID:Evolution:FolderBrowser:1.0");
	if (control == NULL){
		printf ("Could not launch mail control\n");
		exit (1);
	}
	gtk_container_add (GTK_CONTAINER (window), control);

	gtk_widget_show (window);
	gtk_widget_show (control);
}

int
main (int argc, char *argv [])
{
	CORBA_Environment ev;
	CORBA_ORB orb;

	CORBA_exception_init (&ev);

	gnome_CORBA_init ("sample-control-container", "1.0", &argc, argv, 0, &ev);

	CORBA_exception_free (&ev);

	orb = gnome_CORBA_ORB ();

	if (bonobo_init (orb, NULL, NULL) == FALSE)
		g_error ("Could not initialize Bonobo\n");

	bonobo_activate ();
	
	create_container ();

	/*
	 * Main loop
	 */
	gtk_main ();
	
	return 0;
}
