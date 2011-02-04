/**[txh]********************************************************************

  Copyright (c) 2004 by Salvador E. Tropea.
  Covered by the GPL license.

  Comment:
  X11 example/test of the libmigdb.
  Run it from an X11 terminal (xterm, Eterm, etc.).
  
***************************************************************************/

#include <stdio.h>
#include <unistd.h> //usleep
#include <libesmtp.h>
#include "gdbmi.h"
#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_program_lib.h"
#include "gnunet_monkey_action.h"

extern void sendMail (const char *messageContents);
static const char* mode;
static const char* dumpFileName;
static const char* binaryName;
static int ret = 0;

void cb_console(const char *str, void *data)
{
 printf("CONSOLE> %s\n",str);
}

/* Note that unlike what's documented in gdb docs it isn't usable. */
void cb_target(const char *str, void *data)
{
 printf("TARGET> %s\n",str);
}

void cb_log(const char *str, void *data)
{
 printf("LOG> %s\n",str);
}

void cb_to(const char *str, void *data)
{
 printf(">> %s",str);
}

void cb_from(const char *str, void *data)
{
 printf("<< %s\n",str);
}

static int async_c=0;

void cb_async(mi_output *o, void *data)
{
 printf("ASYNC\n");
 async_c++;
}


static void dumpText(const char* message)
{
	FILE* file = fopen(dumpFileName, "w");
	GNUNET_assert(NULL != file);
	fprintf(file,"%s", message);
	fclose(file);
}


void send_bug_mail(mi_stop* sr, mi_frames* f)
{
	char *message;
	GNUNET_asprintf(&message, 
			"Bug detected in file:%s\nfunction:%s\nline:%d\nreason:%s\nreceived signal:%s\n%s\n",
			f->file, f->func, f->line, mi_reason_enum_to_str(sr->reason), sr->signal_name, sr->signal_meaning);
	if (strcasecmp(mode, "mail") == 0)
		sendMail(message);
	else
		dumpText(message);
	
	GNUNET_free (message);
}


int wait_for_stop(mi_h *h)
{
 int res=1;
 mi_stop *sr;
 mi_frames *f;

 while (!mi_get_response(h))
    usleep(1000);
 /* The end of the async. */
 sr=mi_res_stop(h);
 if (sr)
   {
    f = gmi_stack_info_frame(h);
    if (f != NULL)
      send_bug_mail(sr, f);
    else
      GNUNET_break (0);
    mi_free_stop(sr);
    res = 0;
   }
 else
   {
    printf("Error while waiting\n");
    printf("mi_error: %d\nmi_error_from_gdb: %s\n",mi_error,mi_error_from_gdb);
    res=0;
   }
 return res;
}



/**
 * Main function that will be run by the scheduler.
 *
 * @param cls closure
 * @param args remaining command-line arguments
 * @param cfgfile name of the configuration file used (for saving, can be NULL!)
 * @param c configuration
 */
static void
run (void *cls,
     char *const *args,
     const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *c)
{
	struct GNUNET_MONKEY_ACTION_Context* cntxt =
			GNUNET_malloc(sizeof(struct GNUNET_MONKEY_ACTION_Context));
	cntxt->binary_name = binaryName;
	if (GNUNET_OK == GNUNET_MONKEY_ACTION_rerun_with_gdb(cntxt)) {
		GNUNET_MONKEY_ACTION_format_report(cntxt);
		GNUNET_MONKEY_ACTION_report_file(cntxt, dumpFileName);
	}

//	mi_aux_term *xterm_tty=NULL;
//
//	/* This is like a file-handle for fopen.
//	    Here we have all the state of gdb "connection". */
//	 mi_h *h;
//
//	 /* Connect to gdb child. */
//	 h=mi_connect_local();
//	 if (!h)
//	   {
//	    printf("Connect failed\n");
//	    ret = 1;
//	    return;
//	   }
//	 printf("Connected to gdb!\n");
//
//	 /* Set all callbacks. */
//	 mi_set_console_cb(h,cb_console,NULL);
//	 mi_set_target_cb(h,cb_target,NULL);
//	 mi_set_log_cb(h,cb_log,NULL);
//	 mi_set_async_cb(h,cb_async,NULL);
//	 mi_set_to_gdb_cb(h,cb_to,NULL);
//	 mi_set_from_gdb_cb(h,cb_from,NULL);
//
//	 /* Set the name of the child and the command line aguments. */
//	 if (!gmi_set_exec(h, binaryName, NULL))
//	   {
//	    printf("Error setting exec y args\n");
//	    mi_disconnect(h);
//	    ret = 1;
//	    return;
//	   }
//
//	 /* Tell gdb to attach the child to a terminal. */
//	 if (!gmi_target_terminal(h, ttyname(STDIN_FILENO)))
//	   {
//	    printf("Error selecting target terminal\n");
//	    mi_disconnect(h);
//	    ret = 1;
//	    return;
//	   }
//
//	 /* Run the program. */
//	 if (!gmi_exec_run(h))
//	   {
//	    printf("Error in run!\n");
//	    mi_disconnect(h);
//	    ret = 1;
//	    return;
//	   }
//	 /* Here we should be stopped when the program crashes */
//	 if (!wait_for_stop(h))
//	   {
//	    mi_disconnect(h);
//	    ret = 1;
//	    return;
//	   }
//
//	 /* Continue execution. */
//	 if (!gmi_exec_continue(h))
//	   {
//	    printf("Error in continue!\n");
//	    mi_disconnect(h);
//	    ret = 1;
//	    return;
//	   }
//	 /* Here we should be terminated. */
//	 if (!wait_for_stop(h))
//	   {
//	    mi_disconnect(h);
//	    ret = 1;
//	    return;
//	   }
//
//	 /* Exit from gdb. */
//	 gmi_gdb_exit(h);
//	 /* Close the connection. */
//	 mi_disconnect(h);
//	 /* Wait 5 seconds and close the auxiliar terminal. */
//	 printf("Waiting 5 seconds\n");
//	 sleep(5);
//	 gmi_end_aux_term(xterm_tty);
}


int main(int argc, char *argv[])
{
	/*
	 * FIXME: 
	 * Command should accept email address to which monkey sends the debugging report.
	 * The email address can also be read from the configuration file.
	 */
 static const struct GNUNET_GETOPT_CommandLineOption options[] = {
     {'m', "mode", NULL, gettext_noop ("monkey's mode of operation: options are \"text\" or \"email\""),
      GNUNET_YES, &GNUNET_GETOPT_set_string, &mode},
     {'b', "binary", NULL, gettext_noop ("binary for program to debug with monkey"),
      GNUNET_YES, &GNUNET_GETOPT_set_string, &binaryName},
     {'o', "output", NULL, gettext_noop ("path to file to dump monkey's output in case of working in text mode"),
      GNUNET_YES, &GNUNET_GETOPT_set_string, &dumpFileName},
      GNUNET_GETOPT_OPTION_END
   };
 
 if (argc < 2) {
	 printf("%s", "Monkey should take arguments: Use --help to get a list of options.\n");
	 return 1;
 }
 
 if (GNUNET_OK == GNUNET_PROGRAM_run (argc,
                       argv,
                       "gnunet-monkey",
                       gettext_noop
                       ("Automatically debug a service"),
                       options, &run, NULL))
     {
       return ret;
     }

     return 1;
}
