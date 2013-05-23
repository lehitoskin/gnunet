/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file experimentation/gnunet-daemon-experimentation_capabilities.c
 * @brief experimentation daemon: capabilities management
 * @author Christian Grothoff
 * @author Matthias Wachs
 */
#include "platform.h"
#include "gnunet_getopt_lib.h"
#include "gnunet_util_lib.h"
#include "gnunet_core_service.h"
#include "gnunet_statistics_service.h"
#include "gnunet-daemon-experimentation.h"

enum ExperimentationCapabilities
{
	NONE = 0,
	PLUGIN_TCP = 1,
	PLUGIN_UDP = 2,
	PLUGIN_UNIX = 4,
	PLUGIN_HTTP_CLIENT = 8,
	PLUGIN_HTTP_SERVER = 16,
	PLUGIN_HTTPS_CLIENT = 32,
	PLUGIN_HTTPS_SERVER = 64,
	PLUGIN_WLAN = 128,
};

/**
 * Start the detecting capabilities
 *
 * @param cfg configuration handle
 */
void
GNUNET_EXPERIMENTATION_capabilities_start ()
{
	char *plugins;
  char *pos;
  uint32_t capabilities = NONE;

	/* Plugins configured */

  if (GNUNET_OK == GNUNET_CONFIGURATION_get_value_string (GSE_cfg,
  			"TRANSPORT", "PLUGINS", &plugins))
  {
  	  for (pos = strtok (plugins, " "); pos != NULL; pos = strtok (NULL, " "))
  	  {
  	      GNUNET_log (GNUNET_ERROR_TYPE_INFO, _("Found `%s' transport plugin\n"),
  	                  pos);
  	      if (0 == strcmp (pos, "tcp"))
  	      	capabilities |= PLUGIN_TCP;
  	      else if (0 == strcmp (pos, "udp"))
  	      	capabilities |= PLUGIN_UDP;
					else if (0 == strcmp (pos, "unix"))
						capabilities |= PLUGIN_UNIX;
					else if (0 == strcmp (pos, "http_client"))
						capabilities |= PLUGIN_HTTP_CLIENT;
					else if (0 == strcmp (pos, "http_server"))
						capabilities |= PLUGIN_HTTP_SERVER;
					else if (0 == strcmp (pos, "https_client"))
						capabilities |= PLUGIN_HTTP_CLIENT;
					else if (0 == strcmp (pos, "https_server"))
						capabilities |= PLUGIN_HTTPS_SERVER;
					else if (0 == strcmp (pos, "wlan"))
						capabilities |= PLUGIN_WLAN;
  	  }
  	  GNUNET_free (plugins);
  }

	/* IPv6 enabled */

	/* Behind NAT */
}

/**
 * Stop the detecting capabilities
 */
void
GNUNET_EXPERIMENTATION_capabilities_stop ()
{

}

/* end of gnunet-daemon-experimentation_capabilities.c */
