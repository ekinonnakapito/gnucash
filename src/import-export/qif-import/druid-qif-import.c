/********************************************************************\
 * druid-qif-import.c -- window for importing QIF files            *
 *                        (GnuCash)                                 *
 * Copyright (C) 2000 Bill Gribble <grib@billgribble.com>           *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
\********************************************************************/

#define _GNU_SOURCE

#include "config.h"

#include <gnome.h>
#include <libguile.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <libgnomeui/gnome-window-icon.h>

#include "Account.h"
#include "Transaction.h"
#include "dialog-account-picker.h"
#include "dialog-commodity.h"
#include "dialog-utils.h"
#include "druid-qif-import.h"
#include "druid-utils.h"
#include "gnc-component-manager.h"
#include "gnc-engine-util.h"
#include "gnc-file.h"
#include "gnc-gui-query.h"
#include "gnc-ui-util.h"
#include "gnc-gconf-utils.h"
#include "gnc-ui.h"
#include "messages.h"
#include "guile-mappings.h"

#include <g-wrap-wct.h>

#define DRUID_QIF_IMPORT_CM_CLASS "druid-qif-import"
#define GCONF_SECTION "dialogs/import/qif"

struct _qifimportwindow {
  GtkWidget * window;
  GtkWidget * druid;
  GtkWidget * filename_entry;
  GtkWidget * acct_entry;
  GtkWidget * date_format_combo;
  GtkWidget * date_format_entry;
  GtkWidget * selected_file_list;
  GtkWidget * acct_list;
  GtkWidget * cat_list;
  GtkWidget * memo_list;
  GtkWidget * currency_picker;
  GtkWidget * currency_entry;
  GtkWidget * new_transaction_list;
  GtkWidget * old_transaction_list;
  
  GList     * pre_comm_pages;
  GList     * commodity_pages;
  GList     * post_comm_pages;
  GList     * doc_pages;

  gboolean  show_doc_pages;

  SCM       imported_files;
  SCM       selected_file;

  SCM       acct_map_info; 
  SCM       acct_display_info;

  SCM       cat_map_info;
  SCM       cat_display_info;

  SCM       memo_map_info;
  SCM       memo_display_info;

  SCM       gnc_acct_info;
  SCM       stock_hash;
  SCM       new_stocks;
  SCM       ticker_map;

  SCM       imported_account_group;
  SCM       match_transactions;
  int       selected_transaction;
};

struct _qifdruidpage {
  GtkWidget * page;
  GtkWidget * new_type_combo;
  GtkWidget * new_name_entry;
  GtkWidget * new_mnemonic_entry;
  gnc_commodity * commodity;
};  

typedef struct _qifdruidpage QIFDruidPage;
static QIFDruidPage * make_qif_druid_page(gnc_commodity * comm);

static void update_file_page(QIFImportWindow * win);
static void update_accounts_page(QIFImportWindow * win);
static void update_categories_page(QIFImportWindow * win);
static void update_memo_page(QIFImportWindow * win);

static void update_account_picker_page(QIFImportWindow * wind,
				       SCM make_display, GtkWidget *list,
				       SCM map_info, SCM * display_info);

static void gnc_ui_qif_import_commodity_prepare_cb(GnomeDruidPage * page,
                                                   gpointer arg1,
                                                   gpointer user_data);

static GdkColor std_bg_color = { 0, 39835, 49087, 40092 };
static GdkColor std_logo_bg_color = { 0, 65535, 65535, 65535 };
static GdkColor std_title_color =  { 0, 65535, 65535, 65535 };

#define NUM_PRE_PAGES 13
#define NUM_POST_PAGES 3
#define NUM_DOC_PAGES  6

static GnomeDruidPage *
get_named_page(QIFImportWindow * w, const char * name)
{
  return GNOME_DRUID_PAGE(gnc_glade_lookup_widget(w->window, name));
}


/********************************************************************\
 * gnc_ui_qif_import_druid_destroy
 * close the QIF Import druid window
\********************************************************************/

void
gnc_ui_qif_import_druid_destroy (QIFImportWindow * window)
{
  if (!window)
    return;

  /* FIXME -- commodity pages */

  gnc_unregister_gui_component_by_data(DRUID_QIF_IMPORT_CM_CLASS, window);

  gtk_widget_destroy(window->window);

  scm_gc_unprotect_object(window->imported_files);
  scm_gc_unprotect_object(window->selected_file);
  scm_gc_unprotect_object(window->gnc_acct_info);
  scm_gc_unprotect_object(window->cat_display_info);
  scm_gc_unprotect_object(window->cat_map_info);
  scm_gc_unprotect_object(window->memo_display_info);
  scm_gc_unprotect_object(window->memo_map_info);
  scm_gc_unprotect_object(window->acct_display_info);
  scm_gc_unprotect_object(window->acct_map_info);
  scm_gc_unprotect_object(window->stock_hash);
  scm_gc_unprotect_object(window->new_stocks);
  scm_gc_unprotect_object(window->ticker_map);
  scm_gc_unprotect_object(window->imported_account_group);
  scm_gc_unprotect_object(window->match_transactions);

  g_free(window);
}

static GtkWidget * 
get_next_druid_page(QIFImportWindow * wind, GnomeDruidPage * page)
{
  GList     * current = NULL;
  GList     * next;
  int       where = 0;
  
  if((current = g_list_find(wind->pre_comm_pages, page)) == NULL) {
    if((current = g_list_find(wind->commodity_pages, page)) == NULL) {
      if((current = g_list_find(wind->post_comm_pages, page)) == NULL) {
        /* where are we? */
        printf("QIF import: I'm lost!\n");
        return FALSE;
      }
      else {
        where = 3;
      }
    }
    else {
      where = 2;
    }
  }
  else {
    where = 1;
  }
  
  next = current->next;
  while(!next ||
        (!wind->show_doc_pages &&
         g_list_find(wind->doc_pages, next->data))) {
    if(next && next->next) {
      next = next->next;
    }
    else {
      where ++;
      switch(where) {
      case 2:
        next = wind->commodity_pages;
        break;
      case 3:
        next = wind->post_comm_pages;
        break;
      default:
        printf("QIF import: something fishy.\n");
        next = NULL;
        if (where > 3)
          return NULL;
        break;
      }              
    }
  }

  if(next) return (GtkWidget *)next->data;
  else return NULL;
}

static GtkWidget * 
get_prev_druid_page(QIFImportWindow * wind, GnomeDruidPage * page)
{
  GList     * current = NULL;
  GList     * prev;
  int       where = 0;
  
  if((current = g_list_find(wind->pre_comm_pages, page)) == NULL) {
    if((current = g_list_find(wind->commodity_pages, page)) == NULL) {
      if((current = g_list_find(wind->post_comm_pages, page)) == NULL) {
        /* where are we? */
        printf("QIF import: I'm lost!\n");
        return FALSE;
      }
      else {
        where = 3;
      }
    }
    else {
      where = 2;
    }
  }
  else {
    where = 1;
  }
  
  prev = current->prev;
  while(!prev ||
        (!wind->show_doc_pages &&
         g_list_find(wind->doc_pages, prev->data))) {
    if(prev && prev->prev) {
      prev = prev->prev;
    }
    else {
      where --;
      switch(where) {
      case 1:
        prev = g_list_last(wind->pre_comm_pages);
        break;
      case 2:
        if(wind->new_stocks != SCM_BOOL_F) {
          prev = g_list_last(wind->commodity_pages);
        }
        else {
           prev = g_list_last(wind->pre_comm_pages);
        }
        break;
      default:
        if (wind->show_doc_pages)
          printf("QIF import: something fishy.\n");
        prev = NULL;
        if (where < 1)
          return NULL;
        break;
      }              
    }
  }
  if(prev)
    return (GtkWidget *)prev->data;
  else 
    return NULL;
}


/********************************************************************
 * gnc_ui_qif_import_generic_next_cb
 * close the QIF Import druid window
 ********************************************************************/

static gboolean
gnc_ui_qif_import_generic_next_cb(GnomeDruidPage * page, gpointer arg1, 
                                  gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  GtkWidget * next_page = get_next_druid_page(wind, page);
  
  if(next_page) {
    gnome_druid_set_page(GNOME_DRUID(wind->druid),
                         GNOME_DRUID_PAGE(next_page));
    
    return TRUE;
  }
  else {
    return FALSE;
  }
}

/********************************************************************
 * gnc_ui_qif_import_generic_back_cb
 * close the QIF Import druid window
 ********************************************************************/

static gboolean
gnc_ui_qif_import_generic_back_cb(GnomeDruidPage * page, gpointer arg1, 
                                  gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  GtkWidget * back_page = get_prev_druid_page(wind, page);
  
  if(back_page) {
    gnome_druid_set_page(GNOME_DRUID(wind->druid),
                         GNOME_DRUID_PAGE(back_page));
    return TRUE;
  }
  else {
    return FALSE;
  }
}


/********************************************************************
 * gnc_ui_qif_import_select_file_cb
 * invoked when the "select file" button is clicked
 * this is just to pick a file name and reset-to-defaults all the 
 * fields describing how to parse the file.
 ********************************************************************/

static void
gnc_ui_qif_import_select_file_cb(GtkButton * button,
                                 gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  char * new_file_name;
  char *file_name, *default_dir;

  /* Default to whatever's already present */
  default_dir = gnc_gconf_get_string(GCONF_SECTION, KEY_LAST_PATH, NULL);
  if (default_dir == NULL)
    gnc_init_default_directory(&default_dir);
  new_file_name = gnc_file_dialog (_("Select QIF File"), "*.qif", 
		  default_dir, GNC_FILE_DIALOG_IMPORT);

  /* Insure valid data, and something that can be freed. */
  if (new_file_name == NULL) {
    file_name = g_strdup(default_dir);
  } else if (*new_file_name != '/') {
    file_name = g_strdup_printf("%s%s", default_dir, new_file_name);
    g_free(new_file_name);
  } else {
    file_name = new_file_name;
  }

  /* set the filename entry for what was selected */
  gtk_entry_set_text(GTK_ENTRY(wind->filename_entry), file_name);

  /* Update the working directory */
  gnc_extract_directory(&default_dir, file_name);
  gnc_gconf_set_string(GCONF_SECTION, KEY_LAST_PATH, default_dir, NULL);
  g_free(default_dir);
  g_free(file_name);
}


/********************************************************************
 * gnc_ui_qif_import_load_file_back_cb
 * 
 * Invoked when the "back" button is clicked on the load file page.
 ********************************************************************/

static gboolean
gnc_ui_qif_import_load_file_back_cb(GnomeDruidPage * page, gpointer arg1, 
				    gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  if (SCM_LISTP(wind->imported_files) &&
      (scm_ilength(wind->imported_files) > 0)) {
    gnome_druid_set_page(GNOME_DRUID(wind->druid), 
			 get_named_page(wind, "loaded_files_page"));
    return TRUE;
  }

  gnome_druid_set_page(GNOME_DRUID(wind->druid), 
		       get_named_page(wind, "start_page"));
  return TRUE;
}


/********************************************************************
 * gnc_ui_qif_import_load_file_next_cb
 * 
 * Invoked when the "next" button is clicked on the load file page.
 ********************************************************************/

static gboolean
gnc_ui_qif_import_load_file_next_cb(GnomeDruidPage * page, 
                                    gpointer arg1,
                                    gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  const char * path_to_load;
  const gchar * default_acctname = NULL;

  GList * format_strings;
  GList * listit;

  SCM make_qif_file   = scm_c_eval_string("make-qif-file");
  SCM qif_file_load   = scm_c_eval_string("qif-file:read-file");
  SCM qif_file_parse  = scm_c_eval_string("qif-file:parse-fields");
  SCM qif_file_loaded = scm_c_eval_string("qif-dialog:qif-file-loaded?");
  SCM unload_qif_file = scm_c_eval_string("qif-dialog:unload-qif-file");
  SCM check_from_acct = scm_c_eval_string("qif-file:check-from-acct");
  SCM default_acct    = scm_c_eval_string("qif-file:path-to-accountname");
  SCM qif_file_parse_results  = scm_c_eval_string("qif-file:parse-fields-results");
  SCM date_formats;
  SCM scm_filename;
  SCM scm_qiffile;
  SCM imported_files = SCM_EOL;
  SCM load_return, parse_return;

  int ask_date_format = FALSE;

  /* get the file name */ 
  path_to_load = gtk_entry_get_text(GTK_ENTRY(wind->filename_entry));

  /* check a few error conditions before we get started */
  if(strlen(path_to_load) == 0) {
    /* stay here if no file specified */
    gnc_error_dialog(wind->window, _("Please select a file to load.\n"));
    return TRUE;
  }
  else if ((strlen(path_to_load) > 0) && access(path_to_load, R_OK) < 0) {
    /* stay here if bad file */
    gnc_error_dialog(wind->window, 
		     _("File not found or read permission denied.\n"
		       "Please select another file."));
    return TRUE;
  }
  else {
    /* convert filename to scm */
    scm_filename   = scm_makfrom0str(path_to_load);
    imported_files = wind->imported_files;
    
    if(scm_call_2(qif_file_loaded, scm_filename, wind->imported_files)
       == SCM_BOOL_T) {
      gnc_error_dialog(wind->window,
                                _("That QIF file is already loaded.\n"
                                  "Please select another file."));
      return TRUE;
    }
    
    /* turn on the busy cursor */
    gnc_set_busy_cursor(NULL, TRUE);
    
    /* create the <qif-file> object */
    scm_qiffile          = scm_call_0(make_qif_file);    
    imported_files       = scm_cons(scm_qiffile, imported_files);    

    scm_gc_unprotect_object(wind->selected_file);      
    wind->selected_file  = scm_qiffile;    
    scm_gc_protect_object(wind->selected_file);      
    
    /* load the file */
    load_return = scm_call_3(qif_file_load, SCM_CAR(imported_files),
			     scm_filename, wind->ticker_map);
    
    /* a list returned is (#f error-message) for an error, 
     * (#t error-message) for a warning, or just #f for an 
     * exception. */
    if(SCM_LISTP(load_return) &&
       (SCM_CAR(load_return) == SCM_BOOL_T)) {
      const gchar *warn_str = SCM_STRING_CHARS(SCM_CADR(load_return));
      gnc_warning_dialog(GTK_WIDGET(wind->window),
			 _("QIF file load warning:\n%s"),
			 warn_str ? warn_str : "(null)");
    }

    /* check success of the file load */
    if(load_return == SCM_BOOL_F) {
      gnc_error_dialog(wind->window, 
		       _( "An error occurred while loading the QIF file."));
      return TRUE;
    }
    else if ((load_return != SCM_BOOL_T) &&
             (!SCM_LISTP(load_return) || 
              (SCM_CAR(load_return) != SCM_BOOL_T))) {
      const gchar *warn_str = SCM_STRING_CHARS(SCM_CADR(load_return));
      gnc_error_dialog(wind->window,
		       _("QIF file load failed:\n%s"),
		       warn_str ? warn_str : "(null)");

      imported_files = 
        scm_call_2(unload_qif_file, scm_qiffile, imported_files);
            
      scm_gc_unprotect_object(wind->imported_files);
      wind->imported_files = imported_files;
      scm_gc_protect_object(wind->imported_files);

      return TRUE;
    }
    else {
      /* call the field parser */
      parse_return = scm_call_1(qif_file_parse, SCM_CAR(imported_files));
      
      /* parser returns:
       *   success:	#t
       *   failure:	(#f . ((type . errror) ...))
       *   warning:	(#t . ((type . error) ...))
       *
       * warning means that (potentially) the date format is
       * ambiguous.  So search the results for the "date" type and if
       * it's found, set up the format selector page.
       */
      if(SCM_LISTP(parse_return) && 
         (SCM_CAR(parse_return) == SCM_BOOL_T)) {

	if ((date_formats = scm_call_2(qif_file_parse_results,
				       SCM_CDR(parse_return),
				       scm_str2symbol("date"))) != SCM_BOOL_F) {
	  format_strings = NULL;
	  while(SCM_LISTP(date_formats) && !SCM_NULLP(date_formats)) {
	    format_strings = 
	      g_list_append(format_strings, 
			    g_strdup(SCM_SYMBOL_CHARS(SCM_CAR(date_formats))));
	    date_formats = SCM_CDR(date_formats);
	  }
	  gtk_combo_set_popdown_strings(GTK_COMBO(wind->date_format_combo),
					format_strings);

	  for(listit = format_strings; listit; listit=listit->next) {
	    free(listit->data);
	    listit->data = NULL;
	  }
	  g_list_free(format_strings);
        
	  ask_date_format = TRUE;

	} else {
	  /* FIXME: we've got a "warning" but it's not the date! */
	  ;
	}
      }

      /* Can this ever happen??? */
      if(parse_return == SCM_BOOL_F) {
        gnc_error_dialog(wind->window,
			 _("An error occurred while parsing the QIF file."));
        imported_files = 
          scm_call_2(unload_qif_file, scm_qiffile, imported_files);
        return TRUE;
      }
      else if((parse_return != SCM_BOOL_T) &&
         (!SCM_LISTP(parse_return) ||
          (SCM_CAR(parse_return) != SCM_BOOL_T))) {
        const gchar *warn_str = SCM_STRING_CHARS(SCM_CDADR(parse_return));
        gnc_error_dialog(wind->window,
			 _("QIF file parse failed:\n%s"),
			 warn_str ? warn_str : "(null)");

        imported_files = 
          scm_call_2(unload_qif_file, scm_qiffile, imported_files);
        
        return TRUE;
      } 
    }
    
    scm_gc_unprotect_object(wind->imported_files);
    wind->imported_files = imported_files;
    scm_gc_protect_object(wind->imported_files);
    
    /* turn back the cursor */
    gnc_unset_busy_cursor(NULL);

    if(ask_date_format) {
      /* we need to get a date format, so go to the next page */
      return gnc_ui_qif_import_generic_next_cb(page, arg1, wind);
    }
    else if(scm_call_1(check_from_acct, SCM_CAR(imported_files)) != SCM_BOOL_T) {
      /* skip to the "ask account name" page */
      default_acctname =
	SCM_STRING_CHARS(scm_call_1(default_acct, SCM_CAR(imported_files)));
      gtk_entry_set_text(GTK_ENTRY(wind->acct_entry), default_acctname);
      
      gnome_druid_set_page(GNOME_DRUID(wind->druid),
                           get_named_page(wind, "account_name_page"));
      return TRUE;
    }
    else {
      /* skip ahead to the "loaded files" page */
      gnome_druid_set_page(GNOME_DRUID(wind->druid), 
                           get_named_page(wind, "loaded_files_page"));
      return TRUE;      
    }
  }
  
  return FALSE;
}

static gboolean
gnc_ui_qif_import_date_format_next_cb(GnomeDruidPage * page, 
                                      gpointer arg1,
                                      gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  SCM  reparse_dates   = scm_c_eval_string("qif-file:reparse-dates");
  SCM  check_from_acct = scm_c_eval_string("qif-file:check-from-acct");
  SCM  format_sym = 
    scm_str2symbol(gtk_entry_get_text(GTK_ENTRY(wind->date_format_entry)));
  
  scm_call_2(reparse_dates, wind->selected_file, format_sym);
  
  if(scm_call_1(check_from_acct, wind->selected_file) != SCM_BOOL_T) {
    SCM default_acct    = scm_c_eval_string("qif-file:path-to-accountname");
    const gchar * default_acctname;

    default_acctname = SCM_STRING_CHARS(scm_call_1(default_acct,
						wind->selected_file));
    gtk_entry_set_text(GTK_ENTRY(wind->acct_entry), default_acctname);

    return FALSE;
  }
  else {
    /* skip ahead to the "loaded files" page */
    gnome_druid_set_page(GNOME_DRUID(wind->druid), 
                         get_named_page(wind, "loaded_files_page"));
    
    return TRUE;      
  }
}


/****************************************************************
 * gnc_ui_qif_import_select_loaded_file_cb
 * callback when a file is clicked in the "loaded files" page
 ****************************************************************/

static void
gnc_ui_qif_import_select_loaded_file_cb(GtkCList   * list,
                                        int row, int column,
                                        GdkEvent   * event,
                                        gpointer  user_data)
{
  QIFImportWindow * wind = user_data;

  if(SCM_LISTP(wind->imported_files) && 
     (scm_ilength(wind->imported_files) > row)) {
    scm_gc_unprotect_object(wind->selected_file);
    wind->selected_file = scm_list_ref(wind->imported_files,
				       scm_int2num(row));   
    scm_gc_protect_object(wind->selected_file);
  } 
}

/********************************************************************
 * gnc_ui_qif_import_loaded_files_prepare_cb
 * 
 * Get the loaded files page ready for viewing
 ********************************************************************/

static void
gnc_ui_qif_import_loaded_files_prepare_cb(GnomeDruidPage * page,
                                          gpointer arg1,
                                          gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  update_file_page(wind);
  gnome_druid_set_buttons_sensitive(GNOME_DRUID(wind->druid),
                                    FALSE, TRUE, TRUE, TRUE); 
}


/********************************************************************
 * gnc_ui_qif_import_load_another_cb
 * Invoked when the "load another" button is clicked on the loaded
 * files page.
 ********************************************************************/

static void
gnc_ui_qif_import_load_another_cb(GtkButton * button,
                                  gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  
  gnome_druid_set_page(GNOME_DRUID(wind->druid),
                       get_named_page(wind, "load_file_page"));
  gnome_druid_set_buttons_sensitive(GNOME_DRUID(wind->druid),
                                    TRUE, TRUE, TRUE, TRUE); 
}


/********************************************************************
 * gnc_ui_qif_import_unload_cb
 * Invoked when the "unload" button is clicked on the loaded files
 * page.
 ********************************************************************/

static void
gnc_ui_qif_import_unload_file_cb(GtkButton * button,
                                 gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  SCM unload_qif_file = scm_c_eval_string("qif-dialog:unload-qif-file");
  SCM imported_files;
  
  if(wind->selected_file != SCM_BOOL_F) {
    imported_files = 
      scm_call_2(unload_qif_file, wind->selected_file, wind->imported_files);
  
    scm_gc_unprotect_object(wind->imported_files);
    wind->imported_files = imported_files;
    scm_gc_protect_object(wind->imported_files);

    scm_gc_unprotect_object(wind->selected_file);
    wind->selected_file = SCM_BOOL_F;
    scm_gc_protect_object(wind->selected_file);
     
    update_file_page(wind);
  }
}


/********************************************************************
 * update_file_page
 * update the list of loaded files 
 ********************************************************************/

static void
update_file_page(QIFImportWindow * wind)
{
  
  SCM       loaded_file_list = wind->imported_files;
  SCM       scm_qiffile = SCM_BOOL_F;
  SCM       qif_file_path;
  int       row;
  int       sel_item=-1;
  char      * row_text;

  /* clear the list */
  gtk_clist_clear(GTK_CLIST(wind->selected_file_list));
  qif_file_path = scm_c_eval_string("qif-file:path");
  
  /* iterate over all the imported files */
  gtk_clist_freeze(GTK_CLIST(wind->selected_file_list));
  
  while(!SCM_NULLP(loaded_file_list)) {  
    scm_qiffile = SCM_CAR(loaded_file_list);
    row_text    = SCM_STRING_CHARS(scm_call_1(qif_file_path, scm_qiffile));

    row = gtk_clist_append(GTK_CLIST(wind->selected_file_list),
                           &row_text);

    if(scm_qiffile == wind->selected_file) {
      sel_item = row;
    }

    loaded_file_list = SCM_CDR(loaded_file_list);
  }
  gtk_clist_thaw(GTK_CLIST(wind->selected_file_list));

  if(sel_item >= 0) {
    gtk_clist_select_row(GTK_CLIST(wind->selected_file_list), sel_item, 0);
  }

  /* Wheee! Look at me! I'm a little one-line hack to make stuff work! */
  gtk_widget_queue_resize (wind->window);
}


/********************************************************************
 * gnc_ui_qif_import_default_acct_next_cb
 * 
 * Invoked when the "next" button is clicked on the default acct page.
 ********************************************************************/

static gboolean
gnc_ui_qif_import_default_acct_next_cb(GnomeDruidPage * page,
                                       gpointer arg1,
                                       gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  const char   * acct_name = gtk_entry_get_text(GTK_ENTRY(wind->acct_entry));
  SCM    fix_default = scm_c_eval_string("qif-import:fix-from-acct");
  SCM    scm_name;

  if(!acct_name || acct_name[0] == 0) {
    gnc_warning_dialog(wind->window, _("You must enter an account name."));
    return TRUE;
  }
  else {
    scm_name = scm_makfrom0str(acct_name);
    scm_call_2(fix_default, wind->selected_file, scm_name);
    return FALSE;
  }
}

/********************************************************************
 * gnc_ui_qif_import_default_acct_back_cb
 * 
 * Invoked when the "back" button is clicked on the default acct page.
 * this unloads the current file.  
 ********************************************************************/

static gboolean
gnc_ui_qif_import_default_acct_back_cb(GnomeDruidPage * page,
                                       gpointer arg1,
                                       gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  SCM unload = scm_c_eval_string("qif-dialog:unload-qif-file");
  SCM files_list;

  files_list = scm_call_2(unload, wind->selected_file, wind->imported_files);

  scm_gc_unprotect_object(wind->imported_files);
  wind->imported_files = files_list;
  scm_gc_protect_object(wind->imported_files);
  
  scm_gc_unprotect_object(wind->selected_file);
  wind->selected_file = SCM_BOOL_F;
  scm_gc_protect_object(wind->selected_file);
  
  gnome_druid_set_page(GNOME_DRUID(wind->druid),
                       get_named_page(wind, "load_file_page"));
  gnome_druid_set_buttons_sensitive(GNOME_DRUID(wind->druid),
                                    TRUE, TRUE, TRUE, TRUE); 
  return TRUE;
}


/****************************************************************
 * update_account_picker_page 
 * Generic function to update an account_picker page.  This
 * generalizes the code shared whenever any QIF -> GNC mapper is
 * updating it's CLIST.  It asks the Scheme side to guess some account
 * translations and then shows the account name and suggested
 * translation in the Accounts page clist (acount picker list).
 ****************************************************************/

static void
update_account_picker_page(QIFImportWindow * wind, SCM make_display,
			   GtkWidget *list, SCM map_info, SCM * display_info)
{

  SCM  get_qif_name = scm_c_eval_string("qif-map-entry:qif-name");
  SCM  get_gnc_name = scm_c_eval_string("qif-map-entry:gnc-name");
  SCM  get_new      = scm_c_eval_string("qif-map-entry:new-acct?");
  SCM  accts_left;
  int  sel_row=0;
  char * row_text[3];
  int  row;

  /* get the old selection row */
  sel_row = (GTK_CLIST(list))->focus_row;

  /* now get the list of strings to display in the clist widget */
  accts_left = scm_call_3(make_display,
			  wind->imported_files,
			  map_info, 
			  wind->gnc_acct_info);

  scm_gc_unprotect_object(*display_info);
  *display_info = accts_left;  
  scm_gc_protect_object(*display_info);
  
  gtk_clist_column_titles_passive (GTK_CLIST(list));

  /* clear the list */
  gtk_clist_clear(GTK_CLIST(list));

  /* update the text in the boxes */
  gtk_clist_freeze(GTK_CLIST(list));

  gtk_clist_set_column_justification(GTK_CLIST(list),
                                     2,
                                     GTK_JUSTIFY_CENTER);

  row_text[2] = "";

  while(!SCM_NULLP(accts_left)) {
    row_text[0] = SCM_STRING_CHARS(scm_call_1(get_qif_name, SCM_CAR(accts_left)));
    row_text[1] = SCM_STRING_CHARS(scm_call_1(get_gnc_name, SCM_CAR(accts_left)));
    
    row = gtk_clist_append(GTK_CLIST(list), row_text);

    gnc_clist_set_check (GTK_CLIST(list), row, 2,
                         scm_call_1(get_new, SCM_CAR(accts_left)) == SCM_BOOL_T);

    accts_left = SCM_CDR(accts_left);
  }

  gtk_clist_thaw(GTK_CLIST(list));

  /* move to the old selected row */
  (GTK_CLIST(list))->focus_row = sel_row;
  gtk_clist_moveto(GTK_CLIST(list), sel_row, 0, 0.0, 0.0);
}


/****************************************************************
 * update_accounts_page 
 * update the QIF account -> GNC Account picker
 ****************************************************************/

static void
update_accounts_page(QIFImportWindow * wind)
{

  SCM  make_account_display = scm_c_eval_string("qif-dialog:make-account-display");

  update_account_picker_page (wind, make_account_display, wind->acct_list,
			      wind->acct_map_info, &(wind->acct_display_info));
}

/****************************************************************
 * update_categories_page 
 * update the QIF category -> GNC Account picker
 ****************************************************************/

static void
update_categories_page(QIFImportWindow * wind)
{
  SCM  make_category_display = scm_c_eval_string("qif-dialog:make-category-display");

  update_account_picker_page (wind, make_category_display, wind->cat_list,
			      wind->cat_map_info, &(wind->cat_display_info));
}

/****************************************************************
 * update_memo_page 
 * update the QIF memo -> GNC Account picker
 ****************************************************************/

static void
update_memo_page(QIFImportWindow * wind)
{
  SCM  make_memo_display = scm_c_eval_string("qif-dialog:make-memo-display");

  update_account_picker_page (wind, make_memo_display, wind->memo_list,
			      wind->memo_map_info, &(wind->memo_display_info));
}

/********************************************************************
 * select_line
 * generic function to process the selection when a user tries to edit
 * an account mapping in one of the "map QIF * to GNC" pages.  This
 * calls out to the account picker, and then then updates the
 * appropriate data structures.  Finally, it will call the update_page
 * function.
 ********************************************************************/
static void
select_line (QIFImportWindow *wind, gint row, SCM display_info, SCM map_info,
	     void (*update_page)(QIFImportWindow *))
{
  SCM   get_name = scm_c_eval_string("qif-map-entry:qif-name");
  SCM   selected_acct;
  
  /* find the <qif-map-entry> corresponding to the selected row */
  selected_acct = scm_list_ref(display_info, scm_int2num(row));
  
  /* call the account picker to update it */
  selected_acct = qif_account_picker_dialog(wind, selected_acct);

  scm_hash_set_x(map_info, scm_call_1(get_name, selected_acct), selected_acct);

  /* update display */
  update_page(wind);
}

/********************************************************************
 * gnc_ui_qif_import_account_line_select_cb
 * when an account is clicked for editing in the "map QIF accts to GNC"
 * page.
 ********************************************************************/

static void
gnc_ui_qif_import_account_line_select_cb(GtkCList * clist, gint row,
                                         gint column, GdkEvent * event,
                                         gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  select_line (wind, row, wind->acct_display_info, wind->acct_map_info,
	       update_accounts_page);
}

/********************************************************************
 * gnc_ui_qif_import_category_line_select_cb
 * when a cat is clicked for editing in the "map QIF cats to GNC"
 * page.
 ********************************************************************/

static void
gnc_ui_qif_import_category_line_select_cb(GtkCList * clist, gint row,
                                          gint column, GdkEvent * event,
                                          gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  select_line (wind, row, wind->cat_display_info, wind->cat_map_info,
	       update_categories_page);
}

/********************************************************************
 *  gnc_ui_qif_import_memo_line_select_cb
 *  when a memo is clicked for editing in the "map QIF memos to GNC"
 *  page.
 ********************************************************************/

static void
gnc_ui_qif_import_memo_line_select_cb(GtkCList * clist, gint row,
                                      gint column, GdkEvent * event,
                                      gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  select_line (wind, row, wind->memo_display_info, wind->memo_map_info,
	       update_memo_page);
}


/********************************************************************
 * gnc_ui_qif_import_accounts_prepare_cb
 ********************************************************************/

static void
gnc_ui_qif_import_accounts_prepare_cb(GnomeDruidPage * page,
                                      gpointer arg1,
                                      gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  gnc_set_busy_cursor(NULL, TRUE);
  update_accounts_page(wind);
  gnc_unset_busy_cursor(NULL);
}


/********************************************************************
 * gnc_ui_qif_import_categories_prepare_cb
 ********************************************************************/

static void
gnc_ui_qif_import_categories_prepare_cb(GnomeDruidPage * page,
                                        gpointer arg1,
                                        gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  gnc_set_busy_cursor(NULL, TRUE);
  update_categories_page(wind);
  gnc_unset_busy_cursor(NULL);
}

/********************************************************************
 * gnc_ui_qif_import_memo_prepare_cb
 ********************************************************************/

static void
gnc_ui_qif_import_memo_prepare_cb(GnomeDruidPage * page,
                                        gpointer arg1,
                                        gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  gnc_set_busy_cursor(NULL, TRUE);
  update_memo_page(wind);
  gnc_unset_busy_cursor(NULL);
}


/****************************************************************
 * gnc_ui_qif_import_convert
 * do the work of actually translating QIF xtns to GNC xtns.  Fill in 
 * the match page if there are matches. 
 ****************************************************************/

static gboolean
gnc_ui_qif_import_convert(QIFImportWindow * wind)
{

  SCM   qif_to_gnc      = scm_c_eval_string("qif-import:qif-to-gnc");
  SCM   find_duplicates = scm_c_eval_string("gnc:group-find-duplicates");
  SCM   retval;
  SCM   current_xtn;

  GnomeDruidPage * gtkpage;
  QIFDruidPage * page;
  GList        * pageptr;
  Transaction  * gnc_xtn;
  Split        * gnc_split;
  gnc_commodity * old_commodity;

  const char * mnemonic = NULL; 
  const char * namespace = NULL;
  const char * fullname = NULL;
  const gchar * row_text[4] = { NULL, NULL, NULL, NULL };
  int  rownum;

  /* get the default currency */
  const char * currname = gtk_entry_get_text(GTK_ENTRY(wind->currency_entry));

  /* busy cursor */
  gnc_suspend_gui_refresh ();
  gnc_set_busy_cursor(NULL, TRUE);

  /* get any changes to the imported stocks */
  for(pageptr = wind->commodity_pages; pageptr; pageptr=pageptr->next) {
    gtkpage   = GNOME_DRUID_PAGE(pageptr->data); 
    page      = gtk_object_get_data(GTK_OBJECT(gtkpage), "page_struct");
    
    mnemonic  = gtk_entry_get_text(GTK_ENTRY(page->new_mnemonic_entry));
    namespace = gnc_ui_namespace_picker_ns((page->new_type_combo));
    fullname  = gtk_entry_get_text(GTK_ENTRY(page->new_name_entry));
    
    gnc_commodity_set_namespace(page->commodity, namespace);
    gnc_commodity_set_fullname(page->commodity, fullname);
    gnc_commodity_set_mnemonic(page->commodity, mnemonic);

    old_commodity = page->commodity;
    page->commodity = gnc_commodity_table_insert(gnc_get_current_commodities(),
                                                 page->commodity);
    if (old_commodity != page->commodity) {
	scm_hash_remove_x(wind->stock_hash, scm_makfrom0str(fullname));
    }
  }

  /* call a scheme function to do the work.  The return value is an
   * account group containing all the new accounts and transactions */
  retval = scm_apply(qif_to_gnc, 
		     SCM_LIST6(wind->imported_files,
			       wind->acct_map_info, 
			       wind->cat_map_info,
			       wind->memo_map_info,
			       wind->stock_hash,
			       scm_makfrom0str(currname)),
		     SCM_EOL);

  gnc_unset_busy_cursor(NULL);

  if(retval == SCM_BOOL_F) {
    gnc_error_dialog(wind->window,
		     _("An error occurred while importing "
		       "QIF transactions into GnuCash. Your "
		       "accounts are unchanged."));    
    scm_gc_unprotect_object(wind->imported_account_group);
    wind->imported_account_group = SCM_BOOL_F;
    scm_gc_protect_object(wind->imported_account_group);
  }
  else {
    scm_gc_unprotect_object(wind->imported_account_group);
    wind->imported_account_group = retval;
    scm_gc_protect_object(wind->imported_account_group);

    /* now detect duplicate transactions */ 
    gnc_set_busy_cursor(NULL, TRUE);
    retval = scm_call_2(find_duplicates, 
			scm_c_eval_string("(gnc:get-current-group)"),
			wind->imported_account_group);
    gnc_unset_busy_cursor(NULL);
    
    scm_gc_unprotect_object(wind->match_transactions);
    wind->match_transactions = retval;
    scm_gc_protect_object(wind->match_transactions);

    /* skip to the last page if we couldn't find duplicates 
     * in the new group */
    if((retval == SCM_BOOL_F) ||
       (SCM_NULLP(retval))) {

      gnc_resume_gui_refresh();
      return FALSE;
    }

    gtk_clist_column_titles_passive (GTK_CLIST(wind->new_transaction_list));

    /* otherwise, make up the display for the duplicates page */
    gtk_clist_clear(GTK_CLIST(wind->new_transaction_list));
    gtk_clist_freeze(GTK_CLIST(wind->new_transaction_list));

    while(!SCM_NULLP(retval)) {
      current_xtn = SCM_CAAR(retval);
      gnc_xtn     = (Transaction *)gw_wcp_get_ptr(current_xtn);
      gnc_split   = xaccTransGetSplit(gnc_xtn, 0);  

      row_text[0] = gnc_print_date(xaccTransRetDatePostedTS(gnc_xtn));
      row_text[1] = xaccTransGetDescription(gnc_xtn);

      if(xaccTransCountSplits(gnc_xtn) > 2) {
        row_text[2] = g_strdup(_("(split)")); 
      }
      else {
        row_text[2] = 
          xaccPrintAmount(gnc_numeric_abs(xaccSplitGetValue(gnc_split)),
                          gnc_account_print_info
                          (xaccSplitGetAccount(gnc_split), TRUE));
      }

      rownum = gtk_clist_append(GTK_CLIST(wind->new_transaction_list),
                                (gchar **) row_text);      
      
      retval      = SCM_CDR(retval); 
    }

    gtk_clist_columns_autosize(GTK_CLIST(wind->new_transaction_list));

    gtk_clist_thaw(GTK_CLIST(wind->new_transaction_list));        
    gtk_clist_select_row(GTK_CLIST(wind->new_transaction_list), 0, 0);
  }  

  gnc_resume_gui_refresh();
  return TRUE;
}


/********************************************************************
 * gnc_ui_qif_import_memo_next_cb
 ********************************************************************/

static gboolean
gnc_ui_qif_import_memo_next_cb(GnomeDruidPage * page,
                               gpointer arg1,
                               gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  SCM any_new      = scm_c_eval_string("qif-import:any-new-accts?");
  SCM update_stock = scm_c_eval_string("qif-import:update-stock-hash");

  int show_matches;
  
  /* if any accounts are new, ask about the currency; else,
   * just skip that page */
  if((scm_call_1(any_new, wind->acct_map_info) == SCM_BOOL_T) ||
     (scm_call_1(any_new, wind->cat_map_info) == SCM_BOOL_T)) {
    /* go to currency page */ 
    return gnc_ui_qif_import_generic_next_cb(page, arg1, wind);
  }
  else {
    /* if we need to look at stocks, do that, otherwise import
     * xtns and go to the duplicates page */
    scm_gc_unprotect_object(wind->new_stocks);
    wind->new_stocks = scm_call_3(update_stock, wind->stock_hash,
				  wind->ticker_map, wind->acct_map_info);
    scm_gc_protect_object(wind->new_stocks);
    
    if(wind->new_stocks != SCM_BOOL_F) {
      if(wind->show_doc_pages) {
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "commodity_doc_page"));
      }
      else {
        gnc_ui_qif_import_commodity_prepare_cb(page, arg1, wind);
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             GNOME_DRUID_PAGE(wind->commodity_pages->data));
      }
      return TRUE;
    }
    else {
      /* it's time to import the accounts. */
      show_matches = gnc_ui_qif_import_convert(wind);
      
      if(show_matches) {
        if(wind->show_doc_pages) {
          /* check for matches .. the docpage does it automatically */ 
          gnome_druid_set_page(GNOME_DRUID(wind->druid),
                               get_named_page(wind, "match_doc_page"));
        }
        else {
          gnome_druid_set_page(GNOME_DRUID(wind->druid),
                               get_named_page(wind, "match_duplicates_page"));
        }
      }
      else {
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "end_page"));
      }
      return TRUE;
    }
  }
}

  
/********************************************************************
 * gnc_ui_qif_import_currency_next_cb
 ********************************************************************/

static gboolean
gnc_ui_qif_import_currency_next_cb(GnomeDruidPage * page,
                                   gpointer arg1,
                                   gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  SCM update_stock = scm_c_eval_string("qif-import:update-stock-hash");
  int show_matches;

  gnc_set_busy_cursor(NULL, TRUE);
  scm_gc_unprotect_object(wind->new_stocks);
  wind->new_stocks =  scm_call_3(update_stock, wind->stock_hash, 
				 wind->ticker_map, wind->acct_map_info);
  scm_gc_protect_object(wind->new_stocks);
  
  if(wind->new_stocks != SCM_BOOL_F) {
    if(wind->show_doc_pages) {
      gnome_druid_set_page(GNOME_DRUID(wind->druid),
                           get_named_page(wind, "commodity_doc_page"));
    }
    else {
      gnc_ui_qif_import_commodity_prepare_cb(page, arg1, user_data);
      gnome_druid_set_page(GNOME_DRUID(wind->druid),
                           GNOME_DRUID_PAGE(wind->commodity_pages->data));
    }
  }
  else {
    /* it's time to import the accounts. */
    show_matches = gnc_ui_qif_import_convert(wind);
    
    if(show_matches) {
      if(wind->show_doc_pages) {
        /* check for matches .. the docpage does it automatically */ 
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "match_doc_page"));
      }
      else {
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "match_duplicates_page"));
      }
    }
    else {
      gnome_druid_set_page(GNOME_DRUID(wind->druid),
                           get_named_page(wind, "end_page"));
    }
  }

  gnc_unset_busy_cursor(NULL);
  return TRUE;
}


static gboolean
gnc_ui_qif_import_comm_check_cb(GnomeDruidPage * page,
                                gpointer arg1,
                                gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  QIFDruidPage    * qpage = 
    gtk_object_get_data(GTK_OBJECT(page), "page_struct");
  
  const char * namespace = gnc_ui_namespace_picker_ns(qpage->new_type_combo);
  const char * name      = gtk_entry_get_text(GTK_ENTRY(qpage->new_name_entry));
  const char * mnemonic  = gtk_entry_get_text(GTK_ENTRY(qpage->new_mnemonic_entry));
  int  show_matches;

  if(!namespace || (namespace[0] == 0)) {
    gnc_warning_dialog(wind->window,
		       _("You must enter a Type for the commodity."));
    return TRUE;
  }
  else if(!name || (name[0] == 0)) {
    gnc_warning_dialog(wind->window,
		       _("You must enter a name for the commodity."));
    return TRUE;
  }
  else if(!mnemonic || (mnemonic[0] == 0)) {
    gnc_warning_dialog
      (wind->window, _("You must enter an abbreviation for the commodity."));
    return TRUE;
  }

  if (gnc_commodity_namespace_is_iso (namespace) &&
      !gnc_commodity_table_lookup (gnc_get_current_commodities (),
                                   namespace, mnemonic))
  {
    gnc_warning_dialog(wind->window,
		       _("You must enter an existing national "
			 "currency or enter a different type."));

    return TRUE;
  }

  if(page == (g_list_last(wind->commodity_pages))->data) {
    /* it's time to import the accounts. */
    show_matches = gnc_ui_qif_import_convert(wind);
    
    if(show_matches) {
      if(wind->show_doc_pages) {
        /* check for matches .. the docpage does it automatically */ 
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "match_doc_page"));
      }
      else {
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "match_duplicates_page"));
      }
    }
    else {
      gnome_druid_set_page(GNOME_DRUID(wind->druid),
                           get_named_page(wind, "end_page"));
    } 
    return TRUE;
  }
  else {
    return FALSE;
  }
}


/********************************************************************
 * gnc_ui_qif_import_commodity_prepare_cb
 * build a mapping of QIF stock name to a gnc_commodity 
 ********************************************************************/

static void
gnc_ui_qif_import_commodity_prepare_cb(GnomeDruidPage * page,
                                       gpointer arg1,
                                       gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  SCM   hash_ref  = scm_c_eval_string("hash-ref");
  SCM   stocks;
  SCM   comm_ptr_token;
  SCM   show_matches;

  gnc_commodity  * commodity;
  GnomeDruidPage * back_page = get_named_page(wind, "commodity_doc_page");  
  QIFDruidPage   * new_page;
  
  /* only set up once */
  if(wind->commodity_pages) return;
  
  /* this shouldn't happen, but DTRT if it does */
  if(SCM_NULLP(wind->new_stocks)) {
    printf("somehow got to commodity doc page with nothing to do... BUG!\n");
    if (gnc_ui_qif_import_convert(wind))
      show_matches = SCM_BOOL_T;
    else
      show_matches = SCM_BOOL_F;
    
    if(show_matches) {
      if(wind->show_doc_pages) {
        /* check for matches .. the docpage does it automatically */ 
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "match_doc_page"));
      }
      else {
        gnome_druid_set_page(GNOME_DRUID(wind->druid),
                             get_named_page(wind, "match_duplicates_page"));
      }
    }
    else {
      gnome_druid_set_page(GNOME_DRUID(wind->druid),
                           get_named_page(wind, "end_page"));
    } 
  }

  /* insert new pages, one for each stock */
  gnc_set_busy_cursor(NULL, TRUE);
  stocks = wind->new_stocks;
  while(!SCM_NULLP(stocks) && (stocks != SCM_BOOL_F)) {
    comm_ptr_token = scm_call_2(hash_ref, wind->stock_hash, SCM_CAR(stocks));
    commodity      = gw_wcp_get_ptr(comm_ptr_token);
    
    new_page = make_qif_druid_page(commodity);

    gtk_signal_connect(GTK_OBJECT(new_page->page), "next",
                       GTK_SIGNAL_FUNC(gnc_ui_qif_import_comm_check_cb),
                       wind);

    wind->commodity_pages = g_list_append(wind->commodity_pages, 
                                          new_page->page);

    gnome_druid_insert_page(GNOME_DRUID(wind->druid),
                            back_page, 
                            GNOME_DRUID_PAGE(new_page->page));
    back_page = GNOME_DRUID_PAGE(new_page->page);
    
    stocks = SCM_CDR(stocks);
    gtk_widget_show_all(new_page->page);
  }
  gnc_unset_busy_cursor(NULL);

  gnc_druid_set_colors (GNOME_DRUID (wind->druid));
}

static QIFDruidPage *
make_qif_druid_page(gnc_commodity * comm)
{
  
  QIFDruidPage * retval = g_new0(QIFDruidPage, 1);
  GtkWidget * top_vbox;
  GtkWidget * info_label;
  GtkWidget * next_label;
  GtkWidget * temp;
  char      * title = NULL;
  const char * str;
  GnomeDruidPageStandard * page;

  /* make the page widget */
  retval->page = gnome_druid_page_standard_new_with_vals("", NULL, NULL);
  retval->commodity = comm;
  gtk_object_set_data(GTK_OBJECT(retval->page),
                      "page_struct", (gpointer)retval);

  page = GNOME_DRUID_PAGE_STANDARD(retval->page);

  /* save the old commodity name */
  str = gnc_commodity_get_mnemonic(comm);
  str = str ? str : "";
  title = g_strdup_printf(_("Enter information about \"%s\""), str);

  gnome_druid_page_standard_set_background(page, & std_bg_color);  
  gnome_druid_page_standard_set_logo_background(page, & std_logo_bg_color);
  gnome_druid_page_standard_set_title_foreground (page, & std_title_color);
  gnome_druid_page_standard_set_title(page, title);
  g_free(title);
  
  top_vbox = gtk_vbox_new(FALSE, 3);
  gtk_box_pack_start(GTK_BOX(page->vbox), top_vbox, FALSE, FALSE, 0);
                     
  info_label = 
    gtk_label_new(_("Pick the commodity's exchange or listing "
                    "(NASDAQ, NYSE, etc)."));

  gtk_label_set_justify (GTK_LABEL(info_label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start(GTK_BOX(top_vbox), info_label, TRUE, TRUE, 0);

  temp = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(top_vbox), temp, FALSE, FALSE, 0);

  info_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(temp), info_label, TRUE, TRUE, 0);

  retval->new_type_combo = gtk_combo_new(); 
  gtk_box_pack_start(GTK_BOX(temp),
                     retval->new_type_combo, TRUE, TRUE, 0);

  info_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(temp), info_label, TRUE, TRUE, 0);

  gnc_ui_update_namespace_picker(retval->new_type_combo, 
                                 gnc_commodity_get_namespace(comm),
                                 DIAG_COMM_ALL);

  info_label = 
    gtk_label_new(_("Enter the full name of the commodity, "
                    "such as \"Red Hat Stock\""));
  
  gtk_label_set_justify (GTK_LABEL(info_label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start(GTK_BOX(top_vbox), info_label, TRUE, TRUE, 0);
  
  temp = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(top_vbox), temp, FALSE, FALSE, 0);

  info_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(temp), info_label, TRUE, TRUE, 0);

  retval->new_name_entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(temp), retval->new_name_entry,
                     TRUE, TRUE, 0);
  gtk_entry_set_text(GTK_ENTRY(retval->new_name_entry),
                     gnc_commodity_get_fullname(comm));
  
  info_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(temp), info_label, TRUE, TRUE, 0);

  info_label = 
    gtk_label_new(_("Enter the ticker symbol (such as \"RHAT\") or "
                    "other unique abbreviation for the name."));
  
  gtk_label_set_justify (GTK_LABEL(info_label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start(GTK_BOX(top_vbox), info_label, TRUE, TRUE, 0);
 
  temp = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(top_vbox), temp, FALSE, FALSE, 0);

  info_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(temp), info_label, TRUE, TRUE, 0);

  retval->new_mnemonic_entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(temp), retval->new_mnemonic_entry,
                     TRUE, TRUE, 0);
  gtk_entry_set_text(GTK_ENTRY(retval->new_mnemonic_entry),
                     gnc_commodity_get_mnemonic(comm));
  
  info_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(temp), info_label, TRUE, TRUE, 0);

  next_label = gtk_label_new(_("Click \"Next\" to accept the information "
                               "and move on."));
  gtk_label_set_justify (GTK_LABEL(next_label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_end(GTK_BOX(top_vbox), next_label, TRUE, TRUE, 0);

  
  return retval;
}


static void
refresh_old_transactions(QIFImportWindow * wind, int selection)
{
  SCM          possible_matches;
  SCM          current_xtn;
  SCM          selected;
  Transaction  * gnc_xtn;
  Split        * gnc_split;
  const gchar  * row_text[4] = { NULL, NULL, NULL, NULL };
  int          rownum;

  gtk_clist_column_titles_passive (GTK_CLIST(wind->old_transaction_list));

  gtk_clist_clear(GTK_CLIST(wind->old_transaction_list));
  gtk_clist_freeze(GTK_CLIST(wind->old_transaction_list));

  gtk_clist_set_column_justification(GTK_CLIST(wind->old_transaction_list),
                                     3,
                                     GTK_JUSTIFY_CENTER);

  if(wind->match_transactions != SCM_BOOL_F) {
    possible_matches = SCM_CDR(scm_list_ref(wind->match_transactions,
                                  scm_int2num(wind->selected_transaction)));
    scm_call_2(scm_c_eval_string("qif-import:refresh-match-selection"),
	       possible_matches, scm_int2num(selection));

    row_text[3] = "";

    while(!SCM_NULLP(possible_matches)) {
      current_xtn = SCM_CAR(possible_matches);
      gnc_xtn     = (Transaction *)gw_wcp_get_ptr(SCM_CAR(current_xtn));
      selected    = SCM_CDR(current_xtn);
      gnc_split   = xaccTransGetSplit(gnc_xtn, 0);  
      
      row_text[0] = gnc_print_date(xaccTransRetDatePostedTS(gnc_xtn));
      row_text[1] = xaccTransGetDescription(gnc_xtn);
      
      if(xaccTransCountSplits(gnc_xtn) > 2) {
        row_text[2] = _("(split)");
      }
      else {
        row_text[2] = 
          xaccPrintAmount(gnc_numeric_abs(xaccSplitGetValue(gnc_split)),
                          gnc_account_print_info
                          (xaccSplitGetAccount(gnc_split), TRUE));
      }
      
      rownum = gtk_clist_append(GTK_CLIST(wind->old_transaction_list),
                                (gchar **) row_text);

      gnc_clist_set_check (GTK_CLIST(wind->old_transaction_list),
                           rownum, 3, selected != SCM_BOOL_F);

      possible_matches = SCM_CDR(possible_matches);
    }
  }

  gtk_clist_columns_autosize (GTK_CLIST(wind->old_transaction_list));

  gtk_clist_thaw(GTK_CLIST(wind->old_transaction_list));
}

static void
gnc_ui_qif_import_duplicate_new_select_cb(GtkCList * clist, int row, int col, 
                                          GdkEvent * ev, gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  wind->selected_transaction = row;
  refresh_old_transactions(wind, -1);
}


static void
gnc_ui_qif_import_duplicate_old_select_cb(GtkCList * clist, int row, int col, 
                                          GdkEvent * ev, gpointer user_data)
{
  QIFImportWindow * wind = user_data;

  refresh_old_transactions(wind, row);
}

static void
gnc_ui_qif_import_finish_cb(GnomeDruidPage * gpage, 
                            gpointer arg1, 
                            gpointer user_data)
{
  
  SCM   save_map_prefs = scm_c_eval_string("qif-import:save-map-prefs");
  SCM   cat_and_merge = scm_c_eval_string("gnc:group-catenate-and-merge");
  SCM   prune_xtns = scm_c_eval_string("gnc:prune-matching-transactions");
  
  QIFImportWindow * wind = user_data;

  gnc_suspend_gui_refresh();

  /* prune the old transactions marked as dupes */
  if(wind->match_transactions != SCM_BOOL_F) {
    scm_call_1(prune_xtns, wind->match_transactions);
  }

  /* actually add in the new transactions. */
  scm_call_2(cat_and_merge, 
	     scm_c_eval_string("(gnc:get-current-group)"),
	     wind->imported_account_group);
  
  gnc_resume_gui_refresh();
  
  /* write out mapping info before destroying the window */
  scm_apply(save_map_prefs, 
	    SCM_LIST4(wind->acct_map_info, wind->cat_map_info,
		      wind->memo_map_info, wind->stock_hash),
	    SCM_EOL);
  
  gnc_ui_qif_import_druid_destroy(wind);  
}

static void
gnc_ui_qif_import_cancel_cb (GnomeDruid * druid, 
                             gpointer user_data)
{
  QIFImportWindow * wind = user_data;
  
  gnc_ui_qif_import_druid_destroy(wind);
}

SCM
gnc_ui_qif_import_druid_get_mappings(QIFImportWindow * w)
{
  return SCM_LIST3(w->acct_map_info, 
                   w->cat_map_info,
                   w->memo_map_info);
}


/* ======================================================== */

static gboolean
show_handler (const char *class, gint component_id,
	      gpointer user_data, gpointer iter_data)
{
  QIFImportWindow *qif_win = user_data;

  if (!qif_win)
    return(FALSE);
  gtk_window_present (GTK_WINDOW(qif_win->window));
  return(TRUE);
}

void
gnc_file_qif_import (void) 
{
  if (gnc_forall_gui_components (DRUID_QIF_IMPORT_CM_CLASS,
				 show_handler, NULL))
      return;

  /* pop up the QIF File Import dialog box */
  gnc_ui_qif_import_druid_make();
}

/********************************************************************
 * gnc_ui_qif_import_druid_make() 
 * build the druid.
 ********************************************************************/

QIFImportWindow *
gnc_ui_qif_import_druid_make(void)
{
  
  QIFImportWindow * retval;
  GladeXML        * xml;
  SCM  load_map_prefs;
  SCM  mapping_info;
  SCM  create_ticker_map;
  int  i;

  char * pre_page_names[NUM_PRE_PAGES] = {
    "start_page", "load_file_page", "date_format_page", "account_name_page",
    "loaded_files_page", "account_doc_page", "account_match_page", 
    "category_doc_page", "category_match_page", "memo_doc_page",
    "memo_match_page", "currency_page", "commodity_doc_page"
  };

  char * post_page_names[NUM_POST_PAGES] = {
    "match_doc_page", "match_duplicates_page", "end_page"
  };

  char * doc_page_names[NUM_DOC_PAGES] = {
    "start_page", "account_doc_page", "category_doc_page", 
    "commodity_doc_page", "memo_doc_page", "match_doc_page"    
  };

  retval = g_new0(QIFImportWindow, 1);

  xml = gnc_glade_xml_new ("qif.glade", "QIF Import Druid");

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_cancel_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_cancel_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_generic_next_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_generic_next_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_generic_back_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_generic_back_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_select_file_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_select_file_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_load_file_back_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_load_file_back_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_load_file_next_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_load_file_next_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_date_format_next_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_date_format_next_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_select_loaded_file_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_select_loaded_file_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_loaded_files_prepare_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_loaded_files_prepare_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_load_another_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_load_another_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_unload_file_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_unload_file_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_default_acct_next_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_default_acct_next_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_default_acct_back_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_default_acct_back_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_account_line_select_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_account_line_select_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_category_line_select_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_category_line_select_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_memo_line_select_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_memo_line_select_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_accounts_prepare_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_accounts_prepare_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_categories_prepare_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_categories_prepare_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_memo_prepare_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_memo_prepare_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_memo_next_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_memo_next_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_currency_next_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_currency_next_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_commodity_prepare_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_commodity_prepare_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_duplicate_new_select_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_duplicate_new_select_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_duplicate_old_select_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_duplicate_old_select_cb), retval);

  glade_xml_signal_connect_data
    (xml, "gnc_ui_qif_import_finish_cb",
     GTK_SIGNAL_FUNC (gnc_ui_qif_import_finish_cb), retval);

  retval->window = glade_xml_get_widget (xml, "QIF Import Druid");

  retval->imported_files    =  SCM_EOL;
  retval->selected_file     =  SCM_BOOL_F;
  retval->gnc_acct_info     =  SCM_BOOL_F;
  retval->cat_display_info  =  SCM_BOOL_F;
  retval->cat_map_info      =  SCM_BOOL_F;
  retval->acct_display_info =  SCM_BOOL_F;
  retval->acct_map_info     =  SCM_BOOL_F;
  retval->memo_display_info =  SCM_BOOL_F;
  retval->memo_map_info     =  SCM_BOOL_F;
  retval->stock_hash        =  SCM_BOOL_F;
  retval->new_stocks        =  SCM_BOOL_F;
  retval->ticker_map        =  SCM_BOOL_F;
  retval->imported_account_group   = SCM_BOOL_F;
  retval->match_transactions = SCM_BOOL_F;
  retval->selected_transaction = 0;
  
  retval->druid          = glade_xml_get_widget (xml, "qif_import_druid");
  retval->filename_entry = glade_xml_get_widget (xml, "qif_filename_entry");
  retval->acct_entry     = glade_xml_get_widget (xml, "qif_account_entry");
  retval->date_format_combo = glade_xml_get_widget (xml, "date_format_combo");
  retval->date_format_entry = glade_xml_get_widget (xml, "date_format_entry");
  retval->selected_file_list = glade_xml_get_widget(xml, "selected_file_list");
  retval->currency_picker = glade_xml_get_widget (xml, "currency_combo");
  retval->currency_entry = glade_xml_get_widget (xml, "currency_entry");
  retval->acct_list      = glade_xml_get_widget (xml, "account_page_list");
  retval->cat_list       = glade_xml_get_widget (xml, "category_page_list");
  retval->memo_list      = glade_xml_get_widget (xml, "memo_page_list");
  retval->new_transaction_list = 
    glade_xml_get_widget (xml, "new_transaction_list");
  retval->old_transaction_list = 
    glade_xml_get_widget (xml, "old_transaction_list");
  
  retval->pre_comm_pages   = NULL;
  retval->post_comm_pages  = NULL;
  retval->doc_pages        = NULL;
  retval->commodity_pages = NULL;

  retval->show_doc_pages = 
    gnc_gconf_get_bool("dialogs/import/qif", "show_doc", NULL);

  for(i=0; i < NUM_PRE_PAGES; i++) {
    retval->pre_comm_pages = 
      g_list_append(retval->pre_comm_pages, 
                    glade_xml_get_widget (xml, pre_page_names[i]));
  }
  for(i=0; i < NUM_POST_PAGES; i++) {
    retval->post_comm_pages = 
      g_list_append(retval->post_comm_pages, 
                    glade_xml_get_widget (xml, post_page_names[i]));
  }
  for(i=0; i < NUM_DOC_PAGES; i++) {
    retval->doc_pages = 
      g_list_append(retval->doc_pages, 
                    glade_xml_get_widget (xml, doc_page_names[i]));
  }
  
  /* load the saved-state of the mappings from Quicken accounts and
   * categories to gnucash accounts */
  load_map_prefs = scm_c_eval_string("qif-import:load-map-prefs");

  mapping_info = scm_call_0(load_map_prefs);
  retval->gnc_acct_info    = scm_list_ref(mapping_info, scm_int2num(0));
  retval->acct_map_info    = scm_list_ref(mapping_info, scm_int2num(1));
  retval->cat_map_info     = scm_list_ref(mapping_info, scm_int2num(2));
  retval->memo_map_info    = scm_list_ref(mapping_info, scm_int2num(3));
  retval->stock_hash       = scm_list_ref(mapping_info, scm_int2num(4));

  create_ticker_map = scm_c_eval_string("make-ticker-map");
  retval->ticker_map = scm_call_0(create_ticker_map);
  
  scm_gc_protect_object(retval->imported_files);
  scm_gc_protect_object(retval->selected_file);
  scm_gc_protect_object(retval->gnc_acct_info);
  scm_gc_protect_object(retval->cat_display_info);
  scm_gc_protect_object(retval->cat_map_info);
  scm_gc_protect_object(retval->memo_display_info);
  scm_gc_protect_object(retval->memo_map_info);
  scm_gc_protect_object(retval->acct_display_info);
  scm_gc_protect_object(retval->acct_map_info);
  scm_gc_protect_object(retval->stock_hash);
  scm_gc_protect_object(retval->new_stocks);
  scm_gc_protect_object(retval->ticker_map);
  scm_gc_protect_object(retval->imported_account_group);
  scm_gc_protect_object(retval->match_transactions);
  
  /* set a default currency for new accounts */
  gnc_ui_update_commodity_picker(retval->currency_picker,
                                 GNC_COMMODITY_NS_ISO, 
                                 gnc_commodity_get_printname
                                 (gnc_default_currency()));
  
  if(!retval->show_doc_pages) {
    gnome_druid_set_page(GNOME_DRUID(retval->druid),
                         get_named_page(retval, "load_file_page"));
  }

  gnc_druid_set_colors (GNOME_DRUID (retval->druid));

  gnc_register_gui_component(DRUID_QIF_IMPORT_CM_CLASS, NULL, NULL, retval);

  gnome_window_icon_set_from_default(GTK_WINDOW(retval->window));
  gtk_widget_show_all(retval->window);
  gtk_window_present (GTK_WINDOW(retval->window));

  return retval;
}