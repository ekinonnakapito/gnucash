/*********************************************************************
 * businessmod-core.c
 * module definition/initialization for the Business GNOME UI module
 * 
 * Copyright (c) 2001 Derek Atkins <warlord@MIT.EDU>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652
 * Boston, MA  02111-1307,  USA       gnu@gnu.org
 *
 *********************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <libguile.h>
#include <glib.h>

#include "gnc-hooks.h"
#include "gnc-module.h"
#include "gnc-module-api.h"
#include "gw-business-gnome.h"

#include "search-core-type.h"
#include "search-owner.h"
#include "gncOwner.h"
#include "business-options-gnome.h"
#include "business-urls.h"

#include "gnc-plugin-manager.h"
#include "gnc-plugin-business.h"

#include "gnc-hooks.h"
#include "dialog-invoice.h"
#include "dialog-preferences.h"

/* version of the gnc module system interface we require */
int libgncmod_business_gnome_LTX_gnc_module_system_interface = 0;

/* module versioning uses libtool semantics. */
int libgncmod_business_gnome_LTX_gnc_module_current  = 0;
int libgncmod_business_gnome_LTX_gnc_module_revision = 0;
int libgncmod_business_gnome_LTX_gnc_module_age      = 0;

/* forward references */
char *libgncmod_business_gnome_LTX_gnc_module_path(void);
char *libgncmod_business_gnome_LTX_gnc_module_description(void);
int libgncmod_business_gnome_LTX_gnc_module_init(int refcount);
int libgncmod_business_gnome_LTX_gnc_module_end(int refcount);


char *
libgncmod_business_gnome_LTX_gnc_module_path(void) 
{
  return g_strdup("gnucash/business-gnome");
}

char * 
libgncmod_business_gnome_LTX_gnc_module_description(void) 
{
  return g_strdup("The Gnucash business module GNOME UI");
}

int
libgncmod_business_gnome_LTX_gnc_module_init(int refcount) 
{
  /* load business-core: we depend on it -- and it depends on the engine */
  if (!gnc_module_load ("gnucash/business-core", 0)) {
    return FALSE;
  }
  /* We also depend on app-utils, gnome-utils, and gnome-search modules */
  if (!gnc_module_load ("gnucash/app-utils", 0)) {
    return FALSE;
  }
  if (!gnc_module_load ("gnucash/gnome-utils", 0)) {
    return FALSE;
  }
  if (!gnc_module_load ("gnucash/gnome-search", 0)) {
    return FALSE;
  }
  if (!gnc_module_load ("gnucash/report/report-gnome", 0)) {
    return FALSE;
  }
  //  if (!gnc_module_load ("gnucash/report/standard-reports", 0)) {
  //    return FALSE;
  //  }

  scm_c_eval_string("(use-modules (g-wrapped gw-business-gnome))");
  scm_c_eval_string("(use-modules (gnucash business-gnome))");
  scm_c_eval_string("(use-modules (gnucash report business-reports))");

  if (refcount == 0) {
    /* Register the Owner search type */
    gnc_search_core_register_type (GNC_OWNER_MODULE_NAME,
				   (GNCSearchCoreNew) gnc_search_owner_new);
    gnc_business_urls_initialize ();
    gnc_business_options_gnome_initialize ();

    gnc_plugin_manager_add_plugin (gnc_plugin_manager_get (),
				   gnc_plugin_business_new ());

    gnc_hook_add_dangler(HOOK_UI_POST_STARTUP,
			 (GFunc)gnc_invoice_remind_bills_due_cb, NULL);

    gnc_preferences_add_page("businessprefs.glade", "business_prefs",
			     "Business");
  }

  return TRUE;
}

int
libgncmod_business_gnome_LTX_gnc_module_end(int refcount) {
  return TRUE;
}
