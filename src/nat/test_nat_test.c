/*
     This file is part of GNUnet.
     Copyright (C) 2009, 2011, 2014 Christian Grothoff (and other contributing authors)

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
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/
/**
 * @file nat/test_nat_test.c
 * @brief Testcase for NAT testing functions
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_nat_lib.h"

/**
 * Time to wait before stopping NAT test, in seconds
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 15)


static int ret = 1;

static struct GNUNET_NAT_Test *tst;

static struct GNUNET_SCHEDULER_Task * tsk;


static void
report_result (void *cls,
                enum GNUNET_NAT_StatusCode aret)
{
  if (GNUNET_NAT_ERROR_TIMEOUT == aret)
    fprintf (stderr,
             "NAT test timed out\n");
  else if (GNUNET_NAT_ERROR_SUCCESS != aret)
    fprintf (stderr,
             "NAT test reported error %d\n", aret);
  else
    ret = 0;
  GNUNET_NAT_test_stop (tst);
  tst = NULL;
  GNUNET_SCHEDULER_cancel (tsk);
  tsk = NULL;
}


static void
failed_timeout (void *cls,
		const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  tsk = NULL;
  fprintf (stderr,
	   "NAT test failed to terminate on timeout\n");
  ret = 2;
  GNUNET_NAT_test_stop (tst);
  tst = NULL;
}


/**
 * Main function run with scheduler.
 */
static void
run (void *cls, char *const *args, const char *cfgfile,
     const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  tst =
      GNUNET_NAT_test_start (cfg, GNUNET_YES, 1285, 1285, TIMEOUT,
                             &report_result,
                             NULL);
  tsk = GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply (TIMEOUT,
								     2),
				      &failed_timeout,
				      NULL);
  
}


int
main (int argc, char *const argv[])
{
  struct GNUNET_GETOPT_CommandLineOption options[] = {
    GNUNET_GETOPT_OPTION_END
  };
  struct GNUNET_OS_Process *gns;
  int nat_res;
  char *const argv_prog[] = {
    "test-nat-test",
    "-c",
    "test_nat_test_data.conf",
    NULL
  };

  GNUNET_log_setup ("test-nat-test",
                    "WARNING",
                    NULL);

  nat_res = GNUNET_OS_check_helper_binary ("gnunet-nat-server", GNUNET_NO, NULL);
  if (GNUNET_SYSERR == nat_res)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_WARNING,
                "Cannot run NAT test: `%s' file not found\n",
                "gnunet-nat-server");
    return 0;
  }

  gns = GNUNET_OS_start_process (GNUNET_YES,
                                 GNUNET_OS_INHERIT_STD_OUT_AND_ERR,
                                 NULL, NULL, NULL,
                                 "gnunet-nat-server",
                                 "gnunet-nat-server",
                                 "-c", "test_nat_test_data.conf",
                                 "12345", NULL);
  GNUNET_assert (NULL != gns);
  GNUNET_PROGRAM_run (3, argv_prog,
		      "test-nat-test", "nohelp", 
		      options, &run,
                      NULL);
  GNUNET_break (0 == GNUNET_OS_process_kill (gns, GNUNET_TERM_SIG));
  GNUNET_break (GNUNET_OK == GNUNET_OS_process_wait (gns));
  GNUNET_OS_process_destroy (gns);
  if (0 != ret)
    fprintf (stderr,
             "NAT test failed to report success\n");
  return ret;
}

/* end of test_nat_test.c */
