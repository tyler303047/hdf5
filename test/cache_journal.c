/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Programmer:  John Mainzer
 *              3/08
 *
 *		This file contains tests for the metadata journaling
 *		features implemented in H5C.c and friends.
 */

#include <aio.h>

#define H5F_PACKAGE             /*suppress error about including H5Fpkg   */

#include "h5test.h"
#include "H5Eprivate.h"
#include "H5Iprivate.h"
#include "H5MMprivate.h"        /* Memory management                    */
#include "H5MFprivate.h"
#include "H5ACprivate.h"
#include "H5Cprivate.h"
#include "cache_common.h"
#include "H5Fpkg.h"

#define HDF5_FILE_NAME "HDF5.file"

/* global variable declarations: */

const char *FILENAMES[] = {
        "cache_test",
        "cache_journal_test",
	"cache_sb_test",
	"journal_file",
        NULL
};

/* private function declarations: */

/* utility functions */

static void begin_trans(H5C_t * cache_ptr,
                        hbool_t verbose,
                        uint64_t expected_trans_num,
	                const char * trans_name);

static void copy_file(const char * input_file,
                      const char * output_file);

static void end_trans(H5F_t * file_ptr,
                      H5C_t * cache_ptr,
                      hbool_t verbose,
                      uint64_t trans_num,
                      const char * trans_name);

static hbool_t file_exists(const char * file_path_ptr);

static void flush_journal(H5C_t * cache_ptr);

static void jrnl_col_major_scan_backward(H5F_t * file_ptr,
		                          int32_t max_index,
                                          int32_t lag, 
					  hbool_t verbose,
                                          hbool_t reset_stats,
                                          hbool_t display_stats,
                                          hbool_t display_detailed_stats,
                                          hbool_t do_inserts,
                                          hbool_t dirty_inserts,
                                          int dirty_unprotects,
			                  uint64_t trans_num);

static void jrnl_col_major_scan_forward(H5F_t * file_ptr,
		                         int32_t max_index,
                                         int32_t lag,
                                         hbool_t verbose,
                                         hbool_t reset_stats,
                                         hbool_t display_stats,
                                         hbool_t display_detailed_stats,
                                         hbool_t do_inserts,
                                         hbool_t dirty_inserts,
                                         int dirty_unprotects,
			                 uint64_t trans_num);

static void jrnl_row_major_scan_backward(H5F_t * file_ptr,
                                          int32_t max_index,
                                          int32_t lag,
                                          hbool_t verbose,
                                          hbool_t reset_stats,
                                          hbool_t display_stats,
                                          hbool_t display_detailed_stats,
                                          hbool_t do_inserts,
                                          hbool_t dirty_inserts,
                                          hbool_t do_moves,
                                          hbool_t move_to_main_addr,
                                          hbool_t do_destroys,
			                  hbool_t do_mult_ro_protects,
                                          int dirty_destroys,
                                          int dirty_unprotects,
			                  uint64_t trans_num);

static void jrnl_row_major_scan_forward(H5F_t * file_ptr,
                                         int32_t max_index,
                                         int32_t lag,
                                         hbool_t verbose,
                                         hbool_t reset_stats,
                                         hbool_t display_stats,
                                         hbool_t display_detailed_stats,
                                         hbool_t do_inserts,
                                         hbool_t dirty_inserts,
                                         hbool_t do_moves,
                                         hbool_t move_to_main_addr,
                                         hbool_t do_destroys,
		                         hbool_t do_mult_ro_protects,
                                         int dirty_destroys,
                                         int dirty_unprotects,
			                 uint64_t trans_num);

static void open_existing_file_for_journaling(const char * hdf_file_name,
                                              const char * journal_file_name,
                                              hid_t * file_id_ptr,
                                              H5F_t ** file_ptr_ptr,
                                              H5C_t ** cache_ptr_ptr,
                                              hbool_t human_readable,
                                              hbool_t use_aio); 

static void open_existing_file_without_journaling(const char * hdf_file_name,
                                                  hid_t * file_id_ptr,
                                                  H5F_t ** file_ptr_ptr,
                                                  H5C_t ** cache_ptr_ptr);

static void setup_cache_for_journaling(const char * hdf_file_name,
                                       const char * journal_file_name,
                                       hid_t * file_id_ptr,
                                       H5F_t ** file_ptr_ptr,
                                       H5C_t ** cache_ptr_ptr,
                                       hbool_t human_readable,
                                       hbool_t use_aio,
				       hbool_t use_core_driver_if_avail);

static void takedown_cache_after_journaling(hid_t file_id,
                                            const char * filename,
                                            const char * journal_filename,
			                    hbool_t use_core_driver_if_avail);

static void verify_journal_contents(const char * journal_file_path_ptr,
                                    const char * expected_file_path_ptr,
                                    hbool_t human_readable);

static void verify_journal_deleted(const char * journal_file_path_ptr);

static void verify_journal_empty(const char * journal_file_path_ptr);

/* test functions */

static void check_buffer_writes(hbool_t use_aio);

static void write_flush_verify(H5C_jbrb_t * struct_ptr, 
                               int size, 
                               char * data, 
                               FILE * readback);

static void write_noflush_verify(H5C_jbrb_t * struct_ptr, 
                                 int size, 
                                 char * data, 
                                 FILE * readback, 
                                 int repeats);

static void check_superblock_extensions(void);

static void check_mdjsc_callbacks(void);

static herr_t test_mdjsc_callback(const H5C_mdj_config_t * config_ptr,
                                hid_t dxpl_id,
                                void * data_ptr);

static void deregister_mdjsc_callback(H5F_t * file_ptr,
                                      H5C_t * cache_ptr,
                                      int32_t idx);

static void register_mdjsc_callback(H5F_t * file_ptr,
                                    H5C_t * cache_ptr,
                                    H5C_mdj_status_change_func_t fcn_ptr,
                                    void * data_ptr,
                                    int32_t * idx_ptr);

static void verify_mdjsc_table_config(H5C_t * cache_ptr,
                                      int32_t table_len,
                                      int32_t num_entries,
                                      int32_t max_idx_in_use,
                                      hbool_t * free_entries);

static void verify_mdjsc_callback_deregistered(H5C_t * cache_ptr,
                                               int32_t idx);

static void verify_mdjsc_callback_registered(H5C_t * cache_ptr,
                                         H5C_mdj_status_change_func_t fcn_ptr,
                                         void * data_ptr,
                                         int32_t idx);

static void verify_mdjsc_callback_error_rejection(void);

static void verify_mdjsc_callback_execution(void);

static void verify_mdjsc_callback_registration_deregistration(void);

static void check_binary_message_format(void);

static void verify_journal_msg(int fd,
                               uint8_t expected_msg[],
                               int expected_msg_len,
                               hbool_t last_msg,
                               const char * mismatch_failure_msg,
                               const char * read_failure_msg,
                               const char * eof_failure_msg,
                               const char * not_last_msg_msg);

static void check_message_format(void);

static void check_legal_calls(void);

static void check_transaction_tracking(hbool_t use_aio);

static void mdj_api_example_test(hbool_t human_readable,
                                 hbool_t use_aio,
                                 int num_bufs,
                                 size_t buf_size);

static void mdj_smoke_check_00(hbool_t human_readable,
                               hbool_t use_aio);

static void mdj_smoke_check_01(hbool_t human_readable,
                               hbool_t use_aio);

static void mdj_smoke_check_02(hbool_t human_readable,
                               hbool_t use_aio);

static void write_verify_trans_num(H5C_jbrb_t * struct_ptr, 
                                   uint64_t trans_num, 
                                   uint64_t min_verify_val,
                                   uint64_t verify_val);


/**************************************************************************/
/**************************************************************************/
/********************************* tests: *********************************/
/**************************************************************************/
/**************************************************************************/

/*** metadata journaling test utility functions ***/

/*-------------------------------------------------------------------------
 * Function:    begin_trans()
 *
 * Purpose:     If pass is true on entry, attempt to begin a transaction.
 * 		If the operation fails, or if it returns an unexpected
 * 		transaction number, set passw2 to FALSE, and set failure_mssg 
 *              to point to an appropriate failure message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/15/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
begin_trans(H5C_t * cache_ptr,
            hbool_t verbose,
            uint64_t expected_trans_num,
	    const char * trans_name)
{
    const char * fcn_name = "begin_trans()";
    herr_t result;
    uint64_t trans_num = 0; 

    if ( pass ) {

        result = H5C_begin_transaction(cache_ptr, &trans_num, trans_name);

        if ( result < 0 ) {

            if ( verbose ) {

                HDfprintf(stdout, "%s: H5C_begin_transaction(%s) failed.\n",
			  fcn_name, trans_name);
            }
            pass = FALSE;
	    failure_mssg = "H5C_begin_transaction() failed.\n";

        } else if ( trans_num != expected_trans_num ) {

            if ( verbose ) {

                HDfprintf(stdout, "%s: actual/expected trans num = %lld/%lld.\n",
			  fcn_name, (long long)trans_num,
                          (long long)expected_trans_num);
            }
            pass = FALSE;
	    failure_mssg = "begin_trans() issued unexpected trans_num.\n";
        }
    }

    return;

} /* begin_trans() */


/*-------------------------------------------------------------------------
 * Function:    copy_file()
 *
 * Purpose:     If pass is true, copy the input file to the output file.
 *              Set pass to FALSE and set failure_mssg to point to an 
 *              appropriate error message on failure.
 *
 *              Do nothing if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/15/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
copy_file(const char * input_file,
          const char * output_file)
{
    const char * fcn_name = "copy_file()";
    char buffer[(8 * 1024) + 1];
    hbool_t verbose = FALSE;
    size_t cur_buf_len;
    const size_t max_buf_len = (8 * 1024);
    size_t input_len;
    size_t input_remainder = 0;
    ssize_t result;
    int input_file_fd = -1;
    int output_file_fd = -1;
    h5_stat_t buf;

    if ( pass ) {

	if ( input_file == NULL ) {

            failure_mssg = "input_file NULL on entry?!?",
            pass = FALSE;

	} else if ( output_file == NULL ) {

            failure_mssg = "output_file NULL on entry?!?",
            pass = FALSE;

        }
    }

    /* get the length of the input file */
    if ( pass ) {

	if ( HDstat(input_file, &buf) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat() failed with errno = %d.\n",
                          fcn_name, errno);
	    }
	    failure_mssg = "stat() failed on journal file.";
	    pass = FALSE;

	} else {

	    if ( (buf.st_size) == 0 ) {

                failure_mssg = "input file empty?!?";
	        pass = FALSE;

	    } else {
                
	        input_len = (size_t)(buf.st_size);
		input_remainder = input_len;

		if ( verbose ) {

		    HDfprintf(stdout, "%s: input_len = %d.\n", 
		              fcn_name, (int)input_len);
		}
            }
	} 
    }

    /* open the input file */
    if ( pass ) {

	if ( (input_file_fd = HDopen(input_file, O_RDONLY, 0777)) == -1 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDopen(i) failed with errno = %d.\n",
                          fcn_name, errno);
	    }
            failure_mssg = "Can't open input file.";
	    pass = FALSE;
        }
    }

    /* open the output file */
    if ( pass ) {

	if ( (output_file_fd = HDopen(output_file, O_WRONLY|O_CREAT|O_TRUNC, 0777))
             == -1 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDopen(i) failed with errno = %d.\n",
                          fcn_name, errno);
	    }
            failure_mssg = "Can't open output file.";
	    pass = FALSE;
        }
    }

    while ( ( pass ) &&
	    ( input_remainder > 0 ) ) 
    {
        if ( input_remainder > max_buf_len ) {

            cur_buf_len = max_buf_len;
            input_remainder -= max_buf_len;

        } else {

            cur_buf_len = input_remainder;
            input_remainder = 0;
        }

        result = HDread(input_file_fd, buffer, cur_buf_len);

        if ( result != (int)cur_buf_len ) {

            if ( verbose ) {

                HDfprintf(stdout, 
                          "%s: HDread() failed. result = %d, errno = %d.\n",
                          fcn_name, (int)result, errno);
            }
            failure_mssg = "error reading input file.";
            pass = FALSE;
        }

        buffer[cur_buf_len] = '\0';

        if ( pass ) {

            result = HDwrite(output_file_fd, buffer, cur_buf_len);

            if ( result != (int)cur_buf_len ) {

	        if ( verbose ) {

                    HDfprintf(stdout, 
                              "%s: HDwrite() failed. result = %d, errno = %d.\n",
                              fcn_name, (int)result, errno);
                }
                failure_mssg = "error writing output file.";
                pass = FALSE;
            }
        }
    }

    if ( input_file_fd != -1 ) {

        if ( HDclose(input_file_fd) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDclose(i) failed with errno = %d.\n",
                          fcn_name, errno);
	    }

	    if ( pass ) {

                failure_mssg = "Can't close input file.";
	        pass = FALSE;
	    }
	}
    }

    if ( output_file_fd != -1 ) {

        if ( HDclose(output_file_fd) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDclose(o) failed with errno = %d.\n",
                          fcn_name, errno);
	    }

	    if ( pass ) {

                failure_mssg = "Can't close output file.";
	        pass = FALSE;
	    }
	}
    }

    return;

} /* copy_file() */


/*-------------------------------------------------------------------------
 * Function:    end_trans()
 *
 * Purpose:     If pass is true on entry, attempt to end the current 
 * 		transaction.  If the operation fails, set pass to FALSE, 
 * 		and set failure_mssg to point to an appropriate failure 
 * 		message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/15/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
end_trans(H5F_t * file_ptr,
          H5C_t * cache_ptr,
          hbool_t verbose,
          uint64_t trans_num,
          const char * trans_name)
{
    const char * fcn_name = "end_trans()";
    herr_t result;

    if ( pass ) {

        result = H5C_end_transaction(file_ptr, H5AC_dxpl_id, cache_ptr, 
			              trans_num, trans_name);

        if ( result < 0 ) {

            if ( verbose ) {
                HDfprintf(stdout, 
			  "%s: H5C_end_transaction(%lld, \"%s\") failed.\n",
			  fcn_name, (long long)trans_num, trans_name);
            }
            pass = FALSE;
	    failure_mssg = "H5C_end_transaction() failed.\n";
        }
    }

    return;

} /* end_trans() */


/*-------------------------------------------------------------------------
 * Function:    file_exists()
 *
 * Purpose:     If pass is true on entry, stat the target file, and 
 * 		return TRUE if it exists, and FALSE if it does not.
 *
 * 		If any errors are detected in this process, set pass 
 * 		to FALSE and set failure_mssg to point to an appropriate 
 * 		error message.
 *
 *              Do nothing and return FALSE if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5//08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static hbool_t
file_exists(const char * file_path_ptr)
{
    const char * fcn_name = "file_exists()";
    hbool_t ret_val = FALSE; /* will set to TRUE if necessary */
    hbool_t verbose = FALSE;
    h5_stat_t buf;

    if ( pass ) {

	if ( file_path_ptr == NULL ) {

            failure_mssg = "file_path_ptr NULL on entry?!?",
            pass = FALSE;
	}
    }

    if ( pass ) {

	if ( HDstat(file_path_ptr, &buf) == 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat(%s) succeeded.\n", fcn_name,
			  file_path_ptr);
	    }

	    ret_val = TRUE;

        } else if ( errno == ENOENT ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat(%s) failed with ENOENT\n", 
			  fcn_name, file_path_ptr);
	    }
	    
	} else {

	    if ( verbose ) {

	        HDfprintf(stdout, 
			  "%s: HDstat() failed with unexpected errno = %d.\n",
                          fcn_name, errno);
	    }

	    failure_mssg = "HDstat() returned unexpected value.";
	    pass = FALSE;

	} 
    }

    return(ret_val);

} /* file_exists() */


/*-------------------------------------------------------------------------
 * Function:    flush_journal()
 *
 * Purpose:     If pass is true on entry, attempt to flush the journal.
 * 		If the operation fails, set pass to FALSE,  and set 
 * 		failure_mssg to point to an appropriate failure message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/15/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
flush_journal(H5C_t * cache_ptr)
{
    if ( pass ) {

        if ( H5C_jb__flush(&(cache_ptr->mdj_jbrb)) < 0 ) {
		
	    pass = FALSE;
	    failure_mssg = "H5C_jb__flush() reports failure.";
	}
    }

    return;

} /* flush_journal() */


/*-------------------------------------------------------------------------
 * Function:	jrnl_col_major_scan_backward()
 *
 * Purpose:	Do a sequence of inserts, protects, and unprotects
 *		broken into a sequence of transactions while scanning 
 *		backwards through the set of entries.  
 *
 *		If pass is false on entry, do nothing.
 *
 *		Note tht this function is an adaption of 
 *		col_major_scan_backward()
 *
 * Return:	void
 *
 * Programmer:	John Mainzer
 *              5/20/08
 *
 * Modifications:
 *
 * 		None.
 *
 *-------------------------------------------------------------------------
 */

void
jrnl_col_major_scan_backward(H5F_t * file_ptr,
		              int32_t max_index,
                              int32_t lag,
                              hbool_t verbose,
                              hbool_t reset_stats,
                              hbool_t display_stats,
                              hbool_t display_detailed_stats,
                              hbool_t do_inserts,
                              hbool_t dirty_inserts,
                              int dirty_unprotects,
			      uint64_t trans_num)
{
    const char * fcn_name = "jrnl_col_major_scan_backward()";
    H5C_t * cache_ptr;
    int i;
    int mile_stone = 1;
    int32_t type;
    int32_t idx;
    int32_t local_max_index[NUMBER_OF_ENTRY_TYPES];

    if ( verbose )
        HDfprintf(stdout, "%s: entering.\n", fcn_name);

    if ( pass ) {

	cache_ptr = file_ptr->shared->cache;

	HDassert( cache_ptr != NULL );

        for ( i = 0; i < NUMBER_OF_ENTRY_TYPES; i++ )
        {
            local_max_index[i] = MIN(max_index, max_indices[i]);
        }

        HDassert( lag > 5 );

        if ( reset_stats ) {

            H5C_stats__reset(cache_ptr);
        }

        idx = local_max_index[NUMBER_OF_ENTRY_TYPES - 1] + lag;
    }

    if ( verbose ) /* 1 */
        HDfprintf(stdout, "%s: point %d.\n", fcn_name, mile_stone++);


    while ( ( pass ) && ( (idx + lag) >= 0 ) )
    {
        type = NUMBER_OF_ENTRY_TYPES - 1;

	trans_num++;

        begin_trans(cache_ptr, verbose, trans_num, 
	            "jrnl_col_major_scan_backward outer loop");
	    
        if ( verbose ) {

            HDfprintf(stdout, "begin trans %lld, idx = %d.\n", trans_num, idx);
        }

        while ( ( pass ) && ( type >= 0 ) )
        {
	    if ( verbose ) {

                HDfprintf(stdout, "%d:%d: ", type, idx);
	    }
	    
            if ( ( pass ) && ( do_inserts) && ( (idx - lag) >= 0 ) &&
                 ( (idx - lag) <= local_max_index[type] ) &&
                 ( ((idx - lag) % 3) == 0 ) &&
                 ( ! entry_in_cache(cache_ptr, type, (idx - lag)) ) ) {

                if ( verbose )
                    HDfprintf(stdout, "(i, %d, %d) ", type, (idx - lag));

                insert_entry(file_ptr, type, (idx - lag), dirty_inserts,
                              H5C__NO_FLAGS_SET);
            }

            if ( ( pass ) &&
		 ( idx >= 0 ) && 
		 ( idx <= local_max_index[type] ) ) {

                if ( verbose )
                    HDfprintf(stdout, "(p, %d, %d) ", type, idx);

                protect_entry(file_ptr, type, idx);
            }

            if ( ( pass ) && ( (idx + lag) >= 0 ) &&
                 ( (idx + lag) <= local_max_index[type] ) ) {

                if ( verbose )
                    HDfprintf(stdout, "(u, %d, %d) ", type, (idx + lag));

                unprotect_entry(file_ptr, type, idx + lag,
                        (dirty_unprotects ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET));
            }

            if ( verbose )
                HDfprintf(stdout, "\n");

            type--;
        }

        end_trans(file_ptr, cache_ptr, verbose, trans_num, 
	          "jrnl_col_major_scan_backward outer loop");

	if ( verbose ) {

            HDfprintf(stdout, "end trans %lld, idx = %d.\n", trans_num, idx);
        }

	if ( ( verbose ) && ( ! pass ) ) {

	    HDfprintf(stdout, "pass == FALSE, failure mssg = \"%s\".\n",
		      failure_mssg);
	}

        idx--;
    }

    if ( verbose ) /* 2 */
        HDfprintf(stdout, "%s: point %d.\n", fcn_name, mile_stone++);

    if ( ( pass ) && ( display_stats ) ) {

        H5C_stats(cache_ptr, "test cache", display_detailed_stats);
    }

    if ( verbose )
        HDfprintf(stdout, "%s: exiting.\n", fcn_name);

    return;

} /* jrnl_col_major_scan_backward() */


/*-------------------------------------------------------------------------
 * Function:	jrnl_col_major_scan_forward()
 *
 * Purpose:	Do a sequence of inserts, protects, and unprotects
 *		broken into a sequence of transactions while scanning 
 *		through the set of entries.  
 *
 *		Note that this function is an adaption of 
 *		col_major_scan_forward().
 *
 *		If pass is false on entry, do nothing.
 *
 * Return:	void
 *
 * Programmer:	John Mainzer
 *              5/20/08
 *
 * Modifications:
 *
 * 		None.
 *
 *-------------------------------------------------------------------------
 */

void
jrnl_col_major_scan_forward(H5F_t * file_ptr,
		             int32_t max_index,
                             int32_t lag,
                             hbool_t verbose,
                             hbool_t reset_stats,
                             hbool_t display_stats,
                             hbool_t display_detailed_stats,
                             hbool_t do_inserts,
                             hbool_t dirty_inserts,
                             int dirty_unprotects,
			     uint64_t trans_num)
{
    const char * fcn_name = "jrnl_col_major_scan_forward()";
    H5C_t * cache_ptr;
    int i;
    int32_t type;
    int32_t idx;
    int32_t local_max_index[NUMBER_OF_ENTRY_TYPES];

    if ( verbose )
        HDfprintf(stdout, "%s: entering.\n", fcn_name);

    if ( pass ) {

        cache_ptr = file_ptr->shared->cache;

	HDassert( cache_ptr != NULL );

        for ( i = 0; i < NUMBER_OF_ENTRY_TYPES; i++ )
        {
            local_max_index[i] = MIN(max_index, max_indices[i]);
        }

        HDassert( lag > 5 );

        type = 0;

        if ( reset_stats ) {

            H5C_stats__reset(cache_ptr);
	}

        idx = -lag;
    }

    while ( ( pass ) && ( (idx - lag) <= MAX_ENTRIES ) )
    {
        type = 0;

	trans_num++;

        begin_trans(cache_ptr, verbose, trans_num, 
	            "jrnl_col_major_scan_forward outer loop");
	    
        if ( verbose ) {

            HDfprintf(stdout, "begin trans %lld, idx = %d.\n", trans_num, idx);
        }

        while ( ( pass ) && ( type < NUMBER_OF_ENTRY_TYPES ) )
        {
	    if ( verbose ) {

                HDfprintf(stdout, "%d:%d: ", type, idx);
	    }
	    
            if ( ( pass ) && ( do_inserts ) && ( (idx + lag) >= 0 ) &&
                 ( (idx + lag) <= local_max_index[type] ) &&
                 ( ((idx + lag) % 3) == 0 ) &&
                 ( ! entry_in_cache(cache_ptr, type, (idx + lag)) ) ) {

                if ( verbose )
                    HDfprintf(stdout, "(i, %d, %d) ", type, (idx + lag));

                insert_entry(file_ptr, type, (idx + lag), dirty_inserts,
                              H5C__NO_FLAGS_SET);
            }

            if ( ( pass ) && 
                 ( idx >= 0 ) && 
                 ( idx <= local_max_index[type] ) ) {

                if ( verbose )
                    HDfprintf(stdout, "(p, %d, %d) ", type, idx);

                protect_entry(file_ptr, type, idx);
            }

            if ( ( pass ) && ( (idx - lag) >= 0 ) &&
                 ( (idx - lag) <= local_max_index[type] ) ) {

                if ( verbose )
                    HDfprintf(stdout, "(u, %d, %d) ", type, (idx - lag));

                unprotect_entry(file_ptr, type, idx - lag,
                        (dirty_unprotects ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET));
            }

            if ( verbose )
                HDfprintf(stdout, "\n");

            type++;
        }

        end_trans(file_ptr, cache_ptr, verbose, trans_num, 
	          "jrnl_col_major_scan_forward outer loop");

	if ( verbose ) {

            HDfprintf(stdout, "end trans %lld, idx = %d.\n", trans_num, idx);
        }

	if ( ( verbose ) && ( ! pass ) ) {

	    HDfprintf(stdout, "pass == FALSE, failure mssg = \"%s\".\n",
		      failure_mssg);
	}

        idx++;
    }

    if ( ( pass ) && ( display_stats ) ) {

        H5C_stats(cache_ptr, "test cache", display_detailed_stats);
    }

    return;

} /* jrnl_col_major_scan_forward() */


/*-------------------------------------------------------------------------
 * Function:	jrnl_row_major_scan_backward()
 *
 * Purpose:	Do a sequence of inserts, protects, unprotects, moves,
 *		destroys broken into transactions while scanning backwards 
 *		through the set of entries.  
 *
 *		If pass is false on entry, do nothing.
 *
 *		Note that this function is an adaption of 
 *		row_major_scan_backward()
 *
 * Return:	void
 *
 * Programmer:	John Mainzer
 *              5/20/08
 *
 * Modifications:
 *
 * 		None.
 *
 *-------------------------------------------------------------------------
 */

void
jrnl_row_major_scan_backward(H5F_t * file_ptr,
                              int32_t max_index,
                              int32_t lag,
                              hbool_t verbose,
                              hbool_t reset_stats,
                              hbool_t display_stats,
                              hbool_t display_detailed_stats,
                              hbool_t do_inserts,
                              hbool_t dirty_inserts,
                              hbool_t do_moves,
                              hbool_t move_to_main_addr,
                              hbool_t do_destroys,
			      hbool_t do_mult_ro_protects,
                              int dirty_destroys,
                              int dirty_unprotects,
			      uint64_t trans_num)
{
    const char * fcn_name = "jrnl_row_major_scan_backward";
    H5C_t * cache_ptr;
    int32_t type;
    int32_t idx;
    int32_t local_max_index;
    int32_t lower_bound;
    int32_t upper_bound;

    if ( verbose )
        HDfprintf(stdout, "%s(): Entering.\n", fcn_name);

    if ( pass ) {

        cache_ptr = file_ptr->shared->cache;

	HDassert( cache_ptr != NULL );
        HDassert( lag >= 10 );

        if ( reset_stats ) {

            H5C_stats__reset(cache_ptr);
	}
    }

    type = NUMBER_OF_ENTRY_TYPES - 1;

    while ( ( pass ) && ( type >= 0 ) )
    {
        local_max_index = MIN(max_index, max_indices[type]);

        idx = local_max_index + lag;

	upper_bound = local_max_index;
	lower_bound = upper_bound - 8;

        while ( ( pass ) && ( idx >= -lag ) )
        {
	    if ( idx == ( upper_bound + lag ) ) {

	        trans_num++;

                begin_trans(cache_ptr, verbose, trans_num, 
			    "jrnl_row_major_scan_backward inner loop");

		if ( verbose )
		    HDfprintf(stdout, "begin trans %lld.\n", 
			      (long long)trans_num);

	        if ( verbose )
	            HDfprintf(stdout, "(%d, %d)\n", lower_bound, upper_bound);
	    }

	    while ( ( pass ) && ( idx >= lower_bound - lag ) ) 
            {
	        if ( verbose ) {

                    HDfprintf(stdout, "%lld:%d:%d: ", trans_num, type, idx);
	        }
	    
                if ( ( pass ) && ( do_inserts ) && 
		     ( (idx - lag) >= 0 ) &&
		     ( (idx - lag) >= lower_bound ) &&
                     ( (idx - lag) <= local_max_index ) &&
                     ( (idx - lag) <= upper_bound ) &&
                     ( ((idx - lag) % 2) == 1 ) &&
                     ( ! entry_in_cache(cache_ptr, type, (idx - lag)) ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(i, %d, %d) ", type, (idx - lag));

                    insert_entry(file_ptr, type, (idx - lag), dirty_inserts,
                                  H5C__NO_FLAGS_SET);
                }


                if ( ( pass ) && 
		     ( (idx - lag + 1) >= 0 ) &&
		     ( (idx - lag + 1) >= lower_bound ) &&
                     ( (idx - lag + 1) <= local_max_index ) &&
                     ( (idx - lag + 1) <= upper_bound ) &&
                     ( ( (idx - lag + 1) % 3 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(p, %d, %d) ", 
				  type, (idx - lag + 1));

                    protect_entry(file_ptr, type, (idx - lag + 1));
                }

                if ( ( pass ) && 
		     ( (idx - lag + 2) >= 0 ) &&
		     ( (idx - lag + 2) >= lower_bound ) &&
                     ( (idx - lag + 2) <= local_max_index ) &&
                     ( (idx - lag + 2) <= upper_bound ) &&
                     ( ( (idx - lag + 2) % 3 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(u, %d, %d) ", 
				  type, (idx - lag + 2));

                    unprotect_entry(file_ptr, type, idx-lag+2, H5C__NO_FLAGS_SET);
                }


                if ( ( pass ) && ( do_moves ) && 
		     ( (idx - lag + 2) >= 0 ) &&
		     ( (idx - lag + 2) >= lower_bound ) &&
                     ( (idx - lag + 2) <= local_max_index ) &&
                     ( (idx - lag + 2) <= upper_bound ) &&
                     ( ( (idx - lag + 2) % 3 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(r, %d, %d, %d) ", 
			          type, (idx - lag + 2), 
				  (int)move_to_main_addr);

                    move_entry(cache_ptr, type, (idx - lag + 2),
                                  move_to_main_addr);
                }


                if ( ( pass ) && 
		     ( (idx - lag + 3) >= 0 ) &&
		     ( (idx - lag + 3) >= lower_bound ) &&
                     ( (idx - lag + 3) <= local_max_index ) &&
                     ( (idx - lag + 3) <= upper_bound ) &&
                     ( ( (idx - lag + 3) % 5 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(p, %d, %d) ", 
				  type, (idx - lag + 3));

                    protect_entry(file_ptr, type, (idx - lag + 3));
                }

                if ( ( pass ) && 
		     ( (idx - lag + 5) >= 0 ) &&
		     ( (idx - lag + 5) >= lower_bound ) &&
                     ( (idx - lag + 5) <= local_max_index ) &&
                     ( (idx - lag + 5) <= upper_bound ) &&
                     ( ( (idx - lag + 5) % 5 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(u, %d, %d) ", 
				  type, (idx - lag + 5));

                    unprotect_entry(file_ptr, type, idx-lag+5, H5C__NO_FLAGS_SET);
                }

	        if ( do_mult_ro_protects )
	        {
		    if ( ( pass ) && 
		         ( (idx - lag + 5) >= 0 ) &&
		         ( (idx - lag + 5) >= lower_bound ) &&
		         ( (idx - lag + 5) < local_max_index ) &&
		         ( (idx - lag + 5) < upper_bound ) &&
		         ( (idx - lag + 5) % 9 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "(p-ro, %d, %d) ", type, 
				      (idx - lag + 5));

		        protect_entry_ro(file_ptr, type, (idx - lag + 5));
		    }

		    if ( ( pass ) && 
		         ( (idx - lag + 6) >= 0 ) &&
		         ( (idx - lag + 6) >= lower_bound ) &&
		         ( (idx - lag + 6) < local_max_index ) &&
		         ( (idx - lag + 6) < upper_bound ) &&
		         ( (idx - lag + 6) % 11 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "(p-ro, %d, %d) ", type, 
				      (idx - lag + 6));

		        protect_entry_ro(file_ptr, type, (idx - lag + 6));
		    }

		    if ( ( pass ) && 
		         ( (idx - lag + 7) >= 0 ) &&
		         ( (idx - lag + 7) >= lower_bound ) &&
		         ( (idx - lag + 7) < local_max_index ) &&
		         ( (idx - lag + 7) < upper_bound ) &&
		         ( (idx - lag + 7) % 13 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "(p-ro, %d, %d) ", type, 
				      (idx - lag + 7));

		        protect_entry_ro(file_ptr, type, (idx - lag + 7));
		    }

		    if ( ( pass ) && 
		         ( (idx - lag + 7) >= 0 ) &&
		         ( (idx - lag + 7) >= lower_bound ) &&
		         ( (idx - lag + 7) < local_max_index ) &&
		         ( (idx - lag + 7) < upper_bound ) &&
		         ( (idx - lag + 7) % 9 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "(u-ro, %d, %d) ", type, 
				      (idx - lag + 7));

		        unprotect_entry(file_ptr, type, (idx - lag + 7), H5C__NO_FLAGS_SET);
		    }

		    if ( ( pass ) && 
		         ( (idx - lag + 8) >= 0 ) &&
		         ( (idx - lag + 8) >= lower_bound ) &&
		         ( (idx - lag + 8) < local_max_index ) &&
		         ( (idx - lag + 8) < upper_bound ) &&
		         ( (idx - lag + 8) % 11 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "(u-ro, %d, %d) ", type, 
				      (idx - lag + 8));

		        unprotect_entry(file_ptr, type, (idx - lag + 8), H5C__NO_FLAGS_SET);
		    }

		    if ( ( pass ) && 
		         ( (idx - lag + 9) >= 0 ) &&
		         ( (idx - lag + 9) >= lower_bound ) &&
		         ( (idx - lag + 9) < local_max_index ) &&
		         ( (idx - lag + 9) < upper_bound ) &&
		         ( (idx - lag + 9) % 13 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "(u-ro, %d, %d) ", type, 
				      (idx - lag + 9));

		        unprotect_entry(file_ptr, type, (idx - lag + 9), H5C__NO_FLAGS_SET);
		    }
	        } /* if ( do_mult_ro_protects ) */

                if ( ( pass ) && 
		     ( idx >= 0 ) && 
		     ( idx >= lower_bound ) && 
		     ( idx <= local_max_index ) &&
		     ( idx <= upper_bound ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(p, %d, %d) ", type, idx);

                    protect_entry(file_ptr, type, idx);
                }


                if ( ( pass ) && 
		     ( (idx + lag - 2) >= 0 ) &&
		     ( (idx + lag - 2) >= lower_bound ) &&
                     ( (idx + lag - 2) <= local_max_index ) &&
                     ( (idx + lag - 2) <= upper_bound ) &&
                     ( ( (idx + lag - 2) % 7 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(u, %d, %d) ", 
				  type, (idx + lag - 2));

                    unprotect_entry(file_ptr, type, idx+lag-2, H5C__NO_FLAGS_SET);
                }

                if ( ( pass ) && 
		     ( (idx + lag - 1) >= 0 ) &&
		     ( (idx + lag - 1) >= lower_bound ) &&
                     ( (idx + lag - 1) <= local_max_index ) &&
                     ( (idx + lag - 1) <= upper_bound ) &&
                     ( ( (idx + lag - 1) % 7 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "(p, %d, %d) ", 
				  type, (idx + lag - 1));

                    protect_entry(file_ptr, type, (idx + lag - 1));
                }


                if ( do_destroys ) {

                    if ( ( pass ) && 
	                 ( (idx + lag) >= 0 ) &&
	                 ( (idx + lag) >= lower_bound ) &&
                         ( ( idx + lag) <= local_max_index ) &&
                         ( ( idx + lag) <= upper_bound ) ) {

                        switch ( (idx + lag) %4 ) {

                            case 0:
                                if ( (entries[type])[idx+lag].is_dirty ) {

                                    unprotect_entry(file_ptr, type, idx + lag,
						     H5C__NO_FLAGS_SET);
                                } else {

                                    unprotect_entry(file_ptr, type, idx + lag,
                                            (dirty_unprotects ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET));
                                }
                                break;

                            case 1: /* we just did an insert */
                                unprotect_entry(file_ptr, type, idx + lag,
						 H5C__NO_FLAGS_SET);
                                break;

                            case 2:
                                if ( (entries[type])[idx + lag].is_dirty ) {

                                    unprotect_entry(file_ptr, type, idx + lag,
						     H5C__DELETED_FLAG);
                                } else {

                                    unprotect_entry(file_ptr, type, idx + lag,
                                            (dirty_destroys ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET)
                                            | H5C__DELETED_FLAG);
                                }
                                break;

                            case 3: /* we just did an insrt */
                                unprotect_entry(file_ptr, type, idx + lag,
						 H5C__DELETED_FLAG);
                                break;

                            default:
                                HDassert(0); /* this can't happen... */
                                break;
                        }
                    }
                } else {

                    if ( ( pass ) && 
		         ( (idx + lag) >= 0 ) &&
		         ( (idx + lag) >= lower_bound ) &&
                         ( ( idx + lag) <= local_max_index ) &&
                         ( ( idx + lag) <= upper_bound ) ) {

                        if ( verbose )
                            HDfprintf(stdout, 
				      "(u, %d, %d) ", type, (idx + lag));

                        unprotect_entry(file_ptr, type, idx + lag,
                                (dirty_unprotects ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET));
                    }
                }

		idx--;

		if ( verbose )
		    HDfprintf(stdout, "\n");

            } /* while ( ( pass ) && ( idx >= lower_bound - lag ) ) */

	    end_trans(file_ptr, cache_ptr, verbose, trans_num, 
                      "jrnl_row_major_scan_backward inner loop");

	    if ( verbose )
	        HDfprintf(stdout, "end trans %lld.\n", (long long)trans_num);

	    upper_bound = lower_bound - (2 * lag) - 2;
            lower_bound = upper_bound - 8;

	    idx = upper_bound + lag;

        } /* while ( ( pass ) && ( idx >= -lag ) ) */

        type--;

    } /* while ( ( pass ) && ( type >= 0 ) ) */

    if ( ( pass ) && ( display_stats ) ) {

        H5C_stats(cache_ptr, "test cache", display_detailed_stats);
    }

    return;

} /* jrnl_row_major_scan_backward() */


/*-------------------------------------------------------------------------
 * Function:	jrnl_row_major_scan_forward()
 *
 * Purpose:	Do a sequence of inserts, protects, unprotects, moves,
 *		and destroys broken into transactions while scanning 
 *		through the set of entries. 
 *
 *		If pass is false on entry, do nothing.
 *
 *		Note that this function is an adaption of 
 *		row_major_scan_forward().
 *
 * Return:	void
 *
 * Programmer:	John Mainzer
 *              5/20/08
 *
 * Modifications:
 *
 * 		None.
 *
 *-------------------------------------------------------------------------
 */

void
jrnl_row_major_scan_forward(H5F_t * file_ptr,
                             int32_t max_index,
                             int32_t lag,
                             hbool_t verbose,
                             hbool_t reset_stats,
                             hbool_t display_stats,
                             hbool_t display_detailed_stats,
                             hbool_t do_inserts,
                             hbool_t dirty_inserts,
                             hbool_t do_moves,
                             hbool_t move_to_main_addr,
                             hbool_t do_destroys,
		             hbool_t do_mult_ro_protects,
                             int dirty_destroys,
                             int dirty_unprotects,
			     uint64_t trans_num)
{
    const char * fcn_name = "jrnl_row_major_scan_forward";
    H5C_t * cache_ptr;
    int32_t type;
    int32_t idx;
    int32_t local_max_index;
    int32_t lower_bound;
    int32_t upper_bound;

    if ( verbose )
        HDfprintf(stdout, "%s(): entering.\n", fcn_name);

    if ( pass ) {

        cache_ptr = file_ptr->shared->cache;

	HDassert( cache_ptr != NULL );
	HDassert( lag >= 10 );

	type = 0;

        if ( reset_stats ) {

            H5C_stats__reset(cache_ptr);
	}
    }

    while ( ( pass ) && ( type < NUMBER_OF_ENTRY_TYPES ) )
    {
        idx = -lag;

        local_max_index = MIN(max_index, max_indices[type]);

	lower_bound = 0;
	upper_bound = lower_bound + 8;

        while ( ( pass ) && ( idx <= (local_max_index + lag) ) )
        {
	    if ( idx == ( lower_bound - lag ) ) {

	        trans_num++;

                begin_trans(cache_ptr, verbose, trans_num, 
			    "jrnl_row_major_scan_forward inner loop");

		if ( verbose )
		    HDfprintf(stdout, "begin trans %lld.\n", 
			      (long long)trans_num);

	        if ( verbose )
	            HDfprintf(stdout, "(%d, %d)\n", lower_bound, upper_bound);
	    }

	    while ( ( pass ) && ( idx <= upper_bound + lag ) ) 
            {
	    
	        if ( verbose ) {

                    HDfprintf(stdout, "%lld:%d:%d: ", trans_num, type, idx);
	        }

                if ( ( pass ) && ( do_inserts ) && 
		     ( (idx + lag) >= 0 ) &&
		     ( (idx + lag) >= lower_bound ) &&
                     ( (idx + lag) <= local_max_index ) &&
                     ( (idx + lag) <= upper_bound ) &&
                     ( ((idx + lag) % 2) == 0 ) &&
                     ( ! entry_in_cache(cache_ptr, type, (idx + lag)) ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "1(i, %d, %d) ", type, (idx + lag));

		    /*** insert entry idx + lag (if not already present *** */
                    insert_entry(file_ptr, type, (idx + lag), dirty_inserts,
                                  H5C__NO_FLAGS_SET);
                }


                if ( ( pass ) && 
		     ( (idx + lag - 1) >= 0 ) &&
		     ( (idx + lag - 1) >= lower_bound ) &&
                     ( (idx + lag - 1) <= local_max_index ) &&
                     ( (idx + lag - 1) <= upper_bound ) &&
                     ( ( (idx + lag - 1) % 3 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, 
				  "2(p, %d, %d) ", type, (idx + lag - 1));

		    /*** protect entry idx + lag - 1 ***/
                    protect_entry(file_ptr, type, (idx + lag - 1));
                }

                if ( ( pass ) && 
		     ( (idx + lag - 2) >= 0 ) &&
		     ( (idx + lag - 2) >= lower_bound ) &&
                     ( (idx + lag - 2) <= local_max_index ) &&
                     ( (idx + lag - 2) <= upper_bound ) &&
                     ( ( (idx + lag - 2) % 3 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "3(u, %d, %d) ", 
				  type, (idx + lag - 2));

		    /*** unprotect entry idx + lag - 2 ***/
                    unprotect_entry(file_ptr, type, idx+lag-2, H5C__NO_FLAGS_SET);
                }


                if ( ( pass ) && ( do_moves ) && 
		     ( (idx + lag - 2) >= 0 ) &&
		     ( (idx + lag - 2) >= lower_bound ) &&
                     ( (idx + lag - 2) <= local_max_index ) &&
                     ( (idx + lag - 2) <= upper_bound ) &&
                     ( ( (idx + lag - 2) % 3 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "4(r, %d, %d, %d) ", 
			          type, (idx + lag - 2), 
				  (int)move_to_main_addr);

		    /*** move entry idx + lag -2 ***/
                    move_entry(cache_ptr, type, (idx + lag - 2),
                                  move_to_main_addr);
                }


                if ( ( pass ) && 
		     ( (idx + lag - 3) >= 0 ) &&
		     ( (idx + lag - 3) >= lower_bound ) &&
                     ( (idx + lag - 3) <= local_max_index ) &&
                     ( (idx + lag - 3) <= upper_bound ) &&
                     ( ( (idx + lag - 3) % 5 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "5(p, %d, %d) ", 
				  type, (idx + lag - 3));

		    /*** protect entry idx + lag - 3 ***/
                    protect_entry(file_ptr, type, (idx + lag - 3));
                }

                if ( ( pass ) && 
		     ( (idx + lag - 5) >= 0 ) &&
		     ( (idx + lag - 5) >= lower_bound ) &&
                     ( (idx + lag - 5) <= local_max_index ) &&
                     ( (idx + lag - 5) <= upper_bound ) &&
                     ( ( (idx + lag - 5) % 5 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "6(u, %d, %d) ", 
				  type, (idx + lag - 5));


		    /*** unprotect entry idx + lag - 5 ***/
                    unprotect_entry(file_ptr, type, idx+lag-5, H5C__NO_FLAGS_SET);
                }

	        if ( do_mult_ro_protects )
	        {
		    if ( ( pass ) && 
		         ( (idx + lag - 5) >= 0 ) &&
		         ( (idx + lag - 5) >= lower_bound ) &&
		         ( (idx + lag - 5) < local_max_index ) &&
		         ( (idx + lag - 5) < upper_bound ) &&
		         ( (idx + lag - 5) % 9 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "7(p-ro, %d, %d) ", type, 
				      (idx + lag - 5));

		        /*** protect ro entry idx + lag - 5 ***/
		        protect_entry_ro(file_ptr, type, (idx + lag - 5));
		    }

		    if ( ( pass ) && 
		         ( (idx + lag - 6) >= 0 ) &&
		         ( (idx + lag - 6) >= lower_bound ) &&
		         ( (idx + lag - 6) < local_max_index ) &&
		         ( (idx + lag - 6) < upper_bound ) &&
		         ( (idx + lag - 6) % 11 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "8(p-ro, %d, %d) ", type, 
				      (idx + lag - 6));

		        /*** protect ro entry idx + lag - 6 ***/
		        protect_entry_ro(file_ptr, type, (idx + lag - 6));
		    }

		    if ( ( pass ) && 
		         ( (idx + lag - 7) >= 0 ) &&
		         ( (idx + lag - 7) >= lower_bound ) &&
		         ( (idx + lag - 7) < local_max_index ) &&
		         ( (idx + lag - 7) < upper_bound ) &&
		         ( (idx + lag - 7) % 13 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "9(p-ro, %d, %d) ", type, 
				      (idx + lag - 7));

		        /*** protect ro entry idx + lag - 7 ***/
		        protect_entry_ro(file_ptr, type, (idx + lag - 7));
		    }

		    if ( ( pass ) && 
		         ( (idx + lag - 7) >= 0 ) &&
		         ( (idx + lag - 7) >= lower_bound ) &&
		         ( (idx + lag - 7) < local_max_index ) &&
		         ( (idx + lag - 7) < upper_bound ) &&
		         ( (idx + lag - 7) % 9 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "10(u-ro, %d, %d) ", type, 
				      (idx + lag - 7));

		        /*** unprotect ro entry idx + lag - 7 ***/
		        unprotect_entry(file_ptr, type, (idx + lag - 7), H5C__NO_FLAGS_SET);
		    }

		    if ( ( pass ) && 
		         ( (idx + lag - 8) >= 0 ) &&
		         ( (idx + lag - 8) >= lower_bound ) &&
		         ( (idx + lag - 8) < local_max_index ) &&
		         ( (idx + lag - 8) < upper_bound ) &&
		         ( (idx + lag - 8) % 11 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "11(u-ro, %d, %d) ", type, 
				      (idx + lag - 8));

		        /*** unprotect ro entry idx + lag - 8 ***/
		        unprotect_entry(file_ptr, type, (idx + lag - 8), H5C__NO_FLAGS_SET);
		    }

		    if ( ( pass ) && 
		         ( (idx + lag - 9) >= 0 ) &&
		         ( (idx + lag - 9) >= lower_bound ) &&
		         ( (idx + lag - 9) < local_max_index ) &&
		         ( (idx + lag - 9) < upper_bound ) &&
		         ( (idx + lag - 9) % 13 == 0 ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "12(u-ro, %d, %d) ", type, 
				      (idx + lag - 9));

		        /*** unprotect ro entry idx + lag - 9 ***/
		        unprotect_entry(file_ptr, type, (idx + lag - 9), H5C__NO_FLAGS_SET);
		    }
	        } /* if ( do_mult_ro_protects ) */

                if ( ( pass ) && 
		     ( idx >= 0 ) && 
		     ( idx >= lower_bound ) && 
		     ( idx <= local_max_index ) &&
		     ( idx <= upper_bound ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "13(p, %d, %d) ", type, idx);

		    /*** protect entry idx ***/
                    protect_entry(file_ptr, type, idx);
                }

                if ( ( pass ) && 
		     ( (idx - lag + 2) >= 0 ) &&
		     ( (idx - lag + 2) >= lower_bound ) &&
                     ( (idx - lag + 2) <= local_max_index ) &&
                     ( (idx - lag + 2) <= upper_bound ) &&
                     ( ( (idx - lag + 2) % 7 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "14(u, %d, %d) ", 
				  type, (idx - lag + 2));

		    /*** unprotect entry idx - lag + 2 ***/
                    unprotect_entry(file_ptr, type, idx-lag+2, H5C__NO_FLAGS_SET);
                }

                if ( ( pass ) && 
		     ( (idx - lag + 1) >= 0 ) &&
		     ( (idx - lag + 1) >= lower_bound ) &&
                     ( (idx - lag + 1) <= local_max_index ) &&
                     ( (idx - lag + 1) <= upper_bound ) &&
                     ( ( (idx - lag + 1) % 7 ) == 0 ) ) {

                    if ( verbose )
                        HDfprintf(stdout, "15(p, %d, %d) ", 
				  type, (idx - lag + 1));

		    /*** protect entry idx - lag + 1 ***/
                    protect_entry(file_ptr, type, (idx - lag + 1));
                }


                if ( do_destroys ) {

                    if ( ( pass ) && 
		         ( (idx - lag) >= 0 ) &&
		         ( (idx - lag) >= lower_bound ) &&
                         ( ( idx - lag) <= local_max_index ) &&
                         ( ( idx - lag) <= upper_bound ) ) {

                        switch ( (idx - lag) %4 ) {

                            case 0: /* we just did an insert */

                                if ( verbose )
                                    HDfprintf(stdout, "16(u, %d, %d) ", 
					      type, (idx - lag));

			        /*** unprotect entry NC idx - lag ***/
                                unprotect_entry(file_ptr, type, idx - lag,
                                                 H5C__NO_FLAGS_SET);
                                break;

                            case 1:
                                if ( (entries[type])[idx-lag].is_dirty ) {

                                    if ( verbose )
                                        HDfprintf(stdout, "17(u, %d, %d) ", 
						  type, (idx - lag));

			            /*** unprotect entry NC idx - lag ***/
                                    unprotect_entry(file_ptr, type, idx - lag,
						     H5C__NO_FLAGS_SET);
                                } else {

                                    if ( verbose )
                                        HDfprintf(stdout, "18(u, %d, %d) ", 
						  type, (idx - lag));

			            /*** unprotect entry idx - lag ***/
                                    unprotect_entry(file_ptr, type, idx - lag,
                                            (dirty_unprotects ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET));
                                }
                                break;

                            case 2: /* we just did an insrt */

                                if ( verbose )
                                    HDfprintf(stdout, "19(u-del, %d, %d) ", 
					      type, (idx - lag));

			        /*** unprotect delete idx - lag ***/
                                unprotect_entry(file_ptr, type, idx - lag,
                                                 H5C__DELETED_FLAG);
                                break;

                            case 3:
                                if ( (entries[type])[idx-lag].is_dirty ) {

                                    if ( verbose )
                                        HDfprintf(stdout, "20(u-del, %d, %d) ", 
					          type, (idx - lag));

				    /*** unprotect delete idx - lag ***/
                                    unprotect_entry(file_ptr, type, idx - lag,
						     H5C__DELETED_FLAG);
                                } else {

                                    if ( verbose )
                                        HDfprintf(stdout, "21(u-del, %d, %d) ", 
					          type, (idx - lag));

				    /*** unprotect delete idx - lag ***/
                                    unprotect_entry(file_ptr, type, idx - lag,
                                            (dirty_destroys ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET)
                                            | H5C__DELETED_FLAG);
                                }
                                break;

                            default:
                                HDassert(0); /* this can't happen... */
                                break;
                        }
                    }

                } else {

                    if ( ( pass ) && 
		         ( (idx - lag) >= 0 ) &&
		         ( (idx - lag) >= lower_bound ) &&
                         ( ( idx - lag) <= local_max_index ) &&
                         ( ( idx - lag) <= upper_bound ) ) {

                        if ( verbose )
                            HDfprintf(stdout, "22(u, %d, %d) ", 
				      type, (idx - lag));

		        /*** unprotect idx - lag ***/
                        unprotect_entry(file_ptr, type, idx - lag,
                                (dirty_unprotects ? H5C__DIRTIED_FLAG : H5C__NO_FLAGS_SET));
                    }
                }

		idx++;

		if ( verbose )
		    HDfprintf(stdout, "\n");

	    } /* while ( ( pass ) && ( idx <= upper_bound ) ) */

	    end_trans(file_ptr, cache_ptr, verbose, trans_num, 
                      "jrnl_row_major_scan_forward inner loop");

	    if ( verbose )
	        HDfprintf(stdout, "end trans %lld.\n", (long long)trans_num);

            lower_bound = upper_bound + (2 * lag) + 2;
	    upper_bound = lower_bound + 8;

	    idx = lower_bound - lag;

        } /* while ( ( pass ) && ( idx <= (local_max_index + lag) ) ) */

        type++;

    } /* while ( ( pass ) && ( type < NUMBER_OF_ENTRY_TYPES ) ) */

    if ( ( pass ) && ( display_stats ) ) {

        H5C_stats(cache_ptr, "test cache", display_detailed_stats);
    }

    return;

} /* jrnl_row_major_scan_forward() */


/*-------------------------------------------------------------------------
 * Function:    open_existing_file_for_journaling()
 *
 * Purpose:     If pass is true on entry, open the specified a HDF5 file 
 * 		with journaling enabled and journal file with the specified 
 * 		name.  Return pointers to the cache data structure and file 
 * 		data structures, and verify that it contains the expected data.
 *
 *              On failure, set pass to FALSE, and set failure_mssg 
 *              to point to an appropriate failure message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/13/08
 *
 * Modifications:
 *
 *		JRM -- 6/10/09
 *		Added human readable parameter.
 *
 *-------------------------------------------------------------------------
 */

static void
open_existing_file_for_journaling(const char * hdf_file_name,
                                  const char * journal_file_name,
                                  hid_t * file_id_ptr,
                                  H5F_t ** file_ptr_ptr,
                                  H5C_t ** cache_ptr_ptr,
                                  hbool_t human_readable,
                                  hbool_t use_aio) 
{
    const char * fcn_name = "open_existing_file_for_journaling()";
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0;
    herr_t result;
    H5AC_jnl_config_t jnl_config;
    hid_t fapl_id = -1;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    if ( pass )
    {
        if ( ( hdf_file_name == NULL ) ||
             ( journal_file_name == NULL ) ||
	     ( file_id_ptr == NULL ) ||
	     ( file_ptr_ptr == NULL ) ||
	     ( cache_ptr_ptr == NULL ) ) {

            failure_mssg = 
               "Bad param(s) on entry to open_existing_file_for_journaling().\n";
	    pass = FALSE;
        }
	else if ( HDstrlen(journal_file_name) > H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            failure_mssg = "journal file name too long.\n";
	    pass = FALSE;

        } else  if ( verbose ) {

            HDfprintf(stdout, "%s: HDF file name = \"%s\".\n", 
		      fcn_name, hdf_file_name);
            HDfprintf(stdout, "%s: journal file name = \"%s\".\n", 
		      fcn_name, journal_file_name);
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* create a file access propertly list. */
    if ( pass ) {

        fapl_id = H5Pcreate(H5P_FILE_ACCESS);

        if ( fapl_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pcreate() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* call H5Pset_libver_bounds() on the fapl_id */
    if ( pass ) {

	if ( H5Pset_libver_bounds(fapl_id, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) 
		< 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_libver_bounds() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Pget_jnl_config(fapl_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pget_jnl_config() failed.\n";
        }

	/* set journaling config fields to taste */
        jnl_config.enable_journaling       = TRUE;

        HDstrcpy(jnl_config.journal_file_path, journal_file_name);

        jnl_config.journal_recovered       = FALSE;
        jnl_config.jbrb_buf_size           = (8 * 1024);
        jnl_config.jbrb_num_bufs           = 2;
        jnl_config.jbrb_use_aio            = use_aio;
        jnl_config.jbrb_human_readable     = human_readable;
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        result = H5Pset_jnl_config(fapl_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);
 
    /**************************************/
    /* open the file with the fapl above. */
    /**************************************/

    /* open the file using fapl_id */
    if ( pass ) {

        file_id = H5Fopen(hdf_file_name, H5F_ACC_RDWR, fapl_id);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (1).\n";

        } else {

            file_ptr = H5I_object_verify(file_id, H5I_FILE);

            if ( file_ptr == NULL ) {

                pass = FALSE;
                failure_mssg = "Can't get file_ptr.";

                if ( verbose ) {
                    HDfprintf(stdout, "%s: Can't get file_ptr.\n", fcn_name);
                }
            }
        }
    }

    /* At least within the context of the cache test code, there should be
     * no need to allocate space for test entries since we are re-opening
     * the file, and any needed space allocation should have been done at 
     * file creation.
     */


    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * to the cache structure
     */
    if ( pass ) {

        if ( file_ptr->shared->cache == NULL ) {
	
	    pass = FALSE;
	    failure_mssg = "can't get cache pointer(1).\n";

	} else {

	    cache_ptr = file_ptr->shared->cache;
	}
    }


    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        *file_id_ptr = file_id;
	*file_ptr_ptr = file_ptr;
	*cache_ptr_ptr = cache_ptr;
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s: cp = %d -- exiting.\n", fcn_name, cp++);

    return;

} /* open_existing_file_for_journaling() */


/*-------------------------------------------------------------------------
 * Function:    open_existing_file_without_journaling()
 *
 * Purpose:     If pass is true on entry, open the specified a HDF5 file 
 * 		with journaling disabled.  Return pointers to the cache 
 * 		data structure and file data structures, and verify that 
 * 		it contains the expected data.
 *
 *              On failure, set pass to FALSE, and set failure_mssg 
 *              to point to an appropriate failure message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              7/10/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
open_existing_file_without_journaling(const char * hdf_file_name,
                                      hid_t * file_id_ptr,
                                      H5F_t ** file_ptr_ptr,
                                      H5C_t ** cache_ptr_ptr)
{
    const char * fcn_name = "open_existing_file_without_journaling()";
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0;
    hid_t fapl_id = -1;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    if ( pass )
    {
        if ( ( hdf_file_name == NULL ) ||
	     ( file_id_ptr == NULL ) ||
	     ( file_ptr_ptr == NULL ) ||
	     ( cache_ptr_ptr == NULL ) ) {

            failure_mssg = 
           "Bad param(s) on entry to open_existing_file_without_journaling().\n";
	    pass = FALSE;

        } else {

            if ( verbose ) {

                HDfprintf(stdout, "%s: HDF file name = \"%s\".\n", 
			  fcn_name, hdf_file_name);
	    }
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* create a file access propertly list. */
    if ( pass ) {

        fapl_id = H5Pcreate(H5P_FILE_ACCESS);

        if ( fapl_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pcreate() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* call H5Pset_libver_bounds() on the fapl_id */
    if ( pass ) {

	if ( H5Pset_libver_bounds(fapl_id, H5F_LIBVER_LATEST, 
				  H5F_LIBVER_LATEST) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_libver_bounds() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);


    /**************************************/
    /* open the file with the fapl above. */
    /**************************************/

    /* open the file using fapl_id */
    if ( pass ) {

        file_id = H5Fopen(hdf_file_name, H5F_ACC_RDWR, fapl_id);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (2).\n";

        } else {

            file_ptr = H5I_object_verify(file_id, H5I_FILE);

            if ( file_ptr == NULL ) {

                pass = FALSE;
                failure_mssg = "Can't get file_ptr.";

                if ( verbose ) {
                    HDfprintf(stdout, "%s: Can't get file_ptr.\n", fcn_name);
                }
            }
        }
    }

    /* At least within the context of the cache test code, there should be
     * no need to allocate space for test entries since we are re-opening
     * the file, and any needed space allocation should have been done at 
     * file creation.
     */


    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * to the cache structure
     */
    if ( pass ) {

        if ( file_ptr->shared->cache == NULL ) {
	
	    pass = FALSE;
	    failure_mssg = "can't get cache pointer(1).\n";

	} else {

	    cache_ptr = file_ptr->shared->cache;
	}
    }


    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        *file_id_ptr = file_id;
	*file_ptr_ptr = file_ptr;
	*cache_ptr_ptr = cache_ptr;
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s: cp = %d -- exiting.\n", fcn_name, cp++);

    return;

} /* open_existing_file_without_journaling() */


/*-------------------------------------------------------------------------
 * Function:    setup_cache_for_journaling()
 *
 * Purpose:     If pass is true on entry, create a HDF5 file with 
 * 		journaling enabled and journal file with the specified name.  
 * 		Return pointers to the cache data structure and file data 
 * 		structures.  and verify that it contains the expected data.
 *
 *              On failure, set pass to FALSE, and set failure_mssg 
 *              to point to an appropriate failure message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/13/08
 *
 * Modifications:
 *
 *		Added the human_readable parameter and associated 
 *		code to allow selection of either binary or human 
 *		readable journal file.
 *						JRM -- 5/8/09
 *
 *		Added the use_aio parameter and associated code to allow
 *		selection of either AIO or SIO for journal writes.
 *
 *						JRM -- 1/22/10
 *
 *-------------------------------------------------------------------------
 */

static void
setup_cache_for_journaling(const char * hdf_file_name,
                           const char * journal_file_name,
                           hid_t * file_id_ptr,
                           H5F_t ** file_ptr_ptr,
                           H5C_t ** cache_ptr_ptr,
                           hbool_t human_readable,
                           hbool_t use_aio,
#if USE_CORE_DRIVER
			   hbool_t use_core_driver_if_avail)
#else /* USE_CORE_DRIVER */
			   hbool_t UNUSED use_core_driver_if_avail)
#endif /* USE_CORE_DRIVER */
{
    const char * fcn_name = "setup_cache_for_journaling()";
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0;
    herr_t result;
    H5AC_cache_config_t mdj_config =
    {
      /* int         version                 = */ H5C__CURR_AUTO_SIZE_CTL_VER,
      /* hbool_t     rpt_fcn_enabled         = */ FALSE,
      /* hbool_t     open_trace_file         = */ FALSE,
      /* hbool_t     close_trace_file        = */ FALSE,
      /* char        trace_file_name[]       = */ "",
      /* hbool_t     evictions_enabled       = */ TRUE,
      /* hbool_t     set_initial_size        = */ TRUE,
      /* size_t      initial_size            = */ ( 64 * 1024 ),
      /* double      min_clean_fraction      = */ 0.5,
      /* size_t      max_size                = */ (16 * 1024 * 1024 ),
      /* size_t      min_size                = */ ( 8 * 1024 ),
      /* long int    epoch_length            = */ 50000,
      /* enum H5C_cache_incr_mode incr_mode = */ H5C_incr__off,
      /* double      lower_hr_threshold      = */ 0.9,
      /* double      increment               = */ 2.0,
      /* hbool_t     apply_max_increment     = */ TRUE,
      /* size_t      max_increment           = */ (4 * 1024 * 1024),
      /* enum H5C_cache_flash_incr_mode       */
      /*                    flash_incr_mode  = */ H5C_flash_incr__off,
      /* double      flash_multiple          = */ 1.0,
      /* double      flash_threshold         = */ 0.25,
      /* enum H5C_cache_decr_mode decr_mode = */ H5C_decr__off,
      /* double      upper_hr_threshold      = */ 0.999,
      /* double      decrement               = */ 0.9,
      /* hbool_t     apply_max_decrement     = */ TRUE,
      /* size_t      max_decrement           = */ (1 * 1024 * 1024),
      /* int         epochs_before_eviction  = */ 3,
      /* hbool_t     apply_empty_reserve     = */ TRUE,
      /* double      empty_reserve           = */ 0.1,
      /* int         dirty_bytes_threshold   = */ (8 * 1024)
    };
    H5AC_jnl_config_t jnl_config =
    {
      /* int         version                 = */ H5AC__CURR_JNL_CONFIG_VER,
      /* hbool_t     enable_journaling       = */ TRUE,
      /* char        journal_file_path[]     = */ "",
      /* hbool_t     journal_recovered       = */ FALSE,
      /* size_t      jbrb_buf_size           = */ (8 * 1024),
      /* int         jbrb_num_bufs           = */ 2,
      /* hbool_t     jbrb_use_aio            = */ FALSE,
      /* hbool_t     jbrb_human_readable     = */ TRUE
    };
    hid_t fapl_id = -1;
    hid_t file_id = -1;
    haddr_t actual_base_addr;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    if ( pass )
    {
        if ( ( hdf_file_name == NULL ) ||
             ( journal_file_name == NULL ) ||
	     ( file_id_ptr == NULL ) ||
	     ( file_ptr_ptr == NULL ) ||
	     ( cache_ptr_ptr == NULL ) ) {

            failure_mssg = 
                "Bad param(s) on entry to setup_cache_for_journaling().\n";
	    pass = FALSE;
        }
	else if ( HDstrlen(journal_file_name) > H5AC__MAX_JOURNAL_FILE_NAME_LEN )
	{
            failure_mssg = "journal file name too long.\n";
	    pass = FALSE;

        } else {

	    HDstrcpy(jnl_config.journal_file_path, journal_file_name);

            if ( verbose ) {

                HDfprintf(stdout, "%s: HDF file name = \"%s\".\n", 
			  fcn_name, hdf_file_name);
                HDfprintf(stdout, "%s: journal file name = \"%s\".\n", 
			  fcn_name, journal_file_name);
	    }
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* create a file access propertly list. */
    if ( pass ) {

        fapl_id = H5Pcreate(H5P_FILE_ACCESS);

        if ( fapl_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pcreate() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* call H5Pset_libver_bounds() on the fapl_id */
    if ( pass ) {

	if ( H5Pset_libver_bounds(fapl_id, H5F_LIBVER_LATEST, 
				  H5F_LIBVER_LATEST) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_libver_bounds() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        result = H5Pset_mdc_config(fapl_id, (H5AC_cache_config_t *)&mdj_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_mdc_config() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        jnl_config.jbrb_human_readable = human_readable;
        jnl_config.jbrb_use_aio        = use_aio;

        result = H5Pset_jnl_config(fapl_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_mdc_config() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

#if USE_CORE_DRIVER
    if ( ( pass ) && ( use_core_driver_if_avail ) ) {

        if ( H5Pset_fapl_core(fapl_id, 64 * 1024 * 1024, FALSE) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5P_set_fapl_core() failed.\n";
        }
    }
#endif /* USE_CORE_DRIVER */

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

 
    /**************************************/
    /* Create a file with the fapl above. */
    /**************************************/

    /* create the file using fapl_id */
    if ( pass ) {

        file_id = H5Fcreate(hdf_file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fcreate() failed.\n";

        } else {

            file_ptr = H5I_object_verify(file_id, H5I_FILE);

            if ( file_ptr == NULL ) {

                pass = FALSE;
                failure_mssg = "Can't get file_ptr.";

                if ( verbose ) {
                    HDfprintf(stdout, "%s: Can't get file_ptr.\n", fcn_name);
                }
            }
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) { /* allocate space for test entries */

        actual_base_addr = H5MF_alloc(file_ptr, H5FD_MEM_DEFAULT, H5P_DEFAULT,
                                      (hsize_t)(ADDR_SPACE_SIZE + BASE_ADDR));

        if ( actual_base_addr == HADDR_UNDEF ) {

            pass = FALSE;
            failure_mssg = "H5MF_alloc() failed.";

            if ( verbose ) {
                HDfprintf(stdout, "%s: H5MF_alloc() failed.\n", fcn_name);
            }

        } else if ( actual_base_addr > BASE_ADDR ) {

            /* If this happens, must increase BASE_ADDR so that the
             * actual_base_addr is <= BASE_ADDR.  This should only happen
             * if the size of the superblock is increase.
             */
            pass = FALSE;
            failure_mssg = "actual_base_addr > BASE_ADDR";

            if ( verbose ) {
                HDfprintf(stdout, "%s: actual_base_addr > BASE_ADDR.\n", 
			  fcn_name);
            }
        }

        saved_actual_base_addr = actual_base_addr;
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * to the cache structure
     */
    if ( pass ) {

        if ( file_ptr->shared->cache == NULL ) {
	
	    pass = FALSE;
	    failure_mssg = "can't get cache pointer(1).\n";

	} else {

	    cache_ptr = file_ptr->shared->cache;
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    reset_entries();

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* close the fapl */
    if ( pass ) {

        if ( H5Pclose(fapl_id) < 0 ) {

	    pass = FALSE;
	    failure_mssg = "error closing fapl.\n";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        *file_id_ptr = file_id;
	*file_ptr_ptr = file_ptr;
	*cache_ptr_ptr = cache_ptr;
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s: cp = %d -- exiting.\n", fcn_name, cp++);

    return;

} /* setup_cache_for_journaling() */


/*-------------------------------------------------------------------------
 * Function:    takedown_cache_after_journaling()
 *
 * Purpose:     If file_id >= 0, close the associated file, and then delete
 * 		it.  Verify that they journal file has been deleted.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/13/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
takedown_cache_after_journaling(hid_t file_id,
                                const char * filename,
                                const char * journal_filename,
				hbool_t 
				use_core_driver_if_avail)
{
    const char *fcn_name = "takedown_cache_after_journaling";
    hbool_t verbose = FALSE;
    int error;
    
    if ( file_id >= 0 ) {

        if ( H5F_addr_defined(saved_actual_base_addr) ) {
            H5F_t * file_ptr;

            file_ptr = H5I_object_verify(file_id, H5I_FILE);

            if ( file_ptr == NULL ) {

                pass = FALSE;
                failure_mssg = "Can't get file_ptr.";

                if ( verbose ) {
                    HDfprintf(stdout, "%s: Can't get file_ptr.\n", fcn_name);
                }
            }

            /* Flush the cache, so that the close call doesn't try to write to
             * the space we free */
            H5Fflush(file_id, H5F_SCOPE_GLOBAL);

            H5MF_xfree(file_ptr, H5FD_MEM_DEFAULT, H5P_DEFAULT, saved_actual_base_addr,
                                          (hsize_t)(ADDR_SPACE_SIZE + BASE_ADDR));
            saved_actual_base_addr = HADDR_UNDEF;
        }

	if ( H5Fclose(file_id) < 0 ) {

	    if ( pass ) {

                pass = FALSE;
	        failure_mssg = "file close failed.";
	    }
	} else if ( ( ( ! USE_CORE_DRIVER ) || ( ! use_core_driver_if_avail ) ) &&
                    ( ( error = HDremove(filename) ) != 0 ) ) {

	    if ( verbose ) {
	        HDfprintf(stdout, 
		  "HDremove(\"%s\") failed, returned %d, errno = %d = %s.\n", 
		  filename, error, errno, strerror(errno));
	    }

	    if ( pass ) {

                pass = FALSE;
                failure_mssg = "HDremove() failed (1).\n";
            }
        }
    }

    verify_journal_deleted(journal_filename);

    return;

} /* takedown_cache_after_journaling() */


/*-------------------------------------------------------------------------
 * Function:    verify_journal_contents()
 *
 * Purpose:     If pass is true on entry, verify that the contents of the 
 * 		journal file matches that of the expected file.  If 
 * 		differences are detected, or if any other error is detected,
 * 		set pass to FALSE and set failure_mssg to point to an 
 * 		appropriate error message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/06/08
 *
 * Modifications:
 *
 *		JRM -- 6/10/09
 *		Updated function to deal with binary as well as human 
 *		readable journal files.
 *
 *-------------------------------------------------------------------------
 */

static void
verify_journal_contents(const char * journal_file_path_ptr,
                        const char * expected_file_path_ptr,
                        hbool_t human_readable)
{
    const char * fcn_name = "verify_journal_contents()";
    char ch;
    char journal_buf[(8 * 1024) + 1];
    char expected_buf[(8 * 1024) + 1];
    hbool_t verbose = FALSE;
    size_t cur_buf_len;
    const size_t max_buf_len = (8 * 1024);
    size_t journal_len = 0;
    size_t expected_len = 0;
    size_t first_line_len;
    size_t journal_remainder_len = 0;
    size_t expected_remainder_len = 0;
    ssize_t read_result;
    int journal_file_fd = -1;
    int expected_file_fd = -1;
    h5_stat_t buf;

    if ( pass ) {

	if ( journal_file_path_ptr == NULL ) {

            failure_mssg = "journal_file_path_ptr NULL on entry?!?",
            pass = FALSE;

	} else if ( expected_file_path_ptr == NULL ) {

            failure_mssg = "expected_file_path_ptr NULL on entry?!?",
            pass = FALSE;

        }
    }

    if ( ( pass ) && ( verbose ) ) {

        HDfprintf(stdout, "%s: *journal_file_path_ptr = \"%s\"\n",
                  fcn_name, journal_file_path_ptr);
        HDfprintf(stdout, "%s: *expected_file_path_ptr = \"%s\"\n",
                  fcn_name, expected_file_path_ptr);
    }

    /* get the actual length of the journal file */
    if ( pass ) {

	if ( HDstat(journal_file_path_ptr, &buf) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat(j) failed with errno = %d.\n",
                          fcn_name, errno);
	    }
	    failure_mssg = "stat() failed on journal file.";
	    pass = FALSE;

	} else {

	    if ( (buf.st_size) == 0 ) {

                failure_mssg = "Journal file empty?!?";
	        pass = FALSE;

	    } else {
                
	        journal_len = (size_t)(buf.st_size);

		if ( verbose ) {

		    HDfprintf(stdout, "%s: journal_len = %d.\n", 
		              fcn_name, (int)journal_len);
		}
            }
	} 
    }

    /* get the actual length of the expected file */
    if ( pass ) {

	if ( HDstat(expected_file_path_ptr, &buf) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat(e) failed with errno = %d.\n",
                          fcn_name, errno);
	    }
	    failure_mssg = "stat() failed on expected file.";
	    pass = FALSE;

	} else {

	    if ( (buf.st_size) == 0 ) {

                failure_mssg = "Expected file empty?!?";
	        pass = FALSE;

	    } else {
                
	        expected_len = (size_t)(buf.st_size);

		if ( verbose ) {

		    HDfprintf(stdout, "%s: expected_len = %d.\n", 
		              fcn_name, (int)expected_len);
		}
            }
	} 
    }

    /* open the journal file */
    if ( pass ) {

	if ( (journal_file_fd = HDopen(journal_file_path_ptr, O_RDONLY, 0777)) 
	     == -1 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDopen(j) failed with errno = %d.\n",
                          fcn_name, errno);
	    }
            failure_mssg = "Can't open journal file.";
	    pass = FALSE;
        }
    }

    /* open the expected file */
    if ( pass ) {

	if ( (expected_file_fd = HDopen(expected_file_path_ptr, O_RDONLY, 0777)) 
	     == -1 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDopen(e) failed with errno = %d.\n",
                          fcn_name, errno);
	    }
            failure_mssg = "Can't open expected file.";
	    pass = FALSE;
        }
    }

    /* The first lines of the journal and expected files will usually differ
     * in magic number and creation date.  We could look at everything else 
     * on the line, but for now we will just skip past it, and compute the 
     * length of the remainder of the journal and expected files as we do so.
     *
     * Do this by reading the file one character at a time until we hit a 
     * newline.  This is very inefficient, but this is test code, and the 
     * first line can't be very long.
     */
    if ( pass ) {

        first_line_len = 1;
	read_result = HDread(journal_file_fd, &ch, 1);

	while ( ( ch != '\n' ) && 
		( first_line_len < 256 ) &&
	        ( read_result == 1 ) ) {

	    first_line_len++;
	    read_result = HDread(journal_file_fd, &ch, 1);
	}

	if ( ch != '\n' ) {
	
	    failure_mssg = "error skipping first line of journal file.";
	    pass = FALSE;

	} else if ( first_line_len > journal_len ) {

            failure_mssg = "first_line_len > journal_len?!?";
	    pass = FALSE;

	} else {

	    journal_remainder_len = journal_len - first_line_len;
	}
    }

    if ( pass ) {

        first_line_len = 1;
	read_result = HDread(expected_file_fd, &ch, 1);

	while ( ( ch != '\n' ) && 
		( first_line_len < 256 ) &&
	        ( read_result == 1 ) ) {

	    first_line_len++;
	    read_result = HDread(expected_file_fd, &ch, 1);
	}

	if ( ch != '\n' ) {
	
	    failure_mssg = "error skipping first line of expected file.";
	    pass = FALSE;

	} else if ( first_line_len > expected_len ) {

            failure_mssg = "first_line_len > expected_len?!?";
	    pass = FALSE;

	} else {

	    expected_remainder_len = expected_len - first_line_len;
	}
    }

    if ( pass ) {

        if ( journal_remainder_len != expected_remainder_len ) {

	    failure_mssg = "Unexpected journal file contents(1).";
	    pass = FALSE;
	}
    }

    /* If we get this far without an error, the lengths of the actual 
     * and expected files (after skipping the first line) are identical.
     * Thus we have to go and compare the actual data.
     */
    while ( ( pass ) &&
	    ( journal_remainder_len > 0 ) ) 
    {
        HDassert( journal_remainder_len == expected_remainder_len );

        if ( journal_remainder_len > max_buf_len ) {

            cur_buf_len = max_buf_len;
            journal_remainder_len -= max_buf_len;
            expected_remainder_len -= max_buf_len;

        } else {

            cur_buf_len = journal_remainder_len;
            journal_remainder_len = 0;
            expected_remainder_len = 0;
        }

        read_result = HDread(journal_file_fd, journal_buf, cur_buf_len);

        if ( read_result != (int)cur_buf_len ) {

            if ( verbose ) {

                HDfprintf(stdout, 
                          "%s: HDread(j) failed. result = %d, errno = %d.\n",
                          fcn_name, (int)read_result, errno);
            }
            failure_mssg = "error reading journal file.";
            pass = FALSE;
        }

        journal_buf[cur_buf_len] = '\0';

        if ( pass ) {

            read_result = HDread(expected_file_fd, expected_buf, cur_buf_len);

            if ( read_result != (int)cur_buf_len ) {

	        if ( verbose ) {

                    HDfprintf(stdout, 
                              "%s: HDread(e) failed. result = %d, errno = %d.\n",
                              fcn_name, (int)read_result, errno);
                }
                failure_mssg = "error reading expected file.";
                pass = FALSE;
            }

            expected_buf[cur_buf_len] = '\0';
        }

        if ( pass ) {

            if ( human_readable ) {

                if ( HDstrcmp(journal_buf, expected_buf) != 0 ) {

		    if ( verbose ) {

                        HDfprintf(stdout, 
                                  "expected_buf = \"%s\"\n", expected_buf);
                        HDfprintf(stdout, 
                                  "journal_buf  = \"%s\"\n", journal_buf);
                    }

                    failure_mssg = "Unexpected journal file contents(2).";
                    pass = FALSE;
                }
            } else { /* binary journal file -- can't use strcmp() */

                if ( HDmemcmp(journal_buf, expected_buf, (size_t)cur_buf_len)
                     != 0 ) {

                    failure_mssg = "Unexpected journal file contents(2b).";
                    pass = FALSE;
                }
            }
	}
    }

    if ( journal_file_fd != -1 ) {

        if ( HDclose(journal_file_fd) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDclose(j) failed with errno = %d.\n",
                          fcn_name, errno);
	    }

	    if ( pass ) {

                failure_mssg = "Can't close journal file.";
	        pass = FALSE;
	    }
	}
    }

    if ( expected_file_fd != -1 ) {

        if ( HDclose(expected_file_fd) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDclose(e) failed with errno = %d.\n",
                          fcn_name, errno);
	    }

	    if ( pass ) {

                failure_mssg = "Can't close expected file.";
	        pass = FALSE;
	    }
	}
    }

    return;

} /* verify_journal_contents() */


/*-------------------------------------------------------------------------
 * Function:    verify_journal_deleted()
 *
 * Purpose:     If pass is true on entry, stat the target journal file,
 * 		and verify that it does not exist.  If it does, set
 * 		pass to FALSE, and set failure_mssg to point to an 
 * 		appropriate failure message.  Similarly, if any errors 
 * 		are detected in this process, set pass to FALSE and set
 * 		failure_mssg to point to an appropriate error message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5//08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
verify_journal_deleted(const char * journal_file_path_ptr)
{
    const char * fcn_name = "verify_journal_deleted()";
    hbool_t verbose = FALSE;
    h5_stat_t buf;

    if ( pass ) {

	if ( journal_file_path_ptr == NULL ) {

            failure_mssg = "journal_file_path_ptr NULL on entry?!?",
            pass = FALSE;
	}
    }

    if ( pass ) {

	if ( HDstat(journal_file_path_ptr, &buf) == 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat(%s) succeeded.\n", fcn_name,
			  journal_file_path_ptr);
	    }

	    failure_mssg = "journal file not deleted(1).";
	    pass = FALSE;

        } else if ( errno != ENOENT ) {

	    if ( verbose ) {

	        HDfprintf(stdout, 
			  "%s: HDstat() failed with unexpected errno = %d.\n",
                          fcn_name, errno);
	    }
	    failure_mssg = "journal file not deleted(2).";
	    pass = FALSE;

	} 
    }

    return;

} /* verify_journal_deleted() */


/*-------------------------------------------------------------------------
 * Function:    verify_journal_empty()
 *
 * Purpose:     If pass is true on entry, stat the target journal file,
 * 		and verify that it has length zero.  If it is not, set
 * 		pass to FALSE, and set failure_mssg to point to an 
 * 		appropriate failure message.  Similarly, if any errors 
 * 		are detected in this process, set pass to FALSE and set
 * 		failure_mssg to point to an appropriate error message.
 *
 *              Do nothing if pass is FALSE on entry.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/06/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

static void
verify_journal_empty(const char * journal_file_path_ptr)
{
    const char * fcn_name = "verify_journal_empty()";
    hbool_t verbose = FALSE;
    h5_stat_t buf;

    if ( pass ) {

	if ( journal_file_path_ptr == NULL ) {

            failure_mssg = "journal_file_path_ptr NULL on entry?!?",
            pass = FALSE;
	}
    }

    if ( pass ) {

	if ( HDstat(journal_file_path_ptr, &buf) != 0 ) {

	    if ( verbose ) {

	        HDfprintf(stdout, "%s: HDstat() failed with errno = %d.\n",
                          fcn_name, errno);
	    }
	    failure_mssg = "stat() failed on journal file.";
	    pass = FALSE;

	} else {

	    if ( (buf.st_size) > 0 ) {

                failure_mssg = "Empty journal file expected.";
	        pass = FALSE;
            }
	} 
    }

    return;

} /* verify_journal_empty() */


/*** metadata journaling smoke checks ***/

/*-------------------------------------------------------------------------
 * Function:    mdj_smoke_check_00()
 *
 * Purpose:     Run a basic smoke check on the metadata journaling 
 *              facilities of the metadata cache.  Proceed as follows:
 *
 *              1) Create a file with journaling enabled.  Verify that 
 *                 journal file is created.
 *
 *              2) Using the test entries, simulate a selection of 
 *                 transactions, which exercise the full range of 
 *                 metadata cache API which can generate journal entries.  
 *                 Verify that these transactions are reflected correctly 
 *                 in the journal.
 *
 *              3) Close the hdf5 file, and verify that the journal file
 *                 is deleted.  Re-open the file with journaling, and 
 *                 do a transaction or two just to verify that the 
 *                 journaling is working.
 *
 *              4) Close the file, and verify that the journal is deleted.
 *
 *              5) Re-open the file with journaling disabled.  Do a 
 *                 transaction or two, and verify that the transactions
 *                 took place, and that there is no journal file.
 *
 *              6) Enable journaling on the open file.  Do a transaction
 *                 or two to verify that journaling is working.  
 *
 *              7) Disable journaling on the open file.  Verify that the
 *                 journal file has been deleted.
 *
 *              8) Close and delete the file.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              3/11/08
 *
 * Changes:	Modified function to run using either a human readable 
 *		or binary journal file.
 *							JRM -- 5/8/9
 *
 *		Modified function to run using either aio or sio.
 *							JRM -- 1/22/10
 *
 *-------------------------------------------------------------------------
 */

static void
mdj_smoke_check_00(hbool_t human_readable,
                   hbool_t use_aio)
{
    const char * fcn_name = "mdj_smoke_check_00()";
    const char * human_readable_testfiles[] = 
    {
        "testfiles/cache_journal_sc00_000.jnl",
        "testfiles/cache_journal_sc00_001.jnl",
        "testfiles/cache_journal_sc00_002.jnl",
        "testfiles/cache_journal_sc00_003.jnl",
        "testfiles/cache_journal_sc00_004.jnl",
        "testfiles/cache_journal_sc00_005.jnl",
        "testfiles/cache_journal_sc00_006.jnl",
        "testfiles/cache_journal_sc00_007.jnl",
        "testfiles/cache_journal_sc00_008.jnl",
        "testfiles/cache_journal_sc00_009.jnl",
        "testfiles/cache_journal_sc00_010.jnl",
        "testfiles/cache_journal_sc00_011.jnl",
        "testfiles/cache_journal_sc00_012.jnl",
        "testfiles/cache_journal_sc00_013.jnl",
        "testfiles/cache_journal_sc00_014.jnl",
        "testfiles/cache_journal_sc00_015.jnl",
        "testfiles/cache_journal_sc00_016.jnl",
        "testfiles/cache_journal_sc00_017.jnl",
        "testfiles/cache_journal_sc00_018.jnl",
	NULL
    };
    const char * binary_testfiles[] = 
    {
        "testfiles/cache_journal_bsc00_000.jnl",
        "testfiles/cache_journal_bsc00_001.jnl",
        "testfiles/cache_journal_bsc00_002.jnl",
        "testfiles/cache_journal_bsc00_003.jnl",
        "testfiles/cache_journal_bsc00_004.jnl",
        "testfiles/cache_journal_bsc00_005.jnl",
        "testfiles/cache_journal_bsc00_006.jnl",
        "testfiles/cache_journal_bsc00_007.jnl",
        "testfiles/cache_journal_bsc00_008.jnl",
        "testfiles/cache_journal_bsc00_009.jnl",
        "testfiles/cache_journal_bsc00_010.jnl",
        "testfiles/cache_journal_bsc00_011.jnl",
        "testfiles/cache_journal_bsc00_012.jnl",
        "testfiles/cache_journal_bsc00_013.jnl",
        "testfiles/cache_journal_bsc00_014.jnl",
        "testfiles/cache_journal_bsc00_015.jnl",
        "testfiles/cache_journal_bsc00_016.jnl",
        "testfiles/cache_journal_bsc00_017.jnl",
        "testfiles/cache_journal_bsc00_018.jnl",
	NULL
    };
    const char **testfiles;
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    hbool_t testfile_missing = FALSE;
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    hbool_t update_architypes;
    herr_t result;
    int cp = 0;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;
    H5AC_jnl_config_t jnl_config;
    
    if ( human_readable ) {

        testfiles = human_readable_testfiles;
        /* set update_architypes to TRUE to generate new architype files */
        update_architypes = FALSE;

        if ( use_aio ) {

            TESTING("human readable aio mdj smoke check 00 -- general coverage");

        } else {

            TESTING("human readable sio mdj smoke check 00 -- general coverage");
        }
    } else {

        testfiles = binary_testfiles;
        /* set update_architypes to TRUE to generate new architype files */
        update_architypes = TRUE;

        if ( use_aio ) {

            TESTING("binary aio mdj smoke check 00 -- general coverage");

        } else {

            TESTING("binary sio mdj smoke check 00 -- general coverage");
        }
    }

    pass = TRUE;

    /***********************************************************************/
    /* 1) Create a file with cache configuration set to enable journaling. */
    /***********************************************************************/

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
				            == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (1).\n";
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( verbose ) { 
        HDfprintf(stdout, "%s: filename = \"%s\".\n", fcn_name, filename); 
    }

    /* setup the journal file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename, 
                        sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
	else if ( HDstrlen(journal_filename) >= 
			H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( verbose ) { 
        HDfprintf(stdout, "%s: journal filename = \"%s\".\n", 
		  fcn_name, journal_filename); 
    }

    /* clean out any existing journal file */
    HDremove(journal_filename);
    setup_cache_for_journaling(filename, journal_filename, &file_id,
                               &file_ptr, &cache_ptr, human_readable, 
                               use_aio, FALSE);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
 

    /********************************************************************/
    /* 2) Using the test entries, simulate a selection of transactions, */
    /*    which exercise the full range of metadata cache API calls     */
    /*    that can generate journal entries.  Verify that these         */
    /*    transactions are reflected correctly in the journal.          */
    /********************************************************************/

    /* a) First a quick check to see if we can do anything */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.0");

    insert_entry(file_ptr, 0, 1, FALSE, H5C__NO_FLAGS_SET); 

    protect_entry(file_ptr, 0, 0);

    unprotect_entry(file_ptr, 0, 0, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.0");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[0]);
    }

    if ( file_exists(testfiles[0]) ) {

        verify_journal_contents(journal_filename, testfiles[0], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* b) Verify that a sequence of cache operation that do not dirty
     *    any entry do not result in any journal activity.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.1");

    protect_entry(file_ptr, TINY_ENTRY_TYPE, 0);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 1);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 2);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 2, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 1, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);

    protect_entry_ro(file_ptr, TINY_ENTRY_TYPE, 3);
    protect_entry_ro(file_ptr, TINY_ENTRY_TYPE, 3);
    protect_entry_ro(file_ptr, TINY_ENTRY_TYPE, 3);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 3, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 3, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 3, H5C__NO_FLAGS_SET);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.1");

    flush_journal(cache_ptr);

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* c) Verify that the most recently dirtied entry get to the head of 
     *    the transaction list (and thus appears as the last entry in the
     *    transaction).
     */

    begin_trans(cache_ptr, verbose, (uint64_t)2, "transaction 2.1");

    protect_entry(file_ptr, TINY_ENTRY_TYPE, 0);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 1);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 2);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 2, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 3);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 4);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 4, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 5);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 5, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 3);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, TINY_ENTRY_TYPE, 1);
    unprotect_entry(file_ptr, TINY_ENTRY_TYPE, 1, H5C__NO_FLAGS_SET);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)2, "transaction 2.1");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[1]);
    }
    
    if ( file_exists(testfiles[1]) ) {

        verify_journal_contents(journal_filename, testfiles[1], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* d) Mix up some protect/unprotect calls with moves.  Do this with
     *    different orders to make things interesting.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.2");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 0);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 1);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 2);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 2, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 2);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 2, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 3);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 3, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 4);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 4, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 2, FALSE);
    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 3, FALSE);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[2]);
    }
    
    if ( file_exists(testfiles[2]) ) {

        verify_journal_contents(journal_filename, testfiles[2], human_readable);

    } else {

    	testfile_missing = TRUE;
    }



    begin_trans(cache_ptr, verbose, (uint64_t)2, "transaction 2.2");

    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 3, TRUE);
    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 2, TRUE);

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 0);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 1);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 2);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 2, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 3);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 4);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 4, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 5);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 5, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)2, "transaction 2.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[3]);
    }
    
    if ( file_exists(testfiles[3]) ) {

        verify_journal_contents(journal_filename, testfiles[3], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);



    /* e-1) Start by pinning a selection of entries... */

    begin_trans(cache_ptr, verbose, (uint64_t)3, "transaction 3.2");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 0);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 1);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 2);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 2, H5C__PIN_ENTRY_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 3);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG | H5C__PIN_ENTRY_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 4);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 4, H5C__PIN_ENTRY_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 5);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 5, H5C__DIRTIED_FLAG | H5C__PIN_ENTRY_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 6);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 6, H5C__PIN_ENTRY_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 7);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 7, H5C__DIRTIED_FLAG | H5C__PIN_ENTRY_FLAG);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 8);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 8, H5C__NO_FLAGS_SET);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 9);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 9, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)3, "transaction 3.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[4]);
    }
    
    if ( file_exists(testfiles[4]) ) {

        verify_journal_contents(journal_filename, testfiles[4], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* e-2) ... then use the H5C_mark_entry_dirty()
     *      call to mark a variety of protected, pinned, and pinned and 
     *      protected entries dirty.  Also move some pinned entries.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)4, "transaction 4.2");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 0);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 1);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 6);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 7);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 8);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 9);

    mark_entry_dirty(file_ptr, MICRO_ENTRY_TYPE, 0);
    mark_entry_dirty(file_ptr, MICRO_ENTRY_TYPE, 1);
    mark_entry_dirty(file_ptr, MICRO_ENTRY_TYPE, 2);
    mark_entry_dirty(file_ptr, MICRO_ENTRY_TYPE, 3);
    mark_entry_dirty(file_ptr, MICRO_ENTRY_TYPE, 6);
    mark_entry_dirty(file_ptr, MICRO_ENTRY_TYPE, 7);

    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 4, FALSE);
    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 5, FALSE);

    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 6, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 7, H5C__DIRTIED_FLAG);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 8, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 9, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)4, "transaction 4.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[5]);
    }
    
    if ( file_exists(testfiles[5]) ) {

        verify_journal_contents(journal_filename, testfiles[5], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* e-3) ...finally, upin all the pinned entries, with an undo of the
     *      previous move in the middle.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)5, "transaction 5.2");

    unpin_entry(file_ptr, MICRO_ENTRY_TYPE, 2);
    unpin_entry(file_ptr, MICRO_ENTRY_TYPE, 3);
    unpin_entry(file_ptr, MICRO_ENTRY_TYPE, 4);

    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 4, TRUE);
    move_entry(cache_ptr, MICRO_ENTRY_TYPE, 5, TRUE);

    unpin_entry(file_ptr, MICRO_ENTRY_TYPE, 5);
    unpin_entry(file_ptr, MICRO_ENTRY_TYPE, 6);
    unpin_entry(file_ptr, MICRO_ENTRY_TYPE, 7);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)5, "transaction 5.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[6]);
    }
    
    if ( file_exists(testfiles[6]) ) {

        verify_journal_contents(journal_filename, testfiles[6], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);



    /* f-1) Pin a bunch more entries -- make them variable size, as we need
     *      to test resizing.  In passing, pin some of the entries using 
     *      the H5C_pin_ptrotected_entry() call.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)6, "transaction 6.2");

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 4);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 5);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 6);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 7);

    pin_protected_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2);
    pin_protected_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3);

    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2, H5C__NO_FLAGS_SET);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 4, H5C__PIN_ENTRY_FLAG);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 5, H5C__DIRTIED_FLAG | H5C__PIN_ENTRY_FLAG);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 6, H5C__PIN_ENTRY_FLAG);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 7, H5C__DIRTIED_FLAG | H5C__PIN_ENTRY_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)6, "transaction 6.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[7]);
    }
    
    if ( file_exists(testfiles[7]) ) {

        verify_journal_contents(journal_filename, testfiles[7], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);



    /* f-2) Now resize a selection of pinned and unpinned entries via 
     *      protect/unprotect pairs and H5C_resize_entry().
     */


    begin_trans(cache_ptr, verbose, (uint64_t)7, "transaction 7.2");

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0,
            ((VARIABLE_ENTRY_SIZE / 16) * 15), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0, H5C__DIRTIED_FLAG);

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1,
            ((VARIABLE_ENTRY_SIZE / 16) * 14), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2,
            ((VARIABLE_ENTRY_SIZE / 16) * 13), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2, H5C__DIRTIED_FLAG);

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3,
            ((VARIABLE_ENTRY_SIZE / 16) * 12), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG);

    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 4, 
		         ((VARIABLE_ENTRY_SIZE / 16) * 11), TRUE);

    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 5, 
		         ((VARIABLE_ENTRY_SIZE / 16) * 10), TRUE);

    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 6,
		             ((VARIABLE_ENTRY_SIZE / 16) * 9), TRUE);

    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 7,
		             ((VARIABLE_ENTRY_SIZE / 16) * 8), TRUE);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)7, "transaction 7.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[8]);
    }
    
    if ( file_exists(testfiles[8]) ) {

        verify_journal_contents(journal_filename, testfiles[8], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    

    /* f-3) Now put all the sizes back, and also move all the entries. */


    begin_trans(cache_ptr, verbose, (uint64_t)8, "transaction 8.2");

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 0, FALSE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 1, FALSE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 2, FALSE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 3, FALSE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 4, FALSE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 4, VARIABLE_ENTRY_SIZE, TRUE);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 5, FALSE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 5, VARIABLE_ENTRY_SIZE, TRUE);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 6, FALSE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 6, VARIABLE_ENTRY_SIZE, TRUE);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 7, FALSE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 7, VARIABLE_ENTRY_SIZE, TRUE);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)8, "transaction 8.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[9]);
    }
    
    if ( file_exists(testfiles[9]) ) {

        verify_journal_contents(journal_filename, testfiles[9], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
    
    
    
    /* f-4) Finally, move all the entries back to their original locations,
     *      and unpin all the pinned entries.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)9, "transaction 9.2");

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 0, TRUE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 0, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 1, TRUE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 1, H5C__DIRTIED_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 2, TRUE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 2, H5C__DIRTIED_FLAG | H5C__UNPIN_ENTRY_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 3, TRUE);
    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3, VARIABLE_ENTRY_SIZE, TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 3, H5C__DIRTIED_FLAG | H5C__UNPIN_ENTRY_FLAG);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 4, TRUE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 4, VARIABLE_ENTRY_SIZE, TRUE);
    unpin_entry(file_ptr, VARIABLE_ENTRY_TYPE, 4);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 5, TRUE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 5, VARIABLE_ENTRY_SIZE, TRUE);
    unpin_entry(file_ptr, VARIABLE_ENTRY_TYPE, 5);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 6, TRUE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 6, VARIABLE_ENTRY_SIZE, TRUE);
    unpin_entry(file_ptr, VARIABLE_ENTRY_TYPE, 6);

    move_entry(cache_ptr, VARIABLE_ENTRY_TYPE, 7, TRUE);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 7, VARIABLE_ENTRY_SIZE, TRUE);
    unpin_entry(file_ptr, VARIABLE_ENTRY_TYPE, 7);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)9, "transaction 9.2");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[10]);
    }
    
    if ( file_exists(testfiles[10]) ) {

        verify_journal_contents(journal_filename, testfiles[10], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
    
    
    
    /* g) verify that the journaling code handles a cascade of changes
     *    caused when the serialization of an entry causes dirties, resizes,
     *    and/or resizes of other entries.
     *
     * g-1) Load several entries of VARIABLE_ENTRY_TYPE into the cache, and
     *      set their sizes to values less than the maximum.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.3");

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 10);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 10,
        ((VARIABLE_ENTRY_SIZE / 4) * 1), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 10, H5C__DIRTIED_FLAG);

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 11);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 11,
        ((VARIABLE_ENTRY_SIZE / 4) * 2), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 11, H5C__DIRTIED_FLAG);

    protect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 12);
    resize_entry(file_ptr, VARIABLE_ENTRY_TYPE, 12,
        ((VARIABLE_ENTRY_SIZE / 4) * 3), TRUE);
    unprotect_entry(file_ptr, VARIABLE_ENTRY_TYPE, 12, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.3");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[11]);
    }
    
    if ( file_exists(testfiles[11]) ) {

        verify_journal_contents(journal_filename, testfiles[11], human_readable);

    } else {

    	testfile_missing = TRUE;
    }


    /* g-2) Now setup flush operations on some entries to dirty, resize,
     *      and/or move other entries.  When these entries are dirtied
     *      in a transaction, the associated flush operations should be
     *      triggered and appear in the journal.
     *
     *      In case you need a score card, in what follows, I set up the
     *      following dependencies:
     *
     *      (MICRO_ENTRY_TYPE, 20) dirties (MICRO_ENTRY_TYPE, 30)
     *
     *      (MICRO_ENTRY_TYPE, 21) moves, resizes, and dirties:
     *      				   (VARIABLE_ENTRY_TYPE, 10)
     *      				   (VARIABLE_ENTRY_TYPE, 13)
     *
     *      (MICRO_ENTRY_TYPE, 22) resizes (VARIABLE_ENTRY_TYPE, 11)
     *                                     (VARIABLE_ENTRY_TYPE, 12)
     *
     *      (MICRO_ENTRY_TYPE, 23) moves (VARIABLE_ENTRY_TYPE, 10)
     *                                     (VARIABLE_ENTRY_TYPE, 13)
     *                                     to their original locations
     *
     *      (MICRO_ENTRY_TYPE, 24) dirties (MICRO_ENTRY_TYPE, 21)
     *
     *      (MICRO_ENTRY_TYPE, 25) dirties (MICRO_ENTRY_TYPE, 22)
     *                                     (MICRO_ENTRY_TYPE, 23)
     *
     */

    add_flush_op(MICRO_ENTRY_TYPE, 20, 
		  FLUSH_OP__DIRTY, MICRO_ENTRY_TYPE, 30, FALSE, 0);


    add_flush_op(MICRO_ENTRY_TYPE, 21,
	  FLUSH_OP__RESIZE, VARIABLE_ENTRY_TYPE, 10, FALSE, VARIABLE_ENTRY_SIZE);
    add_flush_op(MICRO_ENTRY_TYPE, 21,
	  FLUSH_OP__MOVE, VARIABLE_ENTRY_TYPE, 10, FALSE, 0);
    add_flush_op(MICRO_ENTRY_TYPE, 21,
	  FLUSH_OP__DIRTY, VARIABLE_ENTRY_TYPE, 10, FALSE, 0);

    add_flush_op(MICRO_ENTRY_TYPE, 21,
	  FLUSH_OP__RESIZE, VARIABLE_ENTRY_TYPE, 13, FALSE, VARIABLE_ENTRY_SIZE/4);
    add_flush_op(MICRO_ENTRY_TYPE, 21,
	  FLUSH_OP__MOVE, VARIABLE_ENTRY_TYPE, 13, FALSE, 0);
    add_flush_op(MICRO_ENTRY_TYPE, 21,
	  FLUSH_OP__DIRTY, VARIABLE_ENTRY_TYPE, 13, FALSE, 0);


    add_flush_op(MICRO_ENTRY_TYPE, 22,
	  FLUSH_OP__RESIZE, VARIABLE_ENTRY_TYPE, 11, FALSE, VARIABLE_ENTRY_SIZE);

    add_flush_op(MICRO_ENTRY_TYPE, 22,
	  FLUSH_OP__RESIZE, VARIABLE_ENTRY_TYPE, 12, FALSE, VARIABLE_ENTRY_SIZE);


    add_flush_op(MICRO_ENTRY_TYPE, 23,
	  FLUSH_OP__MOVE, VARIABLE_ENTRY_TYPE, 10, TRUE, 0);

    add_flush_op(MICRO_ENTRY_TYPE, 23,
	  FLUSH_OP__MOVE, VARIABLE_ENTRY_TYPE, 13, TRUE, 0);


    add_flush_op(MICRO_ENTRY_TYPE, 24,
	  FLUSH_OP__DIRTY, MICRO_ENTRY_TYPE, 21, FALSE, 0);


    add_flush_op(MICRO_ENTRY_TYPE, 25,
	  FLUSH_OP__DIRTY, MICRO_ENTRY_TYPE, 22, FALSE, 0);

    add_flush_op(MICRO_ENTRY_TYPE, 25,
	  FLUSH_OP__DIRTY, MICRO_ENTRY_TYPE, 23, FALSE, 0);


    /* g-3) Start with a simple check -- dirty (MICRO_ENTRY_TYPE, 20),
     *      which should also dirty (MICRO_ENTRY_TYPE, 30) when 
     *      (MICRO_ENTRY_TYPE, 20) is serialized at transaction close.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)2, "transaction 2.3");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 20);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 20, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)2, "transaction 2.3");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[12]);
    }
    
    if ( file_exists(testfiles[12]) ) {

        verify_journal_contents(journal_filename, testfiles[12], human_readable);

    } else {

    	testfile_missing = TRUE;
    }


    /* g-4) Now dirty (MICRO_ENTRY_TYPE, 24), which dirties 
     *      (MICRO_ENTRY_TYPE, 21), which dirties, resizes, and 
     *      moves (VARIABLE_ENTRY_TYPE, 10) and (VARIABLE_ENTRY_TYPE, 13)
     */

    begin_trans(cache_ptr, verbose, (uint64_t)3, "transaction 3.3");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 24);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 24, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)3, "transaction 3.3");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[13]);
    }
    
    if ( file_exists(testfiles[13]) ) {

        verify_journal_contents(journal_filename, testfiles[13], human_readable);

    } else {

    	testfile_missing = TRUE;
    }


    /* g-4) Now dirty (MICRO_ENTRY_TYPE, 25), which dirties 
     *      (MICRO_ENTRY_TYPE, 22) and (MICRO_ENTRY_TYPE, 23), which 
     *      in turn resize (VARIABLE_ENTRY_TYPE, 11) and 
     *      (VARIABLE_ENTRY_TYPE, 12), and move (VARIABLE_ENTRY_TYPE, 10)
     *      and (VARIABLE_ENTRY_TYPE, 13) back to their original locations.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)4, "transaction 4.3");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 25);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 25, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)4, "transaction 4.3");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[14]);
    }
    
    if ( file_exists(testfiles[14]) ) {

        verify_journal_contents(journal_filename, testfiles[14], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
    


    /* h) Dirty an entry, and then expunge it.  Entry should not appear
     *    in the journal.  Do this twice -- first with only the expunge
     *    entry in the transaction, and a second time with other entries
     *    involved.
     *
     *    Note that no journal file will be written until the first 
     *    entry, so start with a transaction that generates some data.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.4");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 39);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 39, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.4");

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    begin_trans(cache_ptr, verbose, (uint64_t)2, "transaction 2.4");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 40);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 40, H5C__DIRTIED_FLAG);

    expunge_entry(file_ptr, MICRO_ENTRY_TYPE, 40);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)2, "transaction 2.4");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[15]);
    }
    
    if ( file_exists(testfiles[15]) ) {

        verify_journal_contents(journal_filename, testfiles[15], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
    


    begin_trans(cache_ptr, verbose, (uint64_t)3, "transaction 3.4");

    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 41);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 41, H5C__DIRTIED_FLAG);
    expunge_entry(file_ptr, MICRO_ENTRY_TYPE, 41);
    protect_entry(file_ptr, MICRO_ENTRY_TYPE, 42);
    unprotect_entry(file_ptr, MICRO_ENTRY_TYPE, 42, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)3, "transaction 3.4");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[16]);
    }
    
    if ( file_exists(testfiles[16]) ) {

        verify_journal_contents(journal_filename, testfiles[16], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /************************************************************/
    /* 3) Close the hdf5 file, and verify that the journal file */
    /*    is deleted.  Re-open the file with journaling, and    */
    /*    do a transaction or two just to verify that the       */
    /*    journaling is working.                                */
    /************************************************************/

    /* a) Close the hdf5 file. */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

	    pass = FALSE;
	    failure_mssg = "temporary H5Fclose() failed.\n";

	} else {
	    file_id = -1;
	    file_ptr = NULL;
	    cache_ptr = NULL;
	}
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* b) Verify that the journal file has been deleted. */
    verify_journal_deleted(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* c) Re-open the hdf5 file. */
    open_existing_file_for_journaling(filename, journal_filename, &file_id,
                                      &file_ptr, &cache_ptr, human_readable, 
                                      use_aio);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* d) do a transaction or to to verify that journaling is working. */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.5");

    insert_entry(file_ptr, 0, 1, FALSE, H5C__NO_FLAGS_SET); 
    protect_entry(file_ptr, 0, 0);
    unprotect_entry(file_ptr, 0, 0, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.5");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[17]);
    }
    
    if ( file_exists(testfiles[17]) ) {

        verify_journal_contents(journal_filename, testfiles[17], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    /**************************************************************/
    /* 4) Close the file, and verify that the journal is deleted. */
    /**************************************************************/

    /* Close the hdf5 file. */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

	    pass = FALSE;
	    failure_mssg = "temporary H5Fclose() failed.\n";

	} else {
	    file_id = -1;
	    file_ptr = NULL;
	    cache_ptr = NULL;
	}
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* b) Verify that the journal file has been deleted. */
    verify_journal_deleted(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /************************************************************/
    /* 5) Re-open the file with journaling disabled.  Do a      */
    /*    transaction or two, and verify that the transactions  */
    /*    took place, and that there is no journal file.        */
    /************************************************************/

    /* re-open the file without journaling enabled */

    open_existing_file_without_journaling(filename, &file_id, 
                                          &file_ptr, &cache_ptr);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);


    /* do a transaction to verify that journaling is disabled.  
     *
     * Note that we will only get a transaction number of zero if 
     * journaling is disabled -- thus the following begin/end trans
     * calls should fail if journaling is enabled.
     */

    begin_trans(cache_ptr, verbose, (uint64_t)0, "transaction 1.6");

    insert_entry(file_ptr, 0, 10, FALSE, H5C__NO_FLAGS_SET); 
    protect_entry(file_ptr, 0, 0);
    unprotect_entry(file_ptr, 0, 0, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)0, "transaction 1.6");

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( ( pass ) && ( cache_ptr->mdj_enabled ) ) {

        pass = FALSE;
        failure_mssg = "journaling is enabled?!?!(1).\n";
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    /* note that flush_journal() will throw an exception if journaling
     * is not enabled, so we don't call it here.  Instead, just call
     * verify_journal_deleted() to verify that there is no journal file.
     */

    verify_journal_deleted(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

 
    /************************************************************/
    /* 6) Enable journaling on the open file.  Do a transaction */
    /*    or two to verify that journaling is working.          */
    /************************************************************/

    /* now enable journaling */
    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = TRUE;

        HDstrcpy(jnl_config.journal_file_path, journal_filename);

        jnl_config.journal_recovered       = FALSE;
        jnl_config.jbrb_buf_size           = (8 * 1024);
        jnl_config.jbrb_num_bufs           = 2;
        jnl_config.jbrb_use_aio            = FALSE;
        jnl_config.jbrb_human_readable     = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( pass ) {

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    /* do a transaction or to to verify that journaling is working. */

    begin_trans(cache_ptr, verbose, (uint64_t)1, "transaction 1.7");

    insert_entry(file_ptr, 0, 20, FALSE, H5C__NO_FLAGS_SET); 
    protect_entry(file_ptr, 0, 0);
    unprotect_entry(file_ptr, 0, 0, H5C__DIRTIED_FLAG);

    end_trans(file_ptr, cache_ptr, verbose, (uint64_t)1, "transaction 1.7");

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[18]);
    }
    
    if ( file_exists(testfiles[18]) ) {

        verify_journal_contents(journal_filename, testfiles[18], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

 
    /************************************************************/
    /* 7) Disable journaling on the open file.  Verify that the */
    /*    journal file has been deleted.                        */
    /************************************************************/

    /* disable journaling */
    if ( pass ) {

        jnl_config.enable_journaling       = FALSE;

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    verify_journal_deleted(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

 
    /*********************************/
    /* 8) Close and delete the file. */
    /*********************************/
 
    if ( pass ) {

        if ( H5Fclose(file_id) < 0 ) {

            pass = FALSE;
	    failure_mssg = "H5Fclose(file_id) failed.\n";

	}
    }

    HDremove(filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( pass ) { 
	    
        PASSED(); 

	if ( testfile_missing ) {

	    puts("	WARNING: One or more missing test files."); 
	    fflush(stdout);
        }
    } else { 
	    
        H5_FAILED(); 
    }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);
    }

    return;

} /* mdj_smoke_check_00() */


/*-------------------------------------------------------------------------
 * Function:    mdj_smoke_check_01()
 *
 * Purpose:     Run a cut down version of smoke_check_1 in cache.c, with
 * 		journaling enabled.  Check the journal files generated,
 * 		and verify that the journal output matches the architype
 * 		test files.  Skip the comparison and generate a warning
 * 		if an architype file is missing.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/19/08
 *
 * Changes:	Modified function to run using either a human readable 
 *		or binary journal file.
 *							JRM -- 5/13/09
 *
 *		Modified function to run using either aio or sio.
 *							JRM -- 1/22/10
 *
 *-------------------------------------------------------------------------
 */

static void
mdj_smoke_check_01(hbool_t human_readable,
                   hbool_t use_aio)
{
    const char * fcn_name = "mdj_smoke_check_01()";
    const char * human_readable_testfiles[] = 
    {
        "testfiles/cache_journal_sc01_000.jnl",
        "testfiles/cache_journal_sc01_001.jnl",
        "testfiles/cache_journal_sc01_002.jnl",
        "testfiles/cache_journal_sc01_003.jnl",
        "testfiles/cache_journal_sc01_004.jnl",
	NULL
    };
    const char * binary_testfiles[] = 
    {
        "testfiles/cache_journal_bsc01_000.jnl",
        "testfiles/cache_journal_bsc01_001.jnl",
        "testfiles/cache_journal_bsc01_002.jnl",
        "testfiles/cache_journal_bsc01_003.jnl",
        "testfiles/cache_journal_bsc01_004.jnl",
	NULL
    };
    const char **testfiles;
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    hbool_t testfile_missing = FALSE;
    hbool_t show_progress = FALSE;
    hbool_t dirty_inserts = FALSE;
    hbool_t verbose = FALSE;
    hbool_t update_architypes;
    int dirty_unprotects = FALSE;
    int dirty_destroys = FALSE;
    hbool_t display_stats = FALSE;
    int32_t lag = 10;
    int cp = 0;
    int32_t max_index = 128;
    uint64_t trans_num = 0;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    if ( human_readable ) {

        testfiles = human_readable_testfiles;
        /* set update_architypes to TRUE to generate new architype files */
        update_architypes = FALSE;
        TESTING("hr mdj smoke check 01 -- jnl clean ins, prot, unprot, del, ren");

    } else {

        testfiles = binary_testfiles;
        /* set update_architypes to TRUE to generate new architype files */
        update_architypes = TRUE;
        TESTING("b mdj smoke check 01 -- jnl clean ins, prot, unprot, del, ren");
    }

    pass = TRUE;

    /********************************************************************/
    /* Create a file with cache configuration set to enable journaling. */
    /********************************************************************/

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
				            == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (1).\n";
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( verbose ) { 
        HDfprintf(stdout, "%s: filename = \"%s\".\n", fcn_name, filename); 
    }

    /* setup the journal file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename, 
                        sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
	else if ( HDstrlen(journal_filename) >= 
			H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( verbose ) { 
        HDfprintf(stdout, "%s: journal filename = \"%s\".\n", 
		  fcn_name, journal_filename); 
    }

    /* clean out any existing journal file */
    HDremove(journal_filename);

    /* Unfortunately, we get different journal output depending on the 
     * file driver, as at present we are including the end of address
     * space in journal entries, and the end of address space seems to 
     * be in part a function of the file driver.  
     *
     * Thus, if we want to use the core file driver when available, we 
     * will either have to remove the end of address space from the 
     * journal entries, get the different file drivers to agree on 
     * end of address space, or maintain different sets of architype
     * files for the different file drivers.
     */
    setup_cache_for_journaling(filename, journal_filename, &file_id,
                               &file_ptr, &cache_ptr, human_readable, 
                               use_aio, FALSE);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
 

    /******************************************/
    /* Run a small, fairly simple stress test */
    /******************************************/

    trans_num = 0;

    jrnl_row_major_scan_forward(/* file_ptr               */ file_ptr,
                                 /* max_index              */ max_index,
                                 /* lag                    */ lag,
                                 /* verbose                */ verbose,
                                 /* reset_stats            */ TRUE,
                                 /* display_stats          */ display_stats,
                                 /* display_detailed_stats */ FALSE,
                                 /* do_inserts             */ TRUE,
                                 /* dirty_inserts          */ dirty_inserts,
                                 /* do_moves             */ TRUE,
                                 /* move_to_main_addr    */ FALSE,
                                 /* do_destroys            */ TRUE,
                                 /* do_mult_ro_protects    */ TRUE,
                                 /* dirty_destroys         */ dirty_destroys,
                                 /* dirty_unprotects       */ dirty_unprotects,
                                 /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[0]);
    }

    if ( file_exists(testfiles[0]) ) {

        verify_journal_contents(journal_filename, testfiles[0], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    trans_num = 0;

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_row_major_scan_backward(/* file_ptr               */ file_ptr,
                                  /* max_index              */ max_index,
                                  /* lag                    */ lag,
                                  /* verbose                */ verbose,
                                  /* reset_stats            */ TRUE,
                                  /* display_stats          */ display_stats,
                                  /* display_detailed_stats */ FALSE,
                                  /* do_inserts             */ FALSE,
                                  /* dirty_inserts          */ dirty_inserts,
                                  /* do_moves             */ TRUE,
                                  /* move_to_main_addr    */ TRUE,
                                  /* do_destroys            */ FALSE,
                                  /* do_mult_ro_protects    */ TRUE,
                                  /* dirty_destroys         */ dirty_destroys,
                                  /* dirty_unprotects       */ dirty_unprotects,
                                  /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[1]);
    }
    
    if ( file_exists(testfiles[1]) ) {

        verify_journal_contents(journal_filename, testfiles[1], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    trans_num = 0;

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_row_major_scan_forward(/* file_ptr               */ file_ptr,
                                 /* max_index              */ max_index,
                                 /* lag                    */ lag,
                                 /* verbose                */ verbose,
                                 /* reset_stats            */ TRUE,
                                 /* display_stats          */ display_stats,
                                 /* display_detailed_stats */ FALSE,
                                 /* do_inserts             */ TRUE,
                                 /* dirty_inserts          */ dirty_inserts,
                                 /* do_moves             */ TRUE,
                                 /* move_to_main_addr    */ FALSE,
                                 /* do_destroys            */ TRUE,
                                 /* do_mult_ro_protects    */ TRUE,
                                 /* dirty_destroys         */ dirty_destroys,
                                 /* dirty_unprotects       */ dirty_unprotects,
                                 /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[2]);
    }
    
    if ( file_exists(testfiles[2]) ) {

        verify_journal_contents(journal_filename, testfiles[2], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    trans_num = 0;

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_col_major_scan_forward(/* file_ptr               */ file_ptr,
                                 /* max_index              */ max_index,
                                 /* lag                    */ lag,
                                 /* verbose                */ verbose,
                                 /* reset_stats            */ TRUE,
                                 /* display_stats          */ display_stats,
                                 /* display_detailed_stats */ TRUE,
                                 /* do_inserts             */ TRUE,
                                 /* dirty_inserts          */ dirty_inserts,
                                 /* dirty_unprotects       */ dirty_unprotects,
                                 /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[3]);
    }
    
    if ( file_exists(testfiles[3]) ) {

        verify_journal_contents(journal_filename, testfiles[3], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    trans_num = 0;

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_col_major_scan_backward(/* file_ptr               */ file_ptr,
                                  /* max_index              */ max_index,
                                  /* lag                    */ lag,
                                  /* verbose                */ verbose,
                                  /* reset_stats            */ TRUE,
                                  /* display_stats          */ display_stats,
                                  /* display_detailed_stats */ TRUE,
                                  /* do_inserts             */ TRUE,
                                  /* dirty_inserts          */ dirty_inserts,
                                  /* dirty_unprotects       */ dirty_unprotects,
                                  /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[4]);
    }
    
    if ( file_exists(testfiles[4]) ) {

        verify_journal_contents(journal_filename, testfiles[4], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    /****************************************************/
    /* Close and discard the file and the journal file. */
    /****************************************************/

    takedown_cache_after_journaling(file_id, filename, journal_filename, FALSE);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    verify_clean();
    verify_unprotected();

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( pass ) { 
	    
        PASSED(); 

	if ( testfile_missing ) {

	    puts("	WARNING: One or more missing test files."); 
	    fflush(stdout);
        }
    } else { 
	    
        H5_FAILED(); 
    }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);
    }

    return;

} /* mdj_smoke_check_01() */


/*-------------------------------------------------------------------------
 * Function:    mdj_smoke_check_02()
 *
 * Purpose:     Run a cut down version of smoke_check_2 in cache.c, with
 * 		journaling enabled.  Check the journal files generated,
 * 		and verify that the journal output matches the architype
 * 		test files.  Skip the comparison and generate a warning
 * 		if an architype file is missing.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              5/19/08
 *
 * Changes:	Modified function to run using either a human readable 
 *		or binary journal file.
 *							JRM -- 5/13/09
 *
 *		Modified function to run using either aio or sio.
 *							JRM -- 1/22/10
 *
 *-------------------------------------------------------------------------
 */

static void
mdj_smoke_check_02(hbool_t human_readable,
                   hbool_t use_aio)
{
    const char * fcn_name = "mdj_smoke_check_02()";
    const char * human_readable_testfiles[] = 
    {
        "testfiles/cache_journal_sc02_000.jnl",
        "testfiles/cache_journal_sc02_001.jnl",
        "testfiles/cache_journal_sc02_002.jnl",
        "testfiles/cache_journal_sc02_003.jnl",
        "testfiles/cache_journal_sc02_004.jnl",
	NULL
    };
    const char * binary_testfiles[] = 
    {
        "testfiles/cache_journal_bsc02_000.jnl",
        "testfiles/cache_journal_bsc02_001.jnl",
        "testfiles/cache_journal_bsc02_002.jnl",
        "testfiles/cache_journal_bsc02_003.jnl",
        "testfiles/cache_journal_bsc02_004.jnl",
	NULL
    };
    const char **testfiles;
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    hbool_t testfile_missing = FALSE;
    hbool_t show_progress = FALSE;
    hbool_t dirty_inserts = TRUE;
    hbool_t verbose = FALSE;
    hbool_t update_architypes = FALSE;
    int dirty_unprotects = TRUE;
    int dirty_destroys = TRUE;
    hbool_t display_stats = FALSE;
    int32_t lag = 10;
    int cp = 0;
    int32_t max_index = 128;
    uint64_t trans_num = 0;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    if ( human_readable ) {

        testfiles = human_readable_testfiles;
        /* set update_architypes to TRUE to generate new architype files */
        update_architypes = FALSE;
        TESTING("hr mdj smoke check 02 -- jnl dirty ins, prot, unprot, del, ren");

    } else {

        testfiles = binary_testfiles;
        /* set update_architypes to TRUE to generate new architype files */
        update_architypes = FALSE;
        TESTING("b mdj smoke check 02 -- jnl dirty ins, prot, unprot, del, ren");
    }

    pass = TRUE;

    /********************************************************************/
    /* Create a file with cache configuration set to enable journaling. */
    /********************************************************************/

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (1).\n";
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( verbose ) { 
        HDfprintf(stdout, "%s: filename = \"%s\".\n", fcn_name, filename); 
    }

    /* setup the journal file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename, sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
	else if ( HDstrlen(journal_filename) >= H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( verbose ) { 
        HDfprintf(stdout, "%s: journal filename = \"%s\".\n", 
		  fcn_name, journal_filename); 
    }

    /* clean out any existing journal file */
    HDremove(journal_filename);

    /* Unfortunately, we get different journal output depending on the 
     * file driver, as at present we are including the end of address
     * space in journal entries, and the end of address space seems to 
     * be in part a function of the file driver.  
     *
     * Thus, if we want to use the core file driver when available, we 
     * will either have to remove the end of address space from the 
     * journal entries, get the different file drivers to agree on 
     * end of address space, or maintain different sets of architype
     * files for the different file drivers.
     */
    setup_cache_for_journaling(filename, journal_filename, &file_id,
                               &file_ptr, &cache_ptr, human_readable, 
                               use_aio, FALSE);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);
 

    /******************************************/
    /* Run a small, fairly simple stress test */
    /******************************************/

    trans_num = 0;

    jrnl_row_major_scan_forward(/* file_ptr               */ file_ptr,
                                 /* max_index              */ max_index,
                                 /* lag                    */ lag,
                                 /* verbose                */ verbose,
                                 /* reset_stats            */ TRUE,
                                 /* display_stats          */ display_stats,
                                 /* display_detailed_stats */ FALSE,
                                 /* do_inserts             */ TRUE,
                                 /* dirty_inserts          */ dirty_inserts,
                                 /* do_moves             */ TRUE,
                                 /* move_to_main_addr    */ FALSE,
                                 /* do_destroys            */ TRUE,
                                 /* do_mult_ro_protects    */ TRUE,
                                 /* dirty_destroys         */ dirty_destroys,
                                 /* dirty_unprotects       */ dirty_unprotects,
                                 /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[0]);
    }
    
    if ( file_exists(testfiles[0]) ) {

        verify_journal_contents(journal_filename, testfiles[0], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    trans_num = 0;

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_row_major_scan_backward(/* file_ptr               */ file_ptr,
                                  /* max_index              */ max_index,
                                  /* lag                    */ lag,
                                  /* verbose                */ verbose,
                                  /* reset_stats            */ TRUE,
                                  /* display_stats          */ display_stats,
                                  /* display_detailed_stats */ FALSE,
                                  /* do_inserts             */ FALSE,
                                  /* dirty_inserts          */ dirty_inserts,
                                  /* do_moves             */ TRUE,
                                  /* move_to_main_addr    */ TRUE,
                                  /* do_destroys            */ FALSE,
                                  /* do_mult_ro_protects    */ TRUE,
                                  /* dirty_destroys         */ dirty_destroys,
                                  /* dirty_unprotects       */ dirty_unprotects,
                                  /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[1]);
    }
    
    if ( file_exists(testfiles[1]) ) {

        verify_journal_contents(journal_filename, testfiles[1], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    trans_num = 0;

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_row_major_scan_forward(/* file_ptr               */ file_ptr,
                                 /* max_index              */ max_index,
                                 /* lag                    */ lag,
                                 /* verbose                */ verbose,
                                 /* reset_stats            */ TRUE,
                                 /* display_stats          */ display_stats,
                                 /* display_detailed_stats */ FALSE,
                                 /* do_inserts             */ TRUE,
                                 /* dirty_inserts          */ dirty_inserts,
                                 /* do_moves             */ TRUE,
                                 /* move_to_main_addr    */ FALSE,
                                 /* do_destroys            */ FALSE,
                                 /* do_mult_ro_protects    */ TRUE,
                                 /* dirty_destroys         */ dirty_destroys,
                                 /* dirty_unprotects       */ dirty_unprotects,
                                 /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[2]);
    }
    
    if ( file_exists(testfiles[2]) ) {

        verify_journal_contents(journal_filename, testfiles[2], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    trans_num = 0;

    verify_journal_empty(journal_filename);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_col_major_scan_forward(/* file_ptr               */ file_ptr,
                                 /* max_index              */ max_index,
                                 /* lag                    */ lag,
                                 /* verbose                */ verbose,
                                 /* reset_stats            */ TRUE,
                                 /* display_stats          */ display_stats,
                                 /* display_detailed_stats */ TRUE,
                                 /* do_inserts             */ TRUE,
                                 /* dirty_inserts          */ dirty_inserts,
                                 /* dirty_unprotects       */ dirty_unprotects,
                                 /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[3]);
    }
    
    if ( file_exists(testfiles[3]) ) {

        verify_journal_contents(journal_filename, testfiles[3], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    flush_cache(file_ptr, FALSE, FALSE, FALSE); /* resets transaction number */

    verify_journal_empty(journal_filename);

    trans_num = 0;

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    jrnl_col_major_scan_backward(/* file_ptr               */ file_ptr,
                                  /* max_index              */ max_index,
                                  /* lag                    */ lag,
                                  /* verbose                */ verbose,
                                  /* reset_stats            */ TRUE,
                                  /* display_stats          */ display_stats,
                                  /* display_detailed_stats */ TRUE,
                                  /* do_inserts             */ TRUE,
                                  /* dirty_inserts          */ dirty_inserts,
                                  /* dirty_unprotects       */ dirty_unprotects,
                                  /* trans_num              */ trans_num);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    flush_journal(cache_ptr);

    if ( update_architypes ) {

        copy_file(journal_filename, testfiles[4]);
    }
    
    if ( file_exists(testfiles[4]) ) {

        verify_journal_contents(journal_filename, testfiles[4], human_readable);

    } else {

    	testfile_missing = TRUE;
    }

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    /****************************************************/
    /* Close and discard the file and the journal file. */
    /****************************************************/

    takedown_cache_after_journaling(file_id, filename, journal_filename, FALSE);

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    verify_clean();
    verify_unprotected();

    if ( show_progress )
        HDfprintf(stdout, "%s:%d cp = %d.\n", fcn_name, pass, cp++);

    if ( pass ) { 
	    
        PASSED(); 

	if ( testfile_missing ) {

	    puts("	WARNING: One or more missing test files."); 
	    fflush(stdout);
        }
    } else { 
	    
        H5_FAILED(); 
    }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);
    }

    return;

} /* mdj_smoke_check_02() */


/*-------------------------------------------------------------------------
 * Function:	mdj_api_example_test
 *
 * Purpose:	Verify that the example code for using the metadata 
 * 		journaling works as expected
 *
 * 		This example demonstrates enabling journaling at file 
 * 		creation time, and enabling journaling on an open file.  It 
 * 		also demonstrates disabling journaling both manualy during a
 * 		computation and automatically at file close.  Finally,
 * 		it demonstrates the use of H5Fflush() to keep the journal
 * 		file from becoming too long.
 *
 * 		We begin by creating an hdf5 file with journaling enabled.
 *
 * 		The inital calls to TESTING(), SKIPPED(), h5_fixname(),
 * 		HDremove(), the initialization of pass, and the like are all 
 * 		part of the HDF5 test framework, and may be largely ignored.
 * 		In your application, the only point here is that you will
 * 		have to set up the paths to your data file and journal 
 * 		file.  Further, the journal file must not exist before
 *              the hdf5 file is opened, as hdf5 will refuse to overwrite
 *              a journal file.
 *
 *              With these preliminaries dealt with, we allocate a 
 *              file access property list (FAPL).  Journaling uses some 
 *              recent extensions to the superblock, so the first step
 *              is to set the library version to latest via a call to 
 *              H5Pset_libver_bounds().
 *
 *              Next, we must set up the journaling property.  We could 
 *              do this in several ways, but in this example we will do 
 *              this by using H5Pget_jnl_config() to get the default 
 *              journaling configuration, modifing it, and then using 
 *		H5Pset_jnl_config() to replace the default with our 
 *		configuration.  See the comments in the code for the 
 *		particulars -- note that we must set the version field of
 *		the H5AC_jnl_config_t before we call H5Pget_jnl_config().
 *
 *		After setting up the FAPL, we create the file as usual.
 *		Journaling will be enabled with configuration as specified.
 *
 *		With file created and journaling running we then go off 
 *		and do what we want -- in this example we set up a selection
 *		of chunked data sets.  Note that these data sets (and our 
 *		access pattern) are chosen to maximize the amount of dirty
 *		metadata generated.  In your application, you will want to 
 *		do just the opposite if possible.
 *
 *		After the data sets are created, we then shut down journaling
 *		and then re-enable it via the H5Fget_jnl_config() and 
 *		H5Fset_jnl_config() calls.  Note that when we re-enable 
 *		journaling via the H5Fset_jnl_config() call, we don't need
 *		to set all the fields in the H5AC_jnl_config_t, since we 
 *		are re-using the configuration we obtained via the 
 *		H5Fget_jnl_config() call.  If we had opened the file without
 *		journaling, and then wanted to enable journaling, we would 
 *		have had to set up the fields of the H5AC_jnl_config_t in 
 *		much the same way we did earlier in the example.  We would 
 *		also have had to create the file initially with the latest 
 *		format (using H5Pset_libver_bounds()).
 *
 *		Having re-enabled journaling, we then proceed to write to 
 *		our data sets.  Again, please not that our write strategy
 *		(round robin and small chunks) is designed to maximize
 *		dirty metadata generation and load on the metadata cache.
 *		In your application, you should try to do just the opposite
 *		if possible.  
 *
 *		However, since we are maximizing dirty metadata generation,
 *		the journal file will grow quickly.  This can be a problem,
 *		so from time to time we force truncation of the journal file
 *		via a call to H5Fflush().  This call flushes the hdf5 file,
 *		and then truncates the journal file, as the contents of the
 *		journal becomes irrelvant after the metadata journal is 
 *		flushed.
 *
 * 		After writing data to our data sets, we then to a number of 
 * 		reads.  We could turn off journaling here, as we are not 
 * 		modifying the file.  But since we are not generating any 
 * 		dirty metadata, we aren't generating any journal entries
 * 		either -- so it really doesn't matter.
 *
 * 		Finally, we close the hdf5 file.  Since journaling is enabled,
 * 		the call to H5Fclose() will flush the journal, flush the 
 * 		metadata cache, truncate the journal, mark the file as not 
 * 		having journaling in progress, and then delete the journal 
 * 		file as part of the close.
 *
 * Return:	void
 *
 * Programmer:	John Mainzer
 *              12/14/08
 *
 * Modifications:
 *
 * 		Modified the function to used either the human readable 
 *		or the binary journal file format as directed via the 
 *		new human_readable parameter.
 *						JRM -- 5/13/09
 *
 *		Modified the function to use either AIO or SIO as directed
 *		via the new use_aio parameter.
 *
 *						JRM -- 1/26/09
 *		
 *		Added num_bufs and buf_size parameters and associated
 *		code.
 *						JRM -- 3/2/10
 *
 *-------------------------------------------------------------------------
 */

#define CHUNK_SIZE              10
#define DSET_SIZE               (40 * CHUNK_SIZE)
#define NUM_DSETS               6
#define NUM_RANDOM_ACCESSES     200000

static void
mdj_api_example_test(hbool_t human_readable,
                     hbool_t use_aio,
                     int num_bufs,
                     size_t buf_size)
{
    const char * fcn_name = "mdj_api_example_test()";
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    hbool_t valid_chunk;
    hbool_t report_progress = FALSE;
    hid_t fapl_id = -1;
    hid_t file_id = -1;
    hid_t dataspace_id = -1;
    hid_t filespace_ids[NUM_DSETS];
    hid_t memspace_id = -1;
    hid_t dataset_ids[NUM_DSETS];
    hid_t properties;
    char dset_name[64];
    int i, j, k, l, m, n;
    int progress_counter;
    herr_t status;
    hsize_t dims[2];
    hsize_t a_size[2];
    hsize_t offset[2];
    hsize_t chunk_size[2];
    int data_chunk[CHUNK_SIZE][CHUNK_SIZE];
    H5AC_jnl_config_t jnl_config_0;
    H5AC_jnl_config_t jnl_config_1;


    if ( human_readable ) {

        if ( use_aio ) {

            TESTING("aio mdj example code -- human readable journal file");

        } else {

            TESTING("sio mdj example code -- human readable journal file");
        }
    } else {

        if ( use_aio ) {

            TESTING("aio mdj example code -- binary journal file");

        } else {

            TESTING("sio mdj example code -- binary journal file");
        }
    }

    if ( skip_long_tests > 0 ) {

        SKIPPED();

        HDfprintf(stdout, "     Long tests disabled.\n");

        return;
    }

    pass = TRUE;

    /* Open a file with journaling enabled. */


    /* setup the hdf5 file name */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"\nSetting up file name ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
            == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed.\n";
        }
    }


    /* setup the journal file name */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"\nSetting up journal file name ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename,
                        sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
        else if ( HDstrlen(journal_filename) >=
                  H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }


    /* clean out any existing journal file -- must do this as 
     * HDF5 will refuse to overwrite an existing journal file.
     */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"\nRemoving any existing journal file ... ");
        HDfflush(stdout);
    }

    HDremove(journal_filename);


    /* create a file access propertly list. */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"\nCreating a FAPL ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        fapl_id = H5Pcreate(H5P_FILE_ACCESS);

        if ( fapl_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pcreate() failed.\n";
        }
    }


    /* need latest version of file format to use journaling */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"\nCalling H5Pset_libver_bounds() on FAPL ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( H5Pset_libver_bounds(fapl_id, H5F_LIBVER_LATEST,
                                  H5F_LIBVER_LATEST) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_libver_bounds() failed.\n";
        }
    }


    /* Get the current FAPL journaling configuration.  This should be 
     * the default, and we could just write a predifined journal configuration
     * structure to the FAPL directly, but doing it this way shows off the
     * H5Pget_jnl_config() call, and is less suceptible to API definition
     * changes.
     */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout, "\nCalling H5Pget_jnl_config() on FAPL ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

	jnl_config_0.version = H5AC__CURR_JNL_CONFIG_VER;

	status = H5Pget_jnl_config(fapl_id, &jnl_config_0);

	if ( status < 0 ) {

	    pass = FALSE;
	    failure_mssg = "H5Pset_mdc_config() failed.\n";
	}
    }


    /* Modify the current FAPL journaling configuration to enable 
     * journaling as desired, and then write the revised configuration
     * back to the FAPL.
     */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,
	         "\nRevising config & calling H5Pset_jnl_config() on FAPL ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        jnl_config_0.enable_journaling = TRUE;

        HDstrcpy(jnl_config_0.journal_file_path, journal_filename);

        /* jnl_config_0.journal_recovered should always be FALSE unless
         * you are writing a new journal recovery tool, and need to 
         * tell the library that you have recovered the journal and 
         * that the file is now readable.  As this field is set to 
         * FALSE by default, we don't touch it here.
         */

        /* the journal buffer size should  be some multiple of the block 
         * size of the underlying file system.  
         */
        jnl_config_0.jbrb_buf_size = buf_size;

        /* the number of journal buffers should be either 1 or 2 when 
         * synchronous I/O is used for journal writes.  If AIO is used,
         * the number should be large enough that the write of a buffer 
         * will usually be complete by the time that buffer is needed
         * again.
         */
        jnl_config_0.jbrb_num_bufs = num_bufs;

        /* select aio or not as directed. */
        jnl_config_0.jbrb_use_aio = use_aio;

        /* set human readable as specified in the human_readable parameter
         * to this function.  If human_readable is FALSE, we will use 
         * the binary journal file format which should reduce the size
         * of the journal file by about two thirds, and also reduce the 
         * overhead involved in formating journal entries for writing
         * to the journal file.
         */
	jnl_config_0.jbrb_human_readable = human_readable;
        
        status = H5Pset_jnl_config(fapl_id, &jnl_config_0);

        if ( status < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_mdc_config() failed.\n";
        }
    }


    /* Now open the file using the FAPL we have created. */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,
	         "\nCreating the HDF5 file using the new FAPL ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fcreate() failed.\n";

        }
    }


    /* create the datasets */

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"\nCreating datasets ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

        i = 0;

        while ( ( pass ) && ( i < NUM_DSETS ) )
        {
            /* create a dataspace for the chunked dataset */
            dims[0] = DSET_SIZE;
            dims[1] = DSET_SIZE;
            dataspace_id = H5Screate_simple(2, dims, NULL);

            if ( dataspace_id < 0 ) {

                pass = FALSE;
                failure_mssg = "H5Screate_simple() failed.";
            }

            /* set the dataset creation plist to specify that the raw data is
             * to be partioned into 10X10 element chunks.
             */

            if ( pass ) {

                chunk_size[0] = CHUNK_SIZE;
                chunk_size[1] = CHUNK_SIZE;
                properties = H5Pcreate(H5P_DATASET_CREATE);

                if ( properties < 0 ) {

                    pass = FALSE;
                    failure_mssg = "H5Pcreate() failed.";
                }
            }

            if ( pass ) {

                if ( H5Pset_chunk(properties, 2, chunk_size) < 0 ) {

                    pass = FALSE;
                    failure_mssg = "H5Pset_chunk() failed.";
                }
            }

            /* create the dataset */
            if ( pass ) {

                sprintf(dset_name, "/dset%03d", i);
                dataset_ids[i] = H5Dcreate2(file_id, dset_name, H5T_STD_I32BE,
				            dataspace_id, H5P_DEFAULT, 
					    properties, H5P_DEFAULT);

                if ( dataset_ids[i] < 0 ) {

                    pass = FALSE;
                    failure_mssg = "H5Dcreate() failed.";
                }
            }

            /* get the file space ID */
            if ( pass ) {

                filespace_ids[i] = H5Dget_space(dataset_ids[i]);

                if ( filespace_ids[i] < 0 ) {

                    pass = FALSE;
                    failure_mssg = "H5Dget_space() failed.";
                }
            }

            i++;
        }
    }


    /* just for purposes of demonstration, turn off journaling, and 
     * then turn it back on again.  Note that this will force a 
     * flush of the file, and all metadata with it.  Turning off 
     * journaling will also cause us to close and discard the 
     * journal file after all metadata is on disk.
     */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,
	         "\nTurning off journaling ... ");
        HDfflush(stdout);
    }

    if ( pass ) {

	jnl_config_1.version = H5AC__CURR_JNL_CONFIG_VER;

	status = H5Fget_jnl_config(file_id, &jnl_config_1);

	if ( status < 0 ) {

	    pass = FALSE;
	    failure_mssg = "H5Fget_mdc_config() failed.\n";
	}
    }

    if ( pass ) {

        jnl_config_1.enable_journaling = FALSE;

        status = H5Fset_jnl_config(file_id, &jnl_config_1);

        if ( status < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_mdc_config() failed.\n";
        }
    }


    /* Note that here we simply set jnl_config_1.enable_journaling to
     * TRUE, and pass it back to the HDF5 library via the 
     * H5Fset_jnl_config() call.  
     *
     * We can do this because jnl_config_1 reflected the current 
     * journaling configuration when we got it from the library
     * via the H5Fget_jnl_config() call, and H5Fset_mdc_config()
     * doesn't change the values of any fields.
     */
    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,
	         "\nTurning journaling back on ... ");
        HDfflush(stdout);
    }
    if ( pass ) {

        jnl_config_1.enable_journaling = TRUE;

        status = H5Fset_jnl_config(file_id, &jnl_config_1);

        if ( status < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_mdc_config() failed.\n";
        }
    }


    /* create the mem space to be used to read and write chunks */
    if ( pass ) {

        dims[0] = CHUNK_SIZE;
        dims[1] = CHUNK_SIZE;
        memspace_id = H5Screate_simple(2, dims, NULL);

        if ( memspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Screate_simple() failed.";
        }
    }

    /* select in memory hyperslab */
    if ( pass ) {

        offset[0] = 0;  /*offset of hyperslab in memory*/
        offset[1] = 0;
        a_size[0] = CHUNK_SIZE;  /*size of hyperslab*/
        a_size[1] = CHUNK_SIZE;
        status = H5Sselect_hyperslab(memspace_id, H5S_SELECT_SET, offset, NULL,
                                     a_size, NULL);

        if ( status < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Sselect_hyperslab() failed.";
        }
    }

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"Done.\n");
        HDfflush(stdout);
    }

    /* initialize all datasets on a round robin basis */
    i = 0;
    progress_counter = 0;

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout, "Initializing datasets ");
        HDfflush(stdout);
    }

    while ( ( pass ) && ( i < DSET_SIZE ) )
    {
        j = 0;
        while ( ( pass ) && ( j < DSET_SIZE ) )
        {
            m = 0;
            while ( ( pass ) && ( m < NUM_DSETS ) )
            {
                /* initialize the slab */
                for ( k = 0; k < CHUNK_SIZE; k++ )
                {
                    for ( l = 0; l < CHUNK_SIZE; l++ )
                    {
                        data_chunk[k][l] = (DSET_SIZE * DSET_SIZE * m) +
                                           (DSET_SIZE * (i + k)) + j + l;
                    }
                }

                /* select on disk hyperslab */
                offset[0] = (hsize_t)i; /*offset of hyperslab in file*/
                offset[1] = (hsize_t)j;
                a_size[0] = CHUNK_SIZE;   /*size of hyperslab*/
                a_size[1] = CHUNK_SIZE;
                status = H5Sselect_hyperslab(filespace_ids[m], H5S_SELECT_SET,
                                         offset, NULL, a_size, NULL);

                if ( status < 0 ) {

                    pass = FALSE;
                    failure_mssg = "disk H5Sselect_hyperslab() failed.";
                }

                /* write the chunk to file */
                status = H5Dwrite(dataset_ids[m], H5T_NATIVE_INT, memspace_id,
                                  filespace_ids[m], H5P_DEFAULT, data_chunk);

                if ( status < 0 ) {

                    pass = FALSE;
                    failure_mssg = "H5Dwrite() failed.";
                }
                m++;
            }
            j += CHUNK_SIZE;
        }

        i += CHUNK_SIZE;

        if ( ( pass ) && ( report_progress ) ) {

	    progress_counter += CHUNK_SIZE;

	    if ( progress_counter >= DSET_SIZE / 20 ) {

	        progress_counter = 0;
	        HDfprintf(stdout, ".");
                HDfflush(stdout);
	    }
	}

	/* We are generating a lot of dirty metadata here, all of which 
	 * will wind up in the journal file.  To keep the journal file
	 * from getting too big (and to make sure the raw data is on 
	 * disk, we should do an occasional flush of the HDF5 file.
	 *
	 * This will force all metadata to disk, and cause the journal
	 * file to be truncated.
	 *
	 * On the other hand, it will impose a significant file I/O 
	 * overhead, and slow us down. (try it both ways).
	 */
#if 0
	status = H5Fflush(file_id, H5F_SCOPE_GLOBAL);

        if ( status < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fflush() failed.";
        }
#endif
    }

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout," Done.\n"); /* initializing data sets */
        HDfflush(stdout);
    }


    /* do random reads on all datasets */

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout, "Doing random reads on all datasets ");
        HDfflush(stdout);
    }

    n = 0;
    progress_counter = 0;
    while ( ( pass ) && ( n < NUM_RANDOM_ACCESSES ) )
    {
        m = rand() % NUM_DSETS;
        i = (rand() % (DSET_SIZE / CHUNK_SIZE)) * CHUNK_SIZE;
        j = (rand() % (DSET_SIZE / CHUNK_SIZE)) * CHUNK_SIZE;

        /* select on disk hyperslab */
        offset[0] = (hsize_t)i; /*offset of hyperslab in file*/
        offset[1] = (hsize_t)j;
        a_size[0] = CHUNK_SIZE;   /*size of hyperslab*/
        a_size[1] = CHUNK_SIZE;
        status = H5Sselect_hyperslab(filespace_ids[m], H5S_SELECT_SET,
                                     offset, NULL, a_size, NULL);

        if ( status < 0 ) {

           pass = FALSE;
           failure_mssg = "disk hyperslab create failed.";
        }

        /* read the chunk from file */
        if ( pass ) {

            status = H5Dread(dataset_ids[m], H5T_NATIVE_INT, memspace_id,
                             filespace_ids[m], H5P_DEFAULT, data_chunk);

            if ( status < 0 ) {

               pass = FALSE;
               failure_mssg = "disk hyperslab create failed.";
            }
        }

        /* validate the slab */
        if ( pass ) {

            valid_chunk = TRUE;
            for ( k = 0; k < CHUNK_SIZE; k++ )
            {
                for ( l = 0; l < CHUNK_SIZE; l++ )
                {
                     if ( data_chunk[k][l]
                          !=
                          ((DSET_SIZE * DSET_SIZE * m) +
                           (DSET_SIZE * (i + k)) + j + l) ) {

                         valid_chunk = FALSE;
#if 0 /* this will be useful from time to time -- lets keep it*/
                         HDfprintf(stdout,
                                   "data_chunk[%0d][%0d] = %0d, expect %0d.\n",
                                   k, l, data_chunk[k][l],
                                   ((DSET_SIZE * DSET_SIZE * m) +
                                    (DSET_SIZE * (i + k)) + j + l));
                         HDfprintf(stdout,
                                   "m = %d, i = %d, j = %d, k = %d, l = %d\n",
                                   m, i, j, k, l);
#endif
                    }
                }
            }

            if ( ! valid_chunk ) {
#if 1
                pass = FALSE;
                failure_mssg = "slab validation failed.";
#else /* as above */
                fprintf(stdout, "Chunk (%0d, %0d) in /dset%03d is invalid.\n",
                        i, j, m);
#endif
            }
        }

        n++;

        if ( ( pass ) && ( report_progress ) ) {

	    progress_counter++;

	    if ( progress_counter >= NUM_RANDOM_ACCESSES / 20 ) {

	        progress_counter = 0;
	        HDfprintf(stdout, ".");
                HDfflush(stdout);
	    }
	}
    }

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout, " Done.\n"); /* random reads on all data sets */
        HDfflush(stdout);
    }


    /* close the file spaces we are done with */
    i = 1;
    while ( ( pass ) && ( i < NUM_DSETS ) )
    {
        if ( H5Sclose(filespace_ids[i]) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Sclose() failed.";
        }
        i++;
    }


    /* close the datasets we are done with */
    i = 1;
    while ( ( pass ) && ( i < NUM_DSETS ) )
    {
        if ( H5Dclose(dataset_ids[i]) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Dclose() failed.";
        }
        i++;
    }


    /* do random reads on data set 0 only */

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout, "Doing random reads on dataset 0 ");
        HDfflush(stdout);
    }

    m = 0;
    n = 0;
    progress_counter = 0;
    while ( ( pass ) && ( n < NUM_RANDOM_ACCESSES ) )
    {
        i = (rand() % (DSET_SIZE / CHUNK_SIZE)) * CHUNK_SIZE;
        j = (rand() % (DSET_SIZE / CHUNK_SIZE)) * CHUNK_SIZE;

        /* select on disk hyperslab */
        offset[0] = (hsize_t)i; /*offset of hyperslab in file*/
        offset[1] = (hsize_t)j;
        a_size[0] = CHUNK_SIZE;   /*size of hyperslab*/
        a_size[1] = CHUNK_SIZE;
        status = H5Sselect_hyperslab(filespace_ids[m], H5S_SELECT_SET,
                                     offset, NULL, a_size, NULL);

        if ( status < 0 ) {

           pass = FALSE;
           failure_mssg = "disk hyperslab create failed.";
        }

        /* read the chunk from file */
        if ( pass ) {

            status = H5Dread(dataset_ids[m], H5T_NATIVE_INT, memspace_id,
                             filespace_ids[m], H5P_DEFAULT, data_chunk);

            if ( status < 0 ) {

               pass = FALSE;
               failure_mssg = "disk hyperslab create failed.";
            }
        }

        /* validate the slab */
        if ( pass ) {

            valid_chunk = TRUE;
            for ( k = 0; k < CHUNK_SIZE; k++ )
            {
               for ( l = 0; l < CHUNK_SIZE; l++ )
               {
                   if ( data_chunk[k][l]
                        !=
                        ((DSET_SIZE * DSET_SIZE * m) +
                         (DSET_SIZE * (i + k)) + j + l) ) {

                       valid_chunk = FALSE;
                  }
#if 0 /* this will be useful from time to time -- lets keep it */
                  HDfprintf(stdout, "data_chunk[%0d][%0d] = %0d, expect %0d.\n",
                            k, l, data_chunk[k][l],
                            ((DSET_SIZE * DSET_SIZE * m) +
                             (DSET_SIZE * (i + k)) + j + l));
#endif
                }
            }

            if ( ! valid_chunk ) {

                pass = FALSE;
                failure_mssg = "slab validation failed.";
#if 0 /* as above */
                fprintf(stdout, "Chunk (%0d, %0d) in /dset%03d is invalid.\n",
                        i, j, m);
#endif
            }
        }

        n++;

        if ( ( pass ) && ( report_progress ) ) {

	    progress_counter++;

	    if ( progress_counter >= NUM_RANDOM_ACCESSES / 20 ) {

	        progress_counter = 0;
	        HDfprintf(stdout, ".");
                HDfflush(stdout);
	    }
	}
    }

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout, " Done.\n"); /* random reads data set 0 */
        HDfflush(stdout);
    }


    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"Shutting down ... ");
        HDfflush(stdout);
    }


    /* close file space 0 */
    if ( pass ) {

        if ( H5Sclose(filespace_ids[0]) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Sclose(filespace_ids[0]) failed.";
        }
    }

    /* close the data space */
    if ( pass ) {

        if ( H5Sclose(dataspace_id) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Sclose(dataspace) failed.";
        }
    }

    /* close the mem space */
    if ( pass ) {

        if ( H5Sclose(memspace_id) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Sclose(memspace_id) failed.";
        }
    }

    /* close dataset 0 */
    if ( pass ) {

        if ( H5Dclose(dataset_ids[0]) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Dclose(dataset_ids[0]) failed.";
        }
    }

    /* close the file and delete it */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0  ) {

            pass = FALSE;
	    failure_mssg = "H5Fclose() failed.\n";

        }
        else if ( HDremove(filename) < 0 ) {

            pass = FALSE;
	    failure_mssg = "HDremove() failed.\n";
        }
    }

    if ( ( pass ) && ( report_progress ) ) {

	HDfprintf(stdout,"Done.\n"); /* shutting down */
        HDfflush(stdout);
    }


    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);
    }

    return;

} /* mdj_api_example_test() */


/*** super block extension related test code ***/

/*-------------------------------------------------------------------------
 * Function:    check_superblock_extensions()
 *
 * Purpose:     Verify that the super block extensions for tracking 
 *              journaling status operate as they should.
 *
 *              Note that this test code will have to be re-worked
 *              once journaling is fully implemented.
 *
 * Return:      void
 *
 * Programmer:  John Mainzer
 *              2/26/08
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

extern hbool_t H5C__check_for_journaling;

static void
check_superblock_extensions(void)
{
    const char * fcn_name = "check_superblock_extensions()";
    char filename[512];
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0; 
    hid_t fapl_id = -1;
    hid_t file_id = -1;
    hid_t dataset_id = -1;
    hid_t dataspace_id = -1;
    H5F_t * file_ptr = NULL;
    hsize_t dims[2];


    TESTING("superblock extensions");

    pass = TRUE;

    /* Verify that the journaling superblock extension performs as 
     * expected.  Note that this test will have to be re-written
     * (or possibly subsumed in another test) once the full journaling
     * code is up and running.
     *
     * For now at least, the test proceeds as follows:
     *
     *  1) create a HDF5 file, and verify that journaling is
     *     listed as being off.
     *
     *  2) create a dataset in the file, and then close the file
     * 
     *  3) Open the file again, and verifiy that journaling is still
     *     listed as being off.
     *
     *  4) Write data to the superblock marking the file as currently
     *     being journaled, and close the file again.
     *
     *  5) Open the file a third time, and verify that the superblock
     *     extension indicates that the file is being journaled.  
     *
     *  6) Reset the journaling information to indicate that the file
     *     is not being journaled, and close the file again.
     *
     *  7) Open the file a fourth time, and verify that the superblock
     *     extension indicates that the file is not being journaled.
     *
     *  8) Write data to the superblock, marking the file as being
     *     journaled.  Now write different data to the superbloc, that 
     *     still marks the file as being journaled.  Close the file. 
     *
     *  9) Re-open the file, and verify that the second write in 8 
     *     above took.
     *
     * 10) Write data to the superblock indicating that journaling is
     *     not in progress.  Close the file.
     *
     * 11) Reopen the file, and verify that journaling is not in 
     *     progress.
     *
     * 12) Close the file and delete it.
     */

    /********************************************************/
    /* 1) create a HDF5 file, and verify that journaling is */
    /*    listed as being off.                              */
    /********************************************************/

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[2], H5P_DEFAULT, filename, sizeof(filename))
				            == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* create a file access propertly list */
    if ( pass ) {

        fapl_id = H5Pcreate(H5P_FILE_ACCESS);

        if ( fapl_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pcreate() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* call H5Pset_libver_bounds() on the fapl_id */
    if ( pass ) {

	if ( H5Pset_libver_bounds(fapl_id, H5F_LIBVER_LATEST, 
                                  H5F_LIBVER_LATEST) < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Pset_libver_bounds() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* create the file using fapl_id */
    if ( pass ) {

        file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fcreate() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * verify that journaling is disabled.
     */
    if ( pass ) {

        file_ptr = (H5F_t *)H5I_object_verify(file_id, H5I_FILE);

        if ( file_ptr == NULL ) {

            pass = FALSE;
            failure_mssg = "Can't get file_ptr (1).\n";

        } else if ( file_ptr->shared->mdc_jnl_enabled ) {
	
	    pass = FALSE;
	    failure_mssg = "Journaling enabled on file creation.\n";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /************************************************************/
    /* 2) create a dataset in the file, and then close the file */
    /************************************************************/

    if ( pass ) {

        dims[0] = 4;
        dims[1] = 6;
        dataspace_id = H5Screate_simple(2, dims, NULL);

	if ( dataspace_id < 0 ) {

	    pass = FALSE;
	    failure_mssg = "H5Screate_simple() failed.";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        /* Create the dataset. */
        dataset_id = H5Dcreate2(file_id, "/dset", H5T_STD_I32BE, dataspace_id,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

	if ( dataspace_id < 0 ) {

	    pass = FALSE;
	    failure_mssg = "H5Dcreate2() failed.";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        /* close the data set, the data space, and the file */
	if ( ( H5Dclose(dataset_id) < 0 ) ||
	     ( H5Sclose(dataspace_id) < 0 ) ||
	     ( H5Fclose(file_id) < 0 ) ) {

            pass = FALSE;
	    failure_mssg = "data set, data space, or file close failed.";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /****************************************************************/
    /* 3) Open the file again, and verifiy that journaling is still */
    /*    listed as being off.                                      */
    /****************************************************************/

    /* open the file r/w using the default FAPL */
    if ( pass ) {

        file_id = H5Fopen(filename, H5F_ACC_RDWR, H5P_DEFAULT);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (4).\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * verify that journaling is disabled.
     */
    if ( pass ) {

        file_ptr = (H5F_t *)H5I_object_verify(file_id, H5I_FILE);

        if ( file_ptr == NULL ) {

            pass = FALSE;
            failure_mssg = "Can't get file_ptr (2).\n";

        } else if ( file_ptr->shared->mdc_jnl_enabled ) {
	
	    pass = FALSE;
	    failure_mssg = "Journaling enabled on file open (1).\n";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /*****************************************************************/
    /* 4) Write data to the superblock marking the file as currently */
    /*    being journaled, and close the file again.                 */
    /*****************************************************************/

    /* At present, we just write the super block regardless if the 
     * file is opened read/write.  This is ugly, but that is how it
     * is for now.  Thus just go in and modify the journaling fields
     * of the super block to taste.
     */

    if ( pass ) {

        file_ptr->shared->mdc_jnl_enabled       = TRUE;
        file_ptr->shared->mdc_jnl_magic         = 123;
        file_ptr->shared->mdc_jnl_file_name_len = HDstrlen("abc");
        HDstrncpy(file_ptr->shared->mdc_jnl_file_name,
                  "abc",
                  file_ptr->shared->mdc_jnl_file_name_len + 1);

        if ( verbose ) {

            HDfprintf(stdout, "f->shared->mdc_jnl_enabled       = %d\n",
                      (int)(file_ptr->shared->mdc_jnl_enabled));
            HDfprintf(stdout, "f->shared->mdc_jnl_magic         = %d\n",
                      (int)(file_ptr->shared->mdc_jnl_magic));
            HDfprintf(stdout, "f->shared->mdc_jnl_file_name_len = %d\n",
                      (int)(file_ptr->shared->mdc_jnl_file_name_len));
            HDfprintf(stdout, "f->shared->mdc_jnl_file_name     = \"%s\"\n",
                      file_ptr->shared->mdc_jnl_file_name);
        }

	if ( H5F_super_write_mdj_msg(file_ptr, -1) < 0 ) {

            pass = FALSE;
	    failure_mssg = "H5F_super_write_mdj_msg failed (1).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* close the file again. */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

            pass = FALSE;
	    failure_mssg = "file close failed (1).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);


    /*****************************************************************/
    /* 5) Open the file a third time, and verify that the superblock */
    /*    extension indicates that the file is being journaled.      */
    /*****************************************************************/

    /* open the file r/w using the default FAPL -- turn off journaling
     * in progress check during the open.
     * */
    if ( pass ) {

	H5C__check_for_journaling = FALSE;
        file_id = H5Fopen(filename, H5F_ACC_RDWR, H5P_DEFAULT);
	H5C__check_for_journaling = TRUE;

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (5).\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * verify that journaling is enabled.
     */
    if ( pass ) {

        file_ptr = (H5F_t *)H5I_object_verify(file_id, H5I_FILE);

        if ( file_ptr == NULL ) {

            pass = FALSE;
            failure_mssg = "Can't get file_ptr (3).\n";

        } else if ( ! file_ptr->shared->mdc_jnl_enabled ) {
	
	    pass = FALSE;
	    failure_mssg = "Journaling disabled on file open (1).\n";

	} else if ( file_ptr->shared->mdc_jnl_magic != 123 ) {
	
	    pass = FALSE;
	    HDfprintf(stdout, "%s: mdc_jnl_magic = %d (%d).\n",
		      fcn_name, (int)(file_ptr->shared->mdc_jnl_magic),
		      123);
	    failure_mssg = "unexpected mdc_jnl_magic(1).\n";

	} else if ( file_ptr->shared->mdc_jnl_file_name_len != 
		    (size_t)HDstrlen("abc") ) {
	
	    pass = FALSE;
	    failure_mssg = "unexpected mdc_jnl_file_name_len (1).\n";

	} else if ( HDstrcmp(file_ptr->shared->mdc_jnl_file_name, "abc") != 0 ) {
	
	    pass = FALSE;
	    failure_mssg = "unexpected mdc_jnl_file_name (1).\n";

        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /*****************************************************************/
    /* 6) Reset the journaling information to indicate that the file */
    /*    is not being journaled, and close the file again.          */
    /*****************************************************************/

    if ( pass ) {

	file_ptr->shared->mdc_jnl_enabled = FALSE;

	if ( H5F_super_write_mdj_msg(file_ptr, -1) < 0 ) {

            pass = FALSE;
	    failure_mssg = "H5F_super_write_mdj_msg failed (2).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* close the file again. */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

            pass = FALSE;
	    failure_mssg = "file close failed (2).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);
    

    /******************************************************************/
    /* 7) Open the file a fourth time, and verify that the superblock */
    /*    extension indicates that the file is not being journaled.   */
    /*******************************************************************/

    /* open the file r/w using the default FAPL */
    if ( pass ) {

        file_id = H5Fopen(filename, H5F_ACC_RDWR, H5P_DEFAULT);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (6).\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * verify that journaling is disabled.
     */
    if ( pass ) {

        file_ptr = (H5F_t *)H5I_object_verify(file_id, H5I_FILE);

        if ( file_ptr == NULL ) {

            pass = FALSE;
            failure_mssg = "Can't get file_ptr (4).\n";

        } else if ( file_ptr->shared->mdc_jnl_enabled ) {
	
	    pass = FALSE;
	    failure_mssg = "Journaling enabled on file open (2).\n";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);


    /*******************************************************************/
    /*  8) Write data to the superblock, marking the file as being     */
    /*     journaled.  Now write different data to the superbloc, that */
    /*     still marks the file as being journaled.  Close the file.   */
    /*******************************************************************/

    if ( pass ) {

        file_ptr->shared->mdc_jnl_enabled       = TRUE;
        file_ptr->shared->mdc_jnl_magic         = 456;
        file_ptr->shared->mdc_jnl_file_name_len = HDstrlen("qrst");
        HDstrncpy(file_ptr->shared->mdc_jnl_file_name,
                  "qrst",
                  file_ptr->shared->mdc_jnl_file_name_len + 1);

	if ( H5F_super_write_mdj_msg(file_ptr, -1) < 0 ) {

            pass = FALSE;
	    failure_mssg = "H5F_super_write_mdj_msg failed (3).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) {

        file_ptr->shared->mdc_jnl_enabled       = TRUE;
        file_ptr->shared->mdc_jnl_magic         = 789;
        file_ptr->shared->mdc_jnl_file_name_len = HDstrlen("z");
        HDstrncpy(file_ptr->shared->mdc_jnl_file_name,
                  "z",
                  file_ptr->shared->mdc_jnl_file_name_len + 1);

	if ( H5F_super_write_mdj_msg(file_ptr, -1) < 0 ) {

            pass = FALSE;
	    failure_mssg = "H5F_super_write_mdj_msg failed (4).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* close the file again. */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

            pass = FALSE;
	    failure_mssg = "file close failed (3).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    
    /***************************************************************/
    /*  9) Re-open the file, and verify that the second write in 8 */
    /*     above took.                                             */
    /***************************************************************/

    /* open the file r/w using the default FAPL -- turn off journaling
     * in progress check during the open.
     */
    if ( pass ) {

	H5C__check_for_journaling = FALSE;
        file_id = H5Fopen(filename, H5F_ACC_RDWR, H5P_DEFAULT);
	H5C__check_for_journaling = TRUE;

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (7).\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * verify that journaling is enabled.
     */
    if ( pass ) {

        file_ptr = (H5F_t *)H5I_object_verify(file_id, H5I_FILE);

        if ( file_ptr == NULL ) {

            pass = FALSE;
            failure_mssg = "Can't get file_ptr (5).\n";

        } else if ( ! file_ptr->shared->mdc_jnl_enabled ) {
	
	    pass = FALSE;
	    failure_mssg = "Journaling disabled on file open (2).\n";

	} else if ( file_ptr->shared->mdc_jnl_magic != 789 ) {
	
	    pass = FALSE;
	    HDfprintf(stdout, "%s: mdc_jnl_magic = %d (%d).\n",
		      fcn_name, (int)(file_ptr->shared->mdc_jnl_magic),
		      789);
	    failure_mssg = "unexpected mdc_jnl_magic(2).\n";

	} else if ( file_ptr->shared->mdc_jnl_file_name_len != 
		    (size_t)HDstrlen("z") ) {
	
	    pass = FALSE;
	    failure_mssg = "unexpected mdc_jnl_file_name_len (2).\n";

	} else if ( HDstrcmp(file_ptr->shared->mdc_jnl_file_name, "z") != 0 ) {
	
	    pass = FALSE;
	    failure_mssg = "unexpected mdc_jnl_file_name (2).\n";

        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);


    /******************************************************************/
    /* 10) Write data to the superblock indicating that journaling is */
    /*     not in progress.  Close the file.                          */
    /******************************************************************/

    if ( pass ) {

	file_ptr->shared->mdc_jnl_enabled = FALSE;

	if ( H5F_super_write_mdj_msg(file_ptr, -1) < 0 ) {

            pass = FALSE;
	    failure_mssg = "H5F_super_write_mdj_msg failed (5).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* close the file again. */
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

            pass = FALSE;
	    failure_mssg = "file close failed (4).";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);


    /*************************************************************/
    /* 11) Reopen the file, and verify that journaling is not in */
    /*     progress.                                             */
    /*************************************************************/

    /* open the file r/w using the default FAPL */
    if ( pass ) {

        file_id = H5Fopen(filename, H5F_ACC_RDWR, H5P_DEFAULT);

        if ( file_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fopen() failed (8).\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    /* get a pointer to the files internal data structure and then 
     * verify that journaling is disabled.
     */
    if ( pass ) {

        file_ptr = (H5F_t *)H5I_object_verify(file_id, H5I_FILE);

        if ( file_ptr == NULL ) {

            pass = FALSE;
            failure_mssg = "Can't get file_ptr (6).\n";

        } else if ( file_ptr->shared->mdc_jnl_enabled ) {
	
	    pass = FALSE;
	    failure_mssg = "Journaling enabled on file open (3).\n";
	}
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);


    /*************************************/
    /* 12) Close the file and delete it. */
    /*************************************/
    
    if ( pass ) {

	if ( H5Fclose(file_id) < 0 ) {

            pass = FALSE;
	    failure_mssg = "file close failed (5).";

        } else if ( HDremove(filename) < 0 ) {

            pass = FALSE;
            failure_mssg = "HDremove() failed.\n";
        }
    }

    if ( show_progress ) HDfprintf(stdout, "%s: cp = %d.\n", fcn_name, cp++);

    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);
    }

} /* check_superblock_extensions() */


/***************************************************************************
 * Function: 	check_mdjsc_callbacks()
 *
 * Purpose:  	Verify that the registration and deregistration of 
 *		metadata journaling status change registration/deregistraion
 *		works correctly.  
 *
 *		Verify that the status change callbacks are called as 
 *		they should be, and that the cache is clean when the 
 *		callback is called.
 *
 *              On failure, set pass to false, and failure_mssg to an
 *              appropriate error string.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              7/2/08
 * 
 **************************************************************************/

static void 
check_mdjsc_callbacks(void)
{
    const char * fcn_name = "check_mdjsc_callbacks():";

    TESTING("metadata journaling status change callbacks");

    verify_mdjsc_callback_registration_deregistration();

    verify_mdjsc_callback_execution();

    verify_mdjsc_callback_error_rejection();

    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);
    }

    return;

} /* check_mdjsc_callbacks() */


/***************************************************************************
 *
 * Function: 	test_mdjsc_callback()
 *
 * Purpose:  	Test callback function used to test the metadata 
 *		journaling status change callback facility.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static H5C_t * callback_test_cache_ptr        = NULL;
static hbool_t callback_test_invalid_cache_ptr = FALSE;
static hbool_t callback_test_null_config_ptr   = FALSE;
static hbool_t callback_test_invalid_config    = FALSE;
static hbool_t callback_test_null_data_ptr     = FALSE;
static hbool_t callback_test_cache_is_dirty    = FALSE;
static int callback_test_null_data_ptr_count   = 0;

static herr_t 
test_mdjsc_callback(const H5C_mdj_config_t * config_ptr,
                    hid_t UNUSED dxpl_id,
                    void * data_ptr)
{
    if ( config_ptr == NULL )
    {
        callback_test_null_config_ptr = TRUE;
    }
    
    if ( ( callback_test_cache_ptr == NULL ) ||
         ( callback_test_cache_ptr->magic != H5C__H5C_T_MAGIC ) )
    {
        callback_test_invalid_cache_ptr = TRUE;
    }
    else if ( callback_test_cache_ptr->slist_len > 0 )
    {
        callback_test_cache_is_dirty = TRUE;
    }
    else if ( ( callback_test_cache_ptr != NULL ) &&
              ( callback_test_cache_ptr->mdj_enabled != 
                config_ptr->enable_journaling ) )
    {
        callback_test_invalid_config = TRUE;
    }

    if ( data_ptr == NULL )
    {
        callback_test_null_data_ptr = TRUE;
	callback_test_null_data_ptr_count++;
    }
    else
    {
        *((int *)data_ptr) += 1;
    }

    return SUCCEED;

} /* test_mdjsc_callback() */


/***************************************************************************
 *
 * Function: 	deregister_mdjsc_callback()
 *
 * Purpose:  	Attempt to deregister the metadata journaling status change 
 * 		callback with the supplied index, and verify that the 
 * 		deregistration took place.
 *
 * 		If any error is detected, set pass t FALSE, and set the
 * 		failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
deregister_mdjsc_callback(H5F_t * file_ptr,
		          H5C_t * cache_ptr,
                          int32_t idx)
{
    herr_t result;

    if ( pass ) 
    {
        if ( ( file_ptr == NULL ) ||
	     ( cache_ptr == NULL ) ||
	     ( cache_ptr->magic != H5C__H5C_T_MAGIC ) )
        {
	    pass = FALSE;
            failure_mssg = 
		"deregister_mdjsc_callback(): bad param(s) on entry.";
	}
    }

    if ( pass )
    {
	result = H5AC_deregister_mdjsc_callback(file_ptr, idx);

	if ( result < 0 ) 
	{
	    pass = FALSE;
	    failure_mssg = "H5AC_deregister_mdjsc_callback() failed.";
	}

	verify_mdjsc_callback_deregistered(cache_ptr, idx);
    }

    return;

} /* deregister_mdjsc_callback() */


/***************************************************************************
 *
 * Function: 	register_mdjsc_callback()
 *
 * Purpose:  	Attempt to register the supplied metadata journaling 
 * 		status change callback, and verify that the registration
 * 		took.
 *
 * 		If any error is detected, set pass t FALSE, and set the
 * 		failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
register_mdjsc_callback(H5F_t * file_ptr,
		        H5C_t * cache_ptr,
		        H5C_mdj_status_change_func_t fcn_ptr,
                        void * data_ptr,
                        int32_t * idx_ptr)
{
    herr_t result;
    H5C_mdj_config_t init_config;

    if ( pass ) 
    {
        if ( ( file_ptr == NULL ) ||
	     ( cache_ptr == NULL ) ||
	     ( cache_ptr->magic != H5C__H5C_T_MAGIC ) ||
	     ( fcn_ptr == NULL ) ||
	     ( idx_ptr == NULL ) )
        {
	    pass = FALSE;
            failure_mssg = "register_mdjsc_callback(): bad param(s) on entry.";
	}
    }

    if ( pass )
    {
	result = H5AC_register_mdjsc_callback(file_ptr, fcn_ptr, data_ptr,
			                       idx_ptr, &init_config);

	if ( result < 0 ) 
	{
	    pass = FALSE;
	    failure_mssg = "H5AC_register_mdjsc_callback() failed.";
	}
	else if ( init_config.enable_journaling != cache_ptr->mdj_enabled )
	{
	    pass = FALSE;
	    failure_mssg = 
	        "init_config.enable_journaling != cache_ptr->mdj_enabled";
	}

	verify_mdjsc_callback_registered(cache_ptr, 
			                 fcn_ptr, 
			                 data_ptr, 
					 *idx_ptr);
    }

    return;

} /* register_mdjsc_callback() */


/***************************************************************************
 *
 * Function: 	verify_mdjsc_table_config()
 *
 * Purpose:  	Verify that the mdjsc callback table is configured as 
 *		specified.
 *
 *		If all is as it should be, do nothing.
 *
 *		If anything is not as it should be, set pass to FALSE,
 *              and set failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
verify_mdjsc_table_config(H5C_t * cache_ptr,
                          int32_t table_len,
                          int32_t num_entries,
                          int32_t max_idx_in_use,
                          hbool_t * free_entries)
{
    const char * fcn_name = "verify_mdjsc_table_config()";
    hbool_t show_progress = FALSE;
    int cp = 0;

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d: %d.\n", fcn_name, pass, cp++);

    if ( pass )
    {
        if ( ( cache_ptr == NULL ) ||
             ( cache_ptr->magic != H5C__H5C_T_MAGIC ) ) 
        {
            pass = FALSE;
            failure_mssg = "bad cache_ptr.";
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d: %d.\n", fcn_name, pass, cp++);

    if ( pass )
    {
        if ( cache_ptr->mdjsc_cb_tbl == NULL )
        {
            pass = FALSE;
            failure_mssg = "cache_ptr->mdjsc_cb_tbl == NULL.";
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d: %d.\n", fcn_name, pass, cp++);

    if ( pass )
    {
        if ( cache_ptr->mdjsc_cb_tbl_len != table_len )
        {
            pass = FALSE;
            failure_mssg = "mdjsc callback table len mismatch";
        }
        else if ( cache_ptr->num_mdjsc_cbs != num_entries )
        {
            pass = FALSE;
            failure_mssg = "mdjsc callback table num entries mismatch";
        }
        else if ( cache_ptr->mdjsc_cb_tbl_max_idx_in_use != max_idx_in_use )
        {
            pass = FALSE;
            failure_mssg = "mdjsc callback table max idx in use mismatch";
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d: %d.\n", fcn_name, pass, cp++);

    if ( ( pass ) && ( free_entries ) )
    {
        int32_t i = 0;
        int32_t j;
        H5C_mdjsc_record_t * record_ptr = NULL;

        while ( ( pass ) && ( i < table_len ) )
        {
            if ( free_entries[i] )
            {
                if ( (( (cache_ptr->mdjsc_cb_tbl)[i]).fcn_ptr != NULL)
                     ||
                     (((cache_ptr->mdjsc_cb_tbl)[i]).data_ptr != NULL)
                   )
                {
                    pass = FALSE;
                    failure_mssg = 
                        "mdjsc callback table entry in use that should be free";
                }
            } 
            else 
            {
		/* recall that the data_ptr can be NULL when an entry is
		 * in use.
		 */
                if ( ((cache_ptr->mdjsc_cb_tbl)[i]).fcn_ptr == NULL )
                {
                    pass = FALSE;
                    failure_mssg = 
                        "mdjsc callback table entry free that shoult be in use";
                }
            }

            i++;
        }

        if ( show_progress ) 
            HDfprintf(stdout, "%s:%d: %d.\n", fcn_name, pass, cp++);

        i = 0;
        j = cache_ptr->mdjsc_cb_tbl_fl_head;

        while ( ( pass ) && 
                ( i < (table_len - num_entries) ) && 
                ( j >= 0 ) &&
                ( j < table_len ) )
        {
            record_ptr = &((cache_ptr->mdjsc_cb_tbl)[j]);

            if ( ( record_ptr->fcn_ptr != NULL ) ||
                 ( record_ptr->data_ptr != NULL ) )
            {
                pass = FALSE;
                failure_mssg = "mdjsc callback table free list entry in use.";
            }
            
            i++;
            j = record_ptr->fl_next;
        }

        if ( show_progress ) 
            HDfprintf(stdout, "%s:%d: %d.\n", fcn_name, pass, cp++);

        if ( pass )
        {
            if ( i != (table_len - num_entries) ) {

                pass = FALSE;
                failure_mssg = 
                    "mdjsc callback table free list shorter than expected.";

            } else if ( ( record_ptr != NULL ) &&
			( record_ptr->fl_next != -1 ) ) {

                pass = FALSE;
                failure_mssg = 
                    "mdjsc callback table free list longer than expected.";

            }
        }
    }

    if ( show_progress ) 
        HDfprintf(stdout, "%s:%d: %d -- done.\n", fcn_name, pass, cp++);

    return;

} /* verify_mdjsc_table_config() */


/***************************************************************************
 *
 * Function: 	verify_mdjsc_callback_deregistered()
 *
 * Purpose:  	Verify that the suplied mdjsc callback is registerd
 *		in the metadata journaling status change callback table
 *		at the specified index and with the specified data ptr.
 *
 *		If all is as it should be, do nothing.
 *
 *		If anything is not as it should be, set pass to FALSE,
 *              and set failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
verify_mdjsc_callback_deregistered(H5C_t * cache_ptr,
                                   int32_t idx)
{
    if ( pass )
    {
        if ( ( cache_ptr == NULL ) ||
             ( cache_ptr->magic != H5C__H5C_T_MAGIC ) ) 
        {
            pass = FALSE;
            failure_mssg = "bad cache_ptr.";
        }
    }

    if ( pass )
    {
        if ( cache_ptr->mdjsc_cb_tbl == NULL )
        {
            pass = FALSE;
            failure_mssg = "cache_ptr->mdjsc_cb_tbl == NULL.";
        }
    }

    if ( ( pass ) && ( idx < cache_ptr->mdjsc_cb_tbl_len ) )
    {
        if ( ((cache_ptr->mdjsc_cb_tbl)[idx]).fcn_ptr != NULL )
        {
            pass = FALSE;
            failure_mssg = "fcn_ptr mismatch";
        }
        else if ( ((cache_ptr->mdjsc_cb_tbl)[idx]).data_ptr != NULL )
        {
            pass = FALSE;
            failure_mssg = "data_ptr mismatch";
        }
    }

    return;

} /* verify_mdjsc_callback_deregistered() */


/***************************************************************************
 *
 * Function: 	verify_mdjsc_callback_registered()
 *
 * Purpose:  	Verify that the suplied mdjsc callback is registerd
 *		in the metadata journaling status change callback table
 *		at the specified index and with the specified data ptr.
 *
 *		If all is as it should be, do nothing.
 *
 *		If anything is not as it should be, set pass to FALSE,
 *              and set failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
verify_mdjsc_callback_registered(H5C_t * cache_ptr,
                                 H5C_mdj_status_change_func_t fcn_ptr,
                                 void * data_ptr,
                                 int32_t idx)
{
    if ( pass )
    {
        if ( ( cache_ptr == NULL ) ||
             ( cache_ptr->magic != H5C__H5C_T_MAGIC ) ) 
        {
            pass = FALSE;
            failure_mssg = "bad cache_ptr.";
        }
    }

    if ( pass )
    {
        if ( ( fcn_ptr == NULL ) ||
             ( idx < 0 ) )
        {
            pass = FALSE;
            failure_mssg = "bad fcn_ptr and/or negative idx.";
        }
    }

    if ( pass )
    {
        if ( cache_ptr->mdjsc_cb_tbl == NULL )
        {
            pass = FALSE;
            failure_mssg = "cache_ptr->mdjsc_cb_tbl == NULL.";
        }
    }

    if ( pass )
    {
        if ( cache_ptr->mdjsc_cb_tbl_len <= idx )
        {
            pass = FALSE;
            failure_mssg = "idx out of range.";
        }
    }

    if ( pass )
    {
        if ( ((cache_ptr->mdjsc_cb_tbl)[idx]).fcn_ptr != fcn_ptr )
        {
            pass = FALSE;
            failure_mssg = "fcn_ptr mismatch";
        }
        else if ( ((cache_ptr->mdjsc_cb_tbl)[idx]).data_ptr != data_ptr )
        {
            pass = FALSE;
            failure_mssg = "data_ptr mismatch";
        }
        else if ( ((cache_ptr->mdjsc_cb_tbl)[idx]).fl_next != -1 )
        {
            pass = FALSE;
            failure_mssg = "fl_next != -1";
        }
    }

    return;

} /* verify_mdjsc_callback_registered() */


/***************************************************************************
 *
 * Function: 	verify_mdjsc_callback_error_rejection()
 *
 * Purpose:  	Run a variety of tests to verify that the metadata 
 *		journaling status change callbacks registration and 
 *		de-registration routines will fail on obviously 
 *		invalid input.
 *
 *		If anything is not as it should be, set pass to FALSE,
 *              and set failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/20/08
 * 
 **************************************************************************/

static void
verify_mdjsc_callback_error_rejection(void)
{
    const char * fcn_name = "verify_mdjsc_callback_error_rejection():";
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    const int max_callbacks = 1024 * H5C__MIN_MDJSC_CB_TBL_LEN;
    int counters[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    int i;
    int expected_num_entries = 0;
    int expected_table_len = H5C__MIN_MDJSC_CB_TBL_LEN;
    int expected_max_idx = -1;
    int32_t indicies[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    hbool_t free_entries[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0;
    herr_t result;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    for ( i = 0; i < max_callbacks; i++ )
    {
        counters[i] = 0;
	free_entries[i] = TRUE;
	indicies[i] = -1;
    }

    /* 1) Create a file with journaling enabled.  
     *
     * 2) Attempt to register callbacks with a variety of NULL 
     *    pointers supplied for parameters other than data_ptr.
     *    All attempts should fail.
     *
     * 3) Attempt to deregister a callback in an empty callback table.
     *    Should fail
     *
     * 4) Register a few callbacks.  Attempt to deregister non-existant
     *    callbacks with indicies both inside and outside the range
     *    of indicies currently represented in the table.  All should
     *    fail.
     *
     * 5) Deregister the remaining callbacks, and then close and delete
     *    the file.
     */


    /* 1) Create a file with journaling enabled.
     */

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename,
                        sizeof(filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (1).\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, pass, cp++);
        HDfflush(stdout);
    }

    if ( verbose ) {
        HDfprintf(stdout, "%s filename = \"%s\".\n", fcn_name, filename);
        HDfflush(stdout);
    }

    /* setup the journal file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename,
                        sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
        else if ( HDstrlen(journal_filename) >=
                        H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( verbose ) {
        HDfprintf(stdout, "%s journal filename = \"%s\".\n",
                  fcn_name, journal_filename);
        HDfflush(stdout);
    }

    /* clean out any existing journal file */
    HDremove(journal_filename);
    setup_cache_for_journaling(filename, journal_filename, &file_id,
                               &file_ptr, &cache_ptr, TRUE, FALSE, FALSE);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 2) Attempt to register callbacks with a variety of NULL 
     *    pointers supplied for parameters other than data_ptr.
     *    All attempts should fail.
     */

    if ( pass )
    {
        result = H5AC_register_mdjsc_callback(NULL,
				               test_mdjsc_callback, 
					       NULL, 
					       &(indicies[0]),
					       NULL);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_register_mdjsc_callback() succeeded with NULL file_ptr";
        }
    }

    if ( pass )
    {
        result = H5AC_register_mdjsc_callback(file_ptr,
				               NULL, 
					       NULL, 
					       &(indicies[0]),
					       NULL);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_register_mdjsc_callback() succeeded with NULL fcn_ptr";
        }
    }

    if ( pass )
    {
        result = H5AC_register_mdjsc_callback(file_ptr,
				               test_mdjsc_callback, 
					       NULL, 
					       NULL,
					       NULL);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_register_mdjsc_callback() succeeded with NULL idx_ptr";
        }
    }


    /* 3) Attempt to deregister a callback in an empty callback table.
     *    Should fail
     */

    if ( pass )
    {
        result = H5AC_deregister_mdjsc_callback(NULL, 0);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	      "H5AC_deregister_mdjsc_callback() succeeded with NULL file_ptr";
        }
    }

    if ( pass )
    {
        result = H5AC_deregister_mdjsc_callback(file_ptr, 0);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_deregister_mdjsc_callback() succeeded with invld idx(1)";
        }
    }



    /* 4) Register a few callbacks.  Attempt to deregister non-existant
     *    callbacks with indicies both inside and outside the range
     *    of indicies currently represented in the table.  All should
     *    fail.
     */

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            &(counters[0]), &(indicies[0]));
    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            NULL, &(indicies[1]));
    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            &(counters[2]), &(indicies[2]));

    free_entries[0] = FALSE; 
    free_entries[1] = FALSE; 
    free_entries[2] = FALSE; 
    expected_num_entries += 3;
    expected_max_idx = 2;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);


    if ( pass )
    {
        result = H5AC_deregister_mdjsc_callback(file_ptr, 3);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_deregister_mdjsc_callback() succeeded with invld idx(2)";
        }
    }

    if ( pass )
    {
        result = H5AC_deregister_mdjsc_callback(file_ptr, -1);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_deregister_mdjsc_callback() succeeded with invld idx(3)";
        }
    }

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);


    if ( pass )
    {
        result = H5AC_deregister_mdjsc_callback(file_ptr, 1);

        if ( result != SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_deregister_mdjsc_callback() failed with valid idx";
        }
        else
	{
            free_entries[1] = TRUE; 
            expected_num_entries--;

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);

	}
    }

    if ( pass )
    {
        result = H5AC_deregister_mdjsc_callback(file_ptr, -1);

        if ( result == SUCCEED )
        {
	    pass = FALSE;
	    failure_mssg = 
	        "H5AC_deregister_mdjsc_callback() succeeded with invld idx(4)";
        }
    }


    /* 5) Deregister the remaining callbacks, and then close and delete
     *    the file.
     */

    deregister_mdjsc_callback(file_ptr, cache_ptr, 0);
    deregister_mdjsc_callback(file_ptr, cache_ptr, 2);

    free_entries[0] = TRUE; 
    free_entries[2] = TRUE; 
    expected_num_entries -= 2;
    expected_max_idx = -1;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);


    /* Close the file, and tidy up.
     */

    if ( pass ) {

        if ( H5Fclose(file_id) < 0 ) {

	    pass = FALSE;
            failure_mssg = "H5Fclose() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* delete the HDF5 file and journal file */
#if 1
        HDremove(filename);
        HDremove(journal_filename);
#endif
    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d done.\n", 
		  fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    return;

} /* verify_mdjsc_callback_error_rejection() */


/***************************************************************************
 *
 * Function: 	verify_mdjsc_callback_execution()
 *
 * Purpose:  	Run a variety of tests to verify that the metadata 
 *		journaling status change callbacks are actually performed,
 *		at the correct time, and that the expected data is passed 
 *		to the callback function.
 *
 *		If anything is not as it should be, set pass to FALSE,
 *              and set failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
verify_mdjsc_callback_execution(void)
{
    const char * fcn_name = "verify_mdjsc_callback_execution():";
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    const int max_callbacks = 1024 * H5C__MIN_MDJSC_CB_TBL_LEN;
    int counters[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    int i;
    int expected_num_entries = 0;
    int expected_table_len = H5C__MIN_MDJSC_CB_TBL_LEN;
    int expected_max_idx = -1;
    int32_t indicies[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    hbool_t free_entries[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0;
    herr_t result;
    hid_t dataset_id = -1;
    hid_t dataspace_id = -1;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;
    hsize_t dims[2];
    H5AC_jnl_config_t jnl_config;

    for ( i = 0; i < max_callbacks; i++ )
    {
        counters[i] = 0;
	free_entries[i] = TRUE;
	indicies[i] = -1;
    }

    /*  1) Create a file with journaling enabled.  
     *
     *  2) Register a callback.  
     *
     *  3) Disable journaling.  Verify that the callback is called,
     *     that it gets the correct data, and that the cache is clean
     *     at time of call.
     *
     *  4) Enable journaling again.  Verify that the callback is 
     *     called, that it gets the correct data, and that the cache
     *     is clear at time of call.
     *
     *  5) Perform some writes to the file.  
     *
     *  6) Disable journaling.  Verify that the callback is called, 
     *     that it gets the correct data, and that the cache is 
     *     clean at time of call.
     *
     *  7) Perform some more writes to the file.
     *
     *  8) Enable journaling again.  Verify that the callback is
     *     called, that it gets the correct data, and that the cache
     *     is clear at time of call.
     *
     *  9) Deregister the callback, and close the file.  Recall that
     *     all metadata journaling status change callbacks must 
     *     deregister before the metadata cache is destroyed.
     *
     * 10) Re-open the file with journaling disabled, and register 
     *     several callbacks.  Ensure that at least one has NULL
     *     data_ptr.
     *
     * 11) Enable journaling.  Verify that the callbacks are called.
     *
     * 12) Perform some writes to the file.
     *
     * 13) Register a great number of callbacks.
     *
     * 14) Disable journaling.  Verify that the callbacks are called.
     *
     * 15) Deregister some of the callbacks.
     *
     * 16) Enable journaling.  Verify that the remaining callbacks are
     *     called.
     *
     * 17) Deregister the remaining callbacks, and then close and delete
     *     the file.
     */


    /* 1) Create a file with journaling enabled.
     */

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename,
                        sizeof(filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (1).\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, pass, cp++);
        HDfflush(stdout);
    }

    if ( verbose ) {
        HDfprintf(stdout, "%s filename = \"%s\".\n", fcn_name, filename);
        HDfflush(stdout);
    }

    /* setup the journal file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename,
                        sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
        else if ( HDstrlen(journal_filename) >=
                        H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( verbose ) {
        HDfprintf(stdout, "%s journal filename = \"%s\".\n",
                  fcn_name, journal_filename);
        HDfflush(stdout);
    }

    /* clean out any existing journal file */
    HDremove(journal_filename);
    setup_cache_for_journaling(filename, journal_filename, &file_id,
                               &file_ptr, &cache_ptr, TRUE, FALSE, FALSE);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 2) Register a callback.
     */

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            &(counters[0]), &(indicies[0]));

    free_entries[0] = FALSE; 
    expected_num_entries++;
    expected_max_idx = 0;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* 3) Disable journaling.  Verify that the callback is called,
     *    that it gets the correct data, and that the cache is clean
     *    at time of call.
     */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = FALSE;
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        counters[0]                       = 0;
        callback_test_cache_ptr           = cache_ptr;
        callback_test_invalid_cache_ptr   = FALSE;
        callback_test_null_config_ptr     = FALSE;
        callback_test_invalid_config      = FALSE;
        callback_test_null_data_ptr       = FALSE;
        callback_test_cache_is_dirty      = FALSE;
	callback_test_null_data_ptr_count = 0;

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( counters[0] != 1 )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(1).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(1).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) ||
                  ( callback_test_null_data_ptr ) ||
		  ( callback_test_null_data_ptr_count != 0 ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(1).";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /*  4) Enable journaling again.  Verify that the callback is 
     *     called, that it gets the correct data, and that the cache
     *     is clear at time of call.
     */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = TRUE;

        HDstrcpy(jnl_config.journal_file_path, journal_filename);

        jnl_config.journal_recovered       = FALSE;
        jnl_config.jbrb_buf_size           = (8 * 1024);
        jnl_config.jbrb_num_bufs           = 2;
        jnl_config.jbrb_use_aio            = FALSE;
        jnl_config.jbrb_human_readable     = TRUE;

    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        counters[0]                       = 0;
        callback_test_cache_ptr           = cache_ptr;
        callback_test_invalid_cache_ptr   = FALSE;
        callback_test_null_config_ptr     = FALSE;
        callback_test_invalid_config      = FALSE;
        callback_test_null_data_ptr       = FALSE;
        callback_test_cache_is_dirty      = FALSE;
        callback_test_null_data_ptr_count = 0;


        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( counters[0] != 1 )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(2).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(2).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) ||
                  ( callback_test_null_data_ptr ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(2).";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* 5) Perform some writes to the file. */

    if ( pass ) {

        dims[0] = 4;
        dims[1] = 6;
        dataspace_id = H5Screate_simple(2, dims, NULL);

        if ( dataspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Screate_simple() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        /* Create the dataset. */
        dataset_id = H5Dcreate2(file_id, "/dset0", H5T_STD_I32BE,
                                dataspace_id, H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT);

        if ( dataspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Dcreate2() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        /* close the data set, and the data space */
        if ( ( H5Dclose(dataset_id) < 0 ) ||
             ( H5Sclose(dataspace_id) < 0 ) )
        {
            pass = FALSE;
            failure_mssg = "data set, or data space close failed.";
        }
    }

    if ( pass ) {

        if ( cache_ptr->slist_len <= 0 ) {

            pass = FALSE;
            failure_mssg = "cache isnt' dirty?!?";
        }
    }


    /*  6) Disable journaling.  Verify that the callback is called, 
     *     that it gets the correct data, and that the cache is 
     *     clean at time of call.
     */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = FALSE;
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        counters[0]                     = 0;
        callback_test_cache_ptr         = cache_ptr;
        callback_test_invalid_cache_ptr = FALSE;
        callback_test_null_config_ptr   = FALSE;
        callback_test_invalid_config    = FALSE;
        callback_test_null_data_ptr     = FALSE;
        callback_test_cache_is_dirty    = FALSE;

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( counters[0] != 1 )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(3).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(3).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) ||
                  ( callback_test_null_data_ptr ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(3).";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /*  7) Perform some more writes to the file.  */

    if ( pass ) {

        dims[0] = 6;
        dims[1] = 8;
        dataspace_id = H5Screate_simple(2, dims, NULL);

        if ( dataspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Screate_simple() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        /* Create the dataset. */
        dataset_id = H5Dcreate2(file_id, "/dset1", H5T_STD_I32BE,
                                dataspace_id, H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT);

        if ( dataspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Dcreate2() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        /* close the data set, and the data space */
        if ( ( H5Dclose(dataset_id) < 0 ) ||
             ( H5Sclose(dataspace_id) < 0 ) )
        {
            pass = FALSE;
            failure_mssg = "data set, or data space close failed.";
        }
    }

    if ( pass ) {

        if ( cache_ptr->slist_len <= 0 ) {

            pass = FALSE;
            failure_mssg = "cache isnt' dirty?!?";
        }
    }


    /*  8) Enable journaling again.  Verify that the callback is
     *     called, that it gets the correct data, and that the cache
     *     is clear at time of call.
     */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = TRUE;

        HDstrcpy(jnl_config.journal_file_path, journal_filename);

        jnl_config.journal_recovered       = FALSE;
        jnl_config.jbrb_buf_size           = (8 * 1024);
        jnl_config.jbrb_num_bufs           = 2;
        jnl_config.jbrb_use_aio            = FALSE;
        jnl_config.jbrb_human_readable     = TRUE;

    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        counters[0]                       = 0;
        callback_test_cache_ptr           = cache_ptr;
        callback_test_invalid_cache_ptr   = FALSE;
        callback_test_null_config_ptr     = FALSE;
        callback_test_invalid_config      = FALSE;
        callback_test_null_data_ptr       = FALSE;
        callback_test_cache_is_dirty      = FALSE;
        callback_test_null_data_ptr_count = 0;

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( counters[0] != 1 )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(4).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(4).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) ||
                  ( callback_test_null_data_ptr ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(4).";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

     
    /*  9) Deregister the callback, and close the file.  Recall that
     *     all metadata journaling status change callbacks must 
     *     deregister before the metadata cache is destroyed.
     */

    deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[0]);

    indicies[0] = -1;
    free_entries[0] = TRUE; 
    expected_num_entries = 0;
    expected_max_idx = -1;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);

    if ( file_id >= 0 ) {

        if ( H5Fclose(file_id) < 0 ) {

            if ( pass ) {

                pass = FALSE;
                failure_mssg = "file close failed.";
            }
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d *cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }



    /* 10) Re-open the file with journaling disabled, and register 
     *     several callbacks.  Ensure that at least one has NULL
     *     data_ptr.
     */

    open_existing_file_without_journaling(filename, &file_id,
                                          &file_ptr, &cache_ptr);


    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            &(counters[0]), &(indicies[0]));
    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            NULL, &(indicies[1]));
    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            &(counters[2]), &(indicies[2]));

    free_entries[0] = FALSE; 
    free_entries[1] = FALSE; 
    free_entries[2] = FALSE; 
    expected_num_entries += 3;
    expected_max_idx = 2;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);


    /* 11) Enable journaling.  Verify that the callbacks are called. */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = TRUE;

        HDstrcpy(jnl_config.journal_file_path, journal_filename);

        jnl_config.journal_recovered       = FALSE;
        jnl_config.jbrb_buf_size           = (8 * 1024);
        jnl_config.jbrb_num_bufs           = 2;
        jnl_config.jbrb_use_aio            = FALSE;
        jnl_config.jbrb_human_readable     = TRUE;

    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        counters[0]                       = 0;
        callback_test_cache_ptr           = cache_ptr;
        callback_test_invalid_cache_ptr   = FALSE;
        callback_test_null_config_ptr     = FALSE;
        callback_test_invalid_config      = FALSE;
        callback_test_null_data_ptr       = FALSE;
        callback_test_cache_is_dirty      = FALSE;
        callback_test_null_data_ptr_count = 0;


        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        if ( ( counters[0] != 1 ) && 
	     ( counters[1] != 0 ) && 
	     ( counters[2] != 1 ) &&
	     ( counters[3] != 0 ) )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(5).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(5).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(5).";
        }
	else if ( ( ! callback_test_null_data_ptr ) ||
                  ( callback_test_null_data_ptr_count != 1 ) )
	{
	    pass = FALSE;
	    failure_mssg = "incorrect null data_ptr callbacks.(5)";
	}
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 12) Perform some writes to the file. */

    if ( pass ) {

        dims[0] = 8;
        dims[1] = 10;
        dataspace_id = H5Screate_simple(2, dims, NULL);

        if ( dataspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Screate_simple() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        /* Create the dataset. */
        dataset_id = H5Dcreate2(file_id, "/dset2", H5T_STD_I32BE,
                                dataspace_id, H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT);

        if ( dataspace_id < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Dcreate2() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

        /* close the data set, and the data space */
        if ( ( H5Dclose(dataset_id) < 0 ) ||
             ( H5Sclose(dataspace_id) < 0 ) )
        {
            pass = FALSE;
            failure_mssg = "data set, or data space close failed.";
        }
    }

    if ( pass ) {

        if ( cache_ptr->slist_len <= 0 ) {

            pass = FALSE;
            failure_mssg = "cache isnt' dirty?!?";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 13) Register a great number of callbacks. */

    for ( i = 3; i < max_callbacks; i++ )
    {
	if ( ( pass ) && ( free_entries[i] ) )
	{
            register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                    &(counters[i]), &(indicies[i]));

	    HDassert( indicies[i] == i );

            free_entries[i] = FALSE; 
            expected_num_entries++;

	    if ( i > expected_max_idx ) {

	        expected_max_idx = i;
	    }

	    if ( expected_num_entries > expected_table_len ) {

	        expected_table_len *= 2;
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    HDassert( expected_num_entries == max_callbacks );
    HDassert( expected_max_idx == (max_callbacks - 1) );
    HDassert( expected_table_len == max_callbacks );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 14) Disable journaling.  Verify that the callbacks are called. */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = FALSE;
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

	for ( i = 0; i < max_callbacks; i++ )
	{
            counters[i]                   = 0;
	}
        callback_test_cache_ptr           = cache_ptr;
        callback_test_invalid_cache_ptr   = FALSE;
        callback_test_null_config_ptr     = FALSE;
        callback_test_invalid_config      = FALSE;
        callback_test_null_data_ptr       = FALSE;
        callback_test_cache_is_dirty      = FALSE;
	callback_test_null_data_ptr_count = 0;

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {
	int max_counter = 0;
	int counter_sum = 0;

	for ( i = 0; i < max_callbacks; i++ )
	{
	    if ( counters[i] > max_counter )
	    {
	        max_counter = counters[i];
	    }
	    counter_sum += counters[i];
	}

        if ( ( counters[1] != 0 ) ||
	     ( max_counter != 1 ) ||
	     ( counter_sum != max_callbacks - 1 ) )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(6).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(6).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(6).";
        }
	else if ( ( ! callback_test_null_data_ptr ) ||
                  ( callback_test_null_data_ptr_count != 1 ) )
	{
	    pass = FALSE;
	    failure_mssg = "incorrect null data_ptr callbacks.(6)";
	}
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 15) Deregister some of the callbacks. */

    /* working from the top downwards, de-register all entries with 
     * indicies not divisible by 8.
     */

    for ( i = max_callbacks - 1;  i >= 0;  i-- )
    {
        if ( ( pass ) && ( ! free_entries[i] ) && ( (i % 8) != 0 ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
	        double fraction_in_use;

	        while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
	        {
	            expected_max_idx--;
	        }

                fraction_in_use = ((double)expected_num_entries) /
	                           ((double)expected_table_len);

	        while ( ( expected_max_idx < (expected_table_len / 2) ) 
	                &&
	                ( fraction_in_use < 
			  H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		        &&
		        ( (expected_table_len / 2) >= 
			  H5C__MIN_MDJSC_CB_TBL_LEN )
		      )
                {
	            expected_table_len /= 2;
	        }
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
	                              expected_num_entries, 
                                      expected_max_idx,
                                      free_entries);
	}
    }

    HDassert( expected_num_entries == max_callbacks / 8 );
    HDassert( expected_max_idx == (max_callbacks - 8) );
    HDassert( expected_table_len == max_callbacks );


    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 16) Enable journaling.  Verify that the remaining callbacks are
     *     called.
     */

    if ( pass ) {

        jnl_config.version = H5AC__CURR_JNL_CONFIG_VER;

        result = H5Fget_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fget_jnl_config() failed.\n";
        }

        /* set journaling config fields to taste */
        jnl_config.enable_journaling       = TRUE;

        HDstrcpy(jnl_config.journal_file_path, journal_filename);

        jnl_config.journal_recovered       = FALSE;
        jnl_config.jbrb_buf_size           = (8 * 1024);
        jnl_config.jbrb_num_bufs           = 2;
        jnl_config.jbrb_use_aio            = FALSE;
        jnl_config.jbrb_human_readable     = TRUE;

    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {

	for ( i = 0; i < max_callbacks; i++ )
	{
            counters[i]                   = 0;
	}
        callback_test_cache_ptr           = cache_ptr;
        callback_test_invalid_cache_ptr   = FALSE;
        callback_test_null_config_ptr     = FALSE;
        callback_test_invalid_config      = FALSE;
        callback_test_null_data_ptr       = FALSE;
        callback_test_cache_is_dirty      = FALSE;
	callback_test_null_data_ptr_count = 0;

        result = H5Fset_jnl_config(file_id, &jnl_config);

        if ( result < 0 ) {

            pass = FALSE;
            failure_mssg = "H5Fset_jnl_config() failed.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( pass ) {
	int max_counter = 0;
	int counter_sum = 0;

	for ( i = 0; i < max_callbacks; i++ )
	{
	    if ( counters[i] > max_counter )
	    {
	        max_counter = counters[i];
	    }
	    counter_sum += counters[i];
	}

        if ( ( max_counter != 1 ) ||
	     ( counter_sum != ( max_callbacks / 8 ) ) )
        {
            pass = FALSE;
            failure_mssg = "incorrect number of callback calls(7).";
	}
        else if ( callback_test_cache_is_dirty )
        {
            pass = FALSE;
            failure_mssg = "callback found dirty cache(7).";
        }
        else if ( ( callback_test_invalid_cache_ptr ) ||
                  ( callback_test_null_config_ptr ) ||
                  ( callback_test_invalid_config ) )
        {
            pass = FALSE;
            failure_mssg = "Bad parameter(s) to callback(7).";
        }
	else if ( ( callback_test_null_data_ptr ) ||
                  ( callback_test_null_data_ptr_count != 0 ) )
	{
	    pass = FALSE;
	    failure_mssg = "incorrect null data_ptr callbacks.(6)";
	}
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 17) Deregister the remaining callbacks, and then close and delete
     *     the file.
     */

    for ( i = max_callbacks - 1;  i >= 0;  i-- )
    {
        if ( ( pass ) && ( ! free_entries[i] ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
	        double fraction_in_use;

	        while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
	        {
	            expected_max_idx--;
	        }

                fraction_in_use = ((double)expected_num_entries) /
	                           ((double)expected_table_len);

	        while ( ( expected_max_idx < (expected_table_len / 2) ) 
	                &&
	                ( fraction_in_use < 
			  H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		        &&
		        ( (expected_table_len / 2) >= 
			  H5C__MIN_MDJSC_CB_TBL_LEN )
		      )
                {
	            expected_table_len /= 2;
	        }
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
	                              expected_num_entries, 
                                      expected_max_idx,
                                      free_entries);
	}
    }

    HDassert( expected_num_entries == 0 );
    HDassert( expected_max_idx == -1 );
    HDassert( expected_table_len == H5C__MIN_MDJSC_CB_TBL_LEN );


    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* Close the file, and tidy up.
     */

    if ( pass ) {

        if ( H5Fclose(file_id) < 0 ) {

	    pass = FALSE;
            failure_mssg = "H5Fclose() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* delete the HDF5 file and journal file */
#if 1
        HDremove(filename);
        HDremove(journal_filename);
#endif
    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d done.\n", 
		  fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    return;

} /* verify_mdjsc_callback_execution() */


/***************************************************************************
 *
 * Function: 	verify_mdjsc_callback_registration_deregistration()
 *
 * Purpose:  	Run a variety of tests to verify that the metadata 
 *		journaling status change callback registration and 
 *		deregistration works as expected.
 *
 *		If all tests pass, do nothing.
 *
 *		If anything is not as it should be, set pass to FALSE,
 *              and set failure_mssg to the appropriate error message.
 *
 *              Do nothing and return if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              8/15/08
 * 
 **************************************************************************/

static void
verify_mdjsc_callback_registration_deregistration(void)
{
    const char * fcn_name = 
        "verify_mdjsc_callback_registration_deregistration():";
    char filename[512];
    char journal_filename[H5AC__MAX_JOURNAL_FILE_NAME_LEN + 1];
    const int max_callbacks = 1024 * H5C__MIN_MDJSC_CB_TBL_LEN;
    int counters[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    int i;
    int j;
    int expected_num_entries = 0;
    int expected_table_len = H5C__MIN_MDJSC_CB_TBL_LEN;
    int expected_max_idx = -1;
    int32_t indicies[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    hbool_t free_entries[1024 * H5C__MIN_MDJSC_CB_TBL_LEN];
    hbool_t show_progress = FALSE;
    hbool_t verbose = FALSE;
    int cp = 0;
    hid_t file_id = -1;
    H5F_t * file_ptr = NULL;
    H5C_t * cache_ptr = NULL;

    for ( i = 0; i < max_callbacks; i++ )
    {
        counters[i] = 0;
	free_entries[i] = TRUE;
	indicies[i] = -1;
    }

    /*  1) Open a file for journaling.  It doesn't matter whether
     *     journaling is enabled or not, as this test is directed purely
     *     at the issue of whether the callback table is managed correctly.
     *
     *  2) Register a callback.  Verify that is is added correctly to
     *     the metadata journaling status change callback table.
     *
     *  3) Deregister the callback.  Verify that it is deleted correctly
     *     from the metadata journaling status change callback table.
     *
     *  4) Register H5C__MIN_MDJSC_CB_TBL_LEN - 1 callbacks.  Verify that
     *     they are all correctly added to the table, and that the table
     *     is of size H5C__MIN_MDJSC_CB_TBL_LEN, and that it contains
     *     the expected number of entries.
     *
     *  5) Register one more entry.  Verify that it is registered
     *     correctly, and that the table is now full.
     *
     *  6) Register another entry.  Verify that is is correctly registered,
     *     that the table has doubled in size.
     *
     *  7) In LIFO order, deregister (H5C__MIN_MDJSC_CB_TBL_LEN / 2) + 1
     *     callbacks in LIFO order.  Verify that the entries are deregistered,
     *     and that the table has not changed size.
     * 
     *  8) Again, in LIFO order, deregister another callback.  Verify that
     *     the callback is deregistered, and that the table has been reduced
     *     in size to H5C__MIN_MDJSC_CB_TBL_LEN.
     *
     *  9) Deregister all callbacks.  Verify that the table is empty.
     *
     * 10) Register 8 * H5C__MIN_MDJSC_CB_TBL_LEN + 1 callbacks.  Verify
     *     that all callbacks are registered, and that the table lenght grows
     *     to 16 * H5C__MIN_MDJSC_CB_TBL_LEN.
     *
     * 11) Deregister all callbacks with even indicies.  Verify the 
     *     deregistrations.  Verify that the table does not shrink.
     *
     * 12) Register a callback.  Verify that it is place in one of the 
     *     slots freed by the dergistrations in 11) above.
     *
     * 13) Starting with the lowest index, deregister all the callbacks.
     *     Verify the deregistrations, and also verify that the table 
     *     does not shrink until the last callback is de-registered.
     *
     * 14) Register 8 * H5C__MIN_MDJSC_CB_TBL_LEN + 1 callbacks.  Verify
     *     that all callbacks are registered, and that the table length grows
     *     to 16 * H5C__MIN_MDJSC_CB_TBL_LEN.
     *
     * 15) Starting with the highest index, deregister all entries with 
     *     index not divisible by H5C__MIN_MDJSC_CB_TBL_LEN / 2.  Verify
     *     that the callbacks are de-registers, and that the table does
     *     not shrink
     *
     * 16) Register H5C__MIN_MDJSC_CB_TBL_LEN / 2 callbacks.  Verify that 
     *     they are placed in slots freed by the dergistrations in 15) above.
     *
     * 17) Starting with the lowest index, deregister all entries with 
     *     index with index >= H5C__MIN_MDJSC_CB_TBL_LEN and not divisible 
     *     by H5C__MIN_MDJSC_CB_TBL_LEN.  Verify that the callbacks are 
     *     deregistered, and that the table does not shrink.
     *
     * 18) Register a callback.  Verify that it is place in one of the 
     *     slots freed by the dergistrations in 17) above.
     *
     * 19) Starting with the highest index, deregister all callbacks.
     *     Verify that the table shrinks as expected.
     *
     * 20) Do a torture tests -- forcing the number of registered callbacks
     *     into the thousands.  After each registration and deregistration,
     *     verify that the table is configured as expected.
     */


    /* 1) Open a file for journaling.  It doesn't matter whether
     *    journaling is enabled or not, as this test is directed purely
     *    at the issue of whether the callback table is managed correctly.
     */

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename,
                        sizeof(filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (1).\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, pass, cp++);
        HDfflush(stdout);
    }

    if ( verbose ) {
        HDfprintf(stdout, "%s filename = \"%s\".\n", fcn_name, filename);
        HDfflush(stdout);
    }

    /* setup the journal file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[3], H5P_DEFAULT, journal_filename,
                        sizeof(journal_filename)) == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed (2).\n";
        }
        else if ( HDstrlen(journal_filename) >=
                        H5AC__MAX_JOURNAL_FILE_NAME_LEN ) {

            pass = FALSE;
            failure_mssg = "journal file name too long.\n";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( verbose ) {
        HDfprintf(stdout, "%s journal filename = \"%s\".\n",
                  fcn_name, journal_filename);
        HDfflush(stdout);
    }

    /* clean out any existing journal file */
    HDremove(journal_filename);
    setup_cache_for_journaling(filename, journal_filename, &file_id,
                               &file_ptr, &cache_ptr, TRUE, FALSE, FALSE);


    /* 2) Register a callback.  Verify that is is added correctly to
     *    the metadata journaling status change callback table.
     */
    j = 0;

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		            &(counters[j]), &(indicies[j]));

    free_entries[j] = FALSE; 
    expected_num_entries++;
    expected_max_idx = 0;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);

    j++;

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 3) Deregister the callback.  Verify that it is deleted correctly
     *    from the metadata journaling status change callback table.
     */
    j--;

    deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[j]);

    free_entries[j] = TRUE; 
    expected_num_entries--;
    expected_max_idx = -1;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		              expected_num_entries, expected_max_idx,
                              free_entries);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 4) Register H5C__MIN_MDJSC_CB_TBL_LEN - 1 callbacks.  Verify that
     *    they are all correctly added to the table, and that the table
     *    is of size H5C__MIN_MDJSC_CB_TBL_LEN, and that it contains
     *    the expected number of entries.
     */
    for ( i = 0; i < H5C__MIN_MDJSC_CB_TBL_LEN - 1; i++ )
    {
        register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                &(counters[j]), &(indicies[j]));

        free_entries[j] = FALSE; 
        expected_num_entries++;
        expected_max_idx++;

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);

        j++;
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 5) Register one more entry.  Verify that it is registered
     *    correctly, and that the table is now full.
     */

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
                            &(counters[j]), &(indicies[j]));

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    free_entries[j] = FALSE; 
    expected_num_entries++;
    expected_max_idx++;

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
                              expected_num_entries, expected_max_idx,
                              free_entries);

    j++;

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    if ( ( pass ) && ( expected_num_entries != expected_table_len ) )
    {
	pass = FALSE;
	failure_mssg = "Unexpected table len(1)";
    }


    /* 6) Register another entry.  Verify that is is correctly registered,
     *    that the table has doubled in size.
     */

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
                            &(counters[j]), &(indicies[j]));

    free_entries[j] = FALSE; 
    expected_num_entries++;
    expected_max_idx++;
    expected_table_len *= 2;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
                              expected_num_entries, expected_max_idx,
                              free_entries);

    j++;

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 7) In LIFO order, deregister (H5C__MIN_MDJSC_CB_TBL_LEN / 2) + 1
     *    callbacks in LIFO order.  Verify that the entries are deregistered,
     *    and that the table has not changed size.
     */

    for ( i = 0; i < (H5C__MIN_MDJSC_CB_TBL_LEN / 2) + 1; i++ )
    {
        j--;

        deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[j]);

        free_entries[j] = TRUE; 
        expected_num_entries--;
        expected_max_idx--;

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 8) Again, in LIFO order, deregister another callback.  Verify that
     *    the callback is deregistered, and that the table has been reduced
     *    in size to H5C__MIN_MDJSC_CB_TBL_LEN.
     */

    j--;

    deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[j]);

    free_entries[j] = TRUE; 
    expected_num_entries--;
    expected_max_idx--;
    expected_table_len /= 2;

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
                              expected_num_entries, expected_max_idx,
                              free_entries);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

     
    /*  9) Deregister all callbacks.  Verify that the table is empty.
     */

    while ( expected_num_entries > 0 )
    {
        j--;

        deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[j]);

        free_entries[j] = TRUE; 
        expected_num_entries--;
        expected_max_idx--;

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 10) Register 8 * H5C__MIN_MDJSC_CB_TBL_LEN + 1 callbacks.  Verify
     *     that all callbacks are registered, and that the table length grows
     *     to 16 * H5C__MIN_MDJSC_CB_TBL_LEN.
     */

    for ( i = 0; i < ((8 * H5C__MIN_MDJSC_CB_TBL_LEN) + 1); i++ )
    {
        register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                &(counters[i]), &(indicies[i]));

	HDassert( indicies[i] == i );

        free_entries[i] = FALSE; 
        expected_num_entries++;
        expected_max_idx++;

	if ( expected_num_entries > expected_table_len ) {

	    expected_table_len *= 2;
	}

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);
    }

    HDassert( expected_table_len == 16 * H5C__MIN_MDJSC_CB_TBL_LEN );
    HDassert( expected_table_len < 1024 );
    HDassert( expected_max_idx == 8 * H5C__MIN_MDJSC_CB_TBL_LEN );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }
     

    /* 11) Deregister all callbacks with even indicies.  Verify the 
     *     deregistrations.  Verify that the table does not shrink.
     */

    for ( i = 0; i < (8 * H5C__MIN_MDJSC_CB_TBL_LEN) + 1; i += 2 )
    {
        deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	indicies[i] = -1;
        free_entries[i] = TRUE; 
        expected_num_entries--;

	if ( i == expected_max_idx ) {
            expected_max_idx--;
	}

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);

    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 12) Register a callback.  Verify that it is place in one of the 
     *     slots freed by the dergistrations in 11) above.
     */

    /* The index assigned to the new callback is determined by the 
     * free list management algorithm.  In the present implementation
     * freed entries are added to the head of the free list, so the
     * next index issues will be 8 * H5C__MIN_MDJSC_CB_TBL_LEN.
     */

    j = 8 * H5C__MIN_MDJSC_CB_TBL_LEN;

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
                            &(counters[j]), &(indicies[j]));

    HDassert( indicies[j] == j ); /* see comment above */
    free_entries[j] = FALSE; 
    expected_num_entries++;

    if ( j > expected_max_idx ) {

        expected_max_idx = j;
    }

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
                              expected_num_entries, expected_max_idx,
                              free_entries);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 13) Starting with the lowest index, deregister all the callbacks.
     *     Verify the deregistrations, and also verify that the table 
     *     does not shrink until the last callback is de-registered.
     */

    for ( i = 0; i < (8 * H5C__MIN_MDJSC_CB_TBL_LEN) + 1; i++ )
    {
	if ( ! free_entries[i] ) 
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
                expected_max_idx = -1;
		expected_table_len = H5C__MIN_MDJSC_CB_TBL_LEN;
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }
     

    /* 14) Register 8 * H5C__MIN_MDJSC_CB_TBL_LEN + 1 callbacks.  Verify
     *     that all callbacks are registered, and that the table length grows
     *     to 16 * H5C__MIN_MDJSC_CB_TBL_LEN.
     */

    for ( i = 0; i < ((8 * H5C__MIN_MDJSC_CB_TBL_LEN) + 1); i++ )
    {
        register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                &(counters[i]), &(indicies[i]));

	HDassert( indicies[i] == i );

        free_entries[i] = FALSE; 
        expected_num_entries++;
        expected_max_idx++;

	if ( expected_num_entries > expected_table_len ) {

	    expected_table_len *= 2;
	}

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);
    }

    HDassert( expected_table_len == 16 * H5C__MIN_MDJSC_CB_TBL_LEN );
    HDassert( expected_table_len < 1024 );
    HDassert( expected_max_idx == 8 * H5C__MIN_MDJSC_CB_TBL_LEN );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }
     

    /* 15) Starting with the highest index, deregister all entries with 
     *     index not divisible by H5C__MIN_MDJSC_CB_TBL_LEN / 2.  Verify
     *     that the callbacks are de-registers, and that the table does
     *     not shrink
     */

    for ( i = (8 * H5C__MIN_MDJSC_CB_TBL_LEN); i >= 0; i-- )
    {
	if ( ( ! free_entries[i] ) &&
	     ( (i % (H5C__MIN_MDJSC_CB_TBL_LEN /2)) != 0 ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
                expected_max_idx = -1;
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }
     

    /* 16) Register H5C__MIN_MDJSC_CB_TBL_LEN / 2 callbacks.  Verify that 
     *     they are placed in slots freed by the dergistrations in 15) above.
     */

    /* The index assigned to the new callback is determined by the 
     * free list management algorithm.  In the present implementation
     * freed entries are added to the head of the free list, so the
     * next index issues will be 1.
     */

    j = 1;

    for ( i = 0; i < H5C__MIN_MDJSC_CB_TBL_LEN / 2; i++ )
    {
        while ( ! free_entries[j] )
	{
	    j++;
	}

        register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
                                &(counters[j]), &(indicies[j]));

        HDassert( indicies[j] == j ); /* see comment above */
        free_entries[j] = FALSE; 
        expected_num_entries++;

        if ( j > expected_max_idx ) {

            expected_max_idx = j;
        }

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
                                  expected_num_entries, expected_max_idx,
                                  free_entries);
    }

    HDassert( j == (H5C__MIN_MDJSC_CB_TBL_LEN / 2) + 1 );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 17) Starting with the lowest index, deregister all entries with 
     *     index with index >= H5C__MIN_MDJSC_CB_TBL_LEN and not divisible 
     *     by H5C__MIN_MDJSC_CB_TBL_LEN.  Verify that the callbacks are 
     *     deregistered, and that the table does not shrink.
     */

    for ( i = H5C__MIN_MDJSC_CB_TBL_LEN; 
          i < (8 * H5C__MIN_MDJSC_CB_TBL_LEN) + 1; 
	  i++ )
    {
	if ( ( ! free_entries[i] ) &&
	     ( (i % H5C__MIN_MDJSC_CB_TBL_LEN) != 0 ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
                expected_max_idx = -1;
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }
     

    /* 18) Register a callback.  Verify that it is place in one of the 
     *     slots freed by the dergistrations in 17) above.
     */

    /* The index assigned to the new callback is determined by the 
     * free list management algorithm.  In the present implementation
     * freed entries are added to the head of the free list, so the
     * next index issues will be (7 * H5C__MIN_MDJSC_CB_TBL_LEN) +
     * (H5C__MIN_MDJSC_CB_TBL_LEN / 2).
     */

    j = (7 * H5C__MIN_MDJSC_CB_TBL_LEN) + (H5C__MIN_MDJSC_CB_TBL_LEN / 2);

    register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
                            &(counters[j]), &(indicies[j]));

    HDassert( indicies[j] == j ); /* see comment above */
    free_entries[j] = FALSE; 
    expected_num_entries++;

    if ( j > expected_max_idx ) {

        expected_max_idx = j;
    }

    verify_mdjsc_table_config(cache_ptr, expected_table_len, 
                              expected_num_entries, expected_max_idx,
                              free_entries);

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* 19) Starting with the highest index, deregister all callbacks.
     *     Verify that the table shrinks as expected.
     */

    for ( i = (8 * H5C__MIN_MDJSC_CB_TBL_LEN); i >= 0; i-- )
    {
	if ( ( pass ) && ( ! free_entries[i] ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
		double fraction_in_use;

		while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
		{
		    expected_max_idx--;
		}

                fraction_in_use = ((double)expected_num_entries) /
	                          ((double)expected_table_len);


		if ( ( expected_max_idx < (expected_table_len / 2) ) 
		     &&
		     ( fraction_in_use < H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		     &&
		     ( (expected_table_len / 2) >= H5C__MIN_MDJSC_CB_TBL_LEN )
		   )
		{
		    expected_table_len /= 2;
		}
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }
     

    /* 20) Do a torture tests -- forcing the number of registered callbacks
     *     into the thousands.  After each registration and deregistration,
     *     verify that the table is configured as expected.
     */

    /* register half the maximum number of callbacks in this test */

    for ( i = 0; i < (max_callbacks / 2); i++ )
    {
        register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                &(counters[i]), &(indicies[i]));

	HDassert( indicies[i] == i );

        free_entries[i] = FALSE; 
        expected_num_entries++;
        expected_max_idx++;

	if ( expected_num_entries > expected_table_len ) {

	    expected_table_len *= 2;
	}

        verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                  expected_num_entries, expected_max_idx,
                                  free_entries);
    }

    HDassert( expected_table_len == (max_callbacks / 2) );
    HDassert( expected_max_idx == ((max_callbacks / 2) - 1) );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

     
    /* Starting from 3 * max_callbacks / 8 and working down to 
     * max_callbacks / 8, deregister the odd index callbacks.
     */
    for ( i = (3 * max_callbacks / 8); i >= max_callbacks / 8; i-- )
    {
	if ( ( pass ) && ( ! free_entries[i] ) && ( (i % 2) == 1 ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
		double fraction_in_use;

		while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
		{
		    expected_max_idx--;
		}

                fraction_in_use = ((double)expected_num_entries) /
	                          ((double)expected_table_len);


		if ( ( expected_max_idx < (expected_table_len / 2) ) 
		     &&
		     ( fraction_in_use < H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		     &&
		     ( (expected_table_len / 2) >= H5C__MIN_MDJSC_CB_TBL_LEN )
		   )
		{
		    expected_table_len /= 2;
		}
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* now re-register the callbacks just deregistered.  To keep the test
     * at least somewhat sane, re-register the entries in the order they
     * appear in the free list, so as to maintain the indicies[i] == i
     * invarient.  At present, this means re-registering entries the 
     * the reverse of the order they were deregistered in.
     */
     
    for ( i = (max_callbacks / 8); i <= (3 * max_callbacks / 8); i++ )
    {
	if ( ( pass ) && ( free_entries[i] ) && ( (i % 2) == 1 ) )
	{
            register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                    &(counters[i]), &(indicies[i]));

	    HDassert( indicies[i] == i );

            free_entries[i] = FALSE; 
            expected_num_entries++;

	    if ( i > expected_max_idx ) {

	        expected_max_idx = i;
	    }

	    if ( expected_num_entries > expected_table_len ) {

	        expected_table_len *= 2;
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    HDassert( expected_num_entries == (max_callbacks / 2) );
    HDassert( expected_max_idx == ((max_callbacks / 2) - 1) );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* now register more entries up to max_callbacks */
     
    for ( i = (max_callbacks / 2); i < max_callbacks; i++ )
    {
	if ( ( pass ) && ( free_entries[i] ) )
	{
            register_mdjsc_callback(file_ptr, cache_ptr, test_mdjsc_callback,
		                    &(counters[i]), &(indicies[i]));

	    HDassert( indicies[i] == i );

            free_entries[i] = FALSE; 
            expected_num_entries++;

	    if ( i > expected_max_idx ) {

	        expected_max_idx = i;
	    }

	    if ( expected_num_entries > expected_table_len ) {

	        expected_table_len *= 2;
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                      expected_num_entries, expected_max_idx,
                                      free_entries);
        }
    }

    HDassert( expected_num_entries == max_callbacks );
    HDassert( expected_max_idx == (max_callbacks - 1) );
    HDassert( expected_table_len == max_callbacks );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* deregister every other 200 callbacks on increasing index */
    for ( i = 0; i < max_callbacks; i += 200 )
    {
        for ( j = i; ( ( i < j + 200 ) && ( j < max_callbacks ) ); j++ )
	{
	    if ( ( pass ) && ( ! free_entries[i] ) )
	    {
                deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	        indicies[i] = -1;
                free_entries[i] = TRUE; 
                expected_num_entries--;

	        if ( i == expected_max_idx ) 
	        {
		    double fraction_in_use;

		    while ( ( expected_max_idx >= 0 ) &&
                            ( free_entries[expected_max_idx] ) )
		    {
		        expected_max_idx--;
		    }

                    fraction_in_use = ((double)expected_num_entries) /
	                               ((double)expected_table_len);

		    if ( ( expected_max_idx < (expected_table_len / 2) ) 
		         &&
		         ( fraction_in_use < 
			   H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		         &&
		         ( (expected_table_len / 2) >= 
			   H5C__MIN_MDJSC_CB_TBL_LEN )
		       )
		    {
		        expected_table_len /= 2;
		    }
	        }

                verify_mdjsc_table_config(cache_ptr, expected_table_len, 
		                          expected_num_entries, 
					  expected_max_idx,
                                          free_entries);
            }
	}
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* working from the top downwards, de-register all entries with 
     * indicies not divisible by 3.
     */

    for ( i = max_callbacks - 1;  i >= 0;  i-- )
    {
        if ( ( pass ) && ( ! free_entries[i] ) && ( (i % 3) != 0 ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
	        double fraction_in_use;

	        while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
	        {
	            expected_max_idx--;
	        }

                fraction_in_use = ((double)expected_num_entries) /
	                           ((double)expected_table_len);

	        while ( ( expected_max_idx < (expected_table_len / 2) ) 
	                &&
	                ( fraction_in_use < 
			  H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		        &&
		        ( (expected_table_len / 2) >= 
			  H5C__MIN_MDJSC_CB_TBL_LEN )
		      )
                {
	            expected_table_len /= 2;
	        }
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
	                              expected_num_entries, 
                                      expected_max_idx,
                                      free_entries);
	}
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    
    /* working from low index up, deregister all entries with index
     * greater than (max_callbacks / 8).
     */

    for ( i = (max_callbacks / 8);  i < max_callbacks;  i++ )
    {
        if ( ( pass ) && ( ! free_entries[i] ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
	        double fraction_in_use;

	        while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
	        {
	            expected_max_idx--;
	        }

                fraction_in_use = ((double)expected_num_entries) /
	                           ((double)expected_table_len);

	        while ( ( expected_max_idx < (expected_table_len / 2) ) 
	                &&
	                ( fraction_in_use < 
			  H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		        &&
		        ( (expected_table_len / 2) >= 
			  H5C__MIN_MDJSC_CB_TBL_LEN )
		      )
                {
	            expected_table_len /= 2;
	        }
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
	                              expected_num_entries, 
                                      expected_max_idx,
                                      free_entries);
	}
    }

    HDassert( expected_table_len == (max_callbacks / 8) );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* working from the top down, deregister the remaining callbacks. */

    for ( i = (max_callbacks / 8);  i >= 0;  i-- )
    {
        if ( ( pass ) && ( ! free_entries[i] ) )
	{
            deregister_mdjsc_callback(file_ptr, cache_ptr, indicies[i]);

	    indicies[i] = -1;
            free_entries[i] = TRUE; 
            expected_num_entries--;

	    if ( i == expected_max_idx ) 
	    {
	        double fraction_in_use;

	        while ( ( expected_max_idx >= 0 ) &&
                        ( free_entries[expected_max_idx] ) )
	        {
	            expected_max_idx--;
	        }

                fraction_in_use = ((double)expected_num_entries) /
	                           ((double)expected_table_len);

	        while ( ( expected_max_idx < (expected_table_len / 2) ) 
	                &&
	                ( fraction_in_use < 
			  H5C__MDJSC_CB_TBL_MIN_ACTIVE_RATIO ) 
		        &&
		        ( (expected_table_len / 2) >= 
			  H5C__MIN_MDJSC_CB_TBL_LEN )
		      )
                {
	            expected_table_len /= 2;
	        }
	    }

            verify_mdjsc_table_config(cache_ptr, expected_table_len, 
	                              expected_num_entries, 
                                      expected_max_idx,
                                      free_entries);
	}
    }

    HDassert( expected_table_len == H5C__MIN_MDJSC_CB_TBL_LEN );
    HDassert( expected_num_entries == 0 );
    HDassert( expected_max_idx  == -1 );

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }


    /* Close the file, and tidy up.
     */

    if ( pass ) {

        if ( H5Fclose(file_id) < 0 ) {

	    pass = FALSE;
            failure_mssg = "H5Fclose() failed.";
        }
    }

    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d.\n", fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    /* delete the HDF5 file and journal file */
#if 1
        HDremove(filename);
        HDremove(journal_filename);
#endif
    if ( show_progress ) {

        HDfprintf(stdout, "%s%d cp = %d done.\n", 
		  fcn_name, (int)pass, cp++);
        HDfflush(stdout);
    }

    return;

} /* verify_mdjsc_callback_registration_deregistration() */


/***************************************************************************
 * Function: 	check_buffer_writes
 *
 * Purpose:  	Verify the function H5C_jb__write_to_buffer properly writes
 *              messages of varying sizes into the journal buffers, and 
 *              that the journal buffers properly flush out when filled.
 *
 * Return:      void
 *
 * Programmer: 	Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Thursday, February 21, 2008
 *
 * Changes:	John Mainzer -- 4/16/09
 *		Updated for the addition of new parameters to 
 *		H5C_jb__init().
 * 
 **************************************************************************/

static void 
check_buffer_writes(hbool_t use_aio)
{
    const char * fcn_name = "check_buffer_writes(): ";
    char filename[512];
    int i;
    herr_t result;
    H5C_jbrb_t jbrb_struct;
    FILE * readback;
    hbool_t show_progress = FALSE;
    int32_t checkpoint = 1;
    char filldata[12][100];
    int repeatnum[12];

    if ( use_aio ) {

        TESTING("metadata buffer & file aio writes");

    } else {

        TESTING("metadata buffer & file sio writes");
    }

    pass = TRUE;

    /* Initialize data to get written as tests */
    HDmemcpy(filldata[0], "abcdefghijklmn\n", 16);
    HDmemcpy(filldata[1], "ABCDEFGHIJKLMNO\n", 17);
    HDmemcpy(filldata[2], "AaBbCcDdEeFfGgHh\n", 18);
    HDmemcpy(filldata[3], "ZAB-ZAB-ZAB-ZAB-ZAB-ZAB-ZAB-ZA\n", 32);
    HDmemcpy(filldata[4], "ABC-ABC-ABC-ABC-ABC-ABC-ABC-ABC\n", 33);
    HDmemcpy(filldata[5], "BCD-BCD-BCD-BCD-BCD-BCD-BCD-BCD-\n", 34);
    HDmemcpy(filldata[6], "12345-12345-12345-12345-12345-12345-12345-1234\n", 
	     48);
    HDmemcpy(filldata[7], "01234-01234-01234-01234-01234-01234-01234-01234\n", 
	     49);
    HDmemcpy(filldata[8], "23456-23456-23456-23456-23456-23456-23456-23456-\n",
	     50);
    HDmemcpy(filldata[9], "aaaa-bbbb-cccc-dddd-eeee-ffff-gggg-hhhh-iiii-jjjj-kkkk-llll-mmmm-nnnn-oooo-pppp-qqqq-rrrr-ssss\n", 96);
    HDmemcpy(filldata[10], "bbbb-cccc-dddd-eeee-ffff-gggg-hhhh-iiii-jjjj-kkkk-llll-mmmm-nnnn-oooo-pppp-qqqq-rrrr-ssss-tttt-\n", 97);
    HDmemcpy(filldata[11], "cccc-dddd-eeee-ffff-gggg-hhhh-iiii-jjjj-kkkk-llll-mmmm-nnnn-oooo-pppp-qqqq-rrrr-ssss-tttt-uuuu-v\n", 98);
    
    /* Assert that size of data is as expected */
    HDassert(HDstrlen(filldata[0]) == 15);
    HDassert(HDstrlen(filldata[1]) == 16);
    HDassert(HDstrlen(filldata[2]) == 17);
    HDassert(HDstrlen(filldata[3]) == 31);
    HDassert(HDstrlen(filldata[4]) == 32);
    HDassert(HDstrlen(filldata[5]) == 33);
    HDassert(HDstrlen(filldata[6]) == 47);
    HDassert(HDstrlen(filldata[7]) == 48);
    HDassert(HDstrlen(filldata[8]) == 49);
    HDassert(HDstrlen(filldata[9]) == 95);
    HDassert(HDstrlen(filldata[10]) == 96);
    HDassert(HDstrlen(filldata[11]) == 97);

    /* Give structure its magic number */
    jbrb_struct.magic = H5C__H5C_JBRB_T_MAGIC;
	
    if ( show_progress ) /* 1 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
             == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed";

        } /* end if */

    } /* end if */
	
    if ( show_progress ) /* 2 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal_magic */	    123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            16, 
                               /* Number of Buffers */      3, 
                               /* Use Synchronois I/O */    use_aio, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != 0) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 1";

       	} /* end if */

    } /* end if */

    /* generate the header message manually */
    if ( pass ) {

        if ( H5C_jb__write_header_entry(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__write_header_entry failed";
        }
    }
	
    if ( show_progress ) /* 3 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush and truncate journal file to get rid of the header
     * message for subsequent tests. */
    if ( pass ) {
	
	if ( H5C_jb__flush(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
	    failure_mssg = "H5C_jb_flush failed";

	} /* end if */	

    } /* end if */
	
    /* Truncate journal file */
    if ( pass ) {

	if ( H5C_jb__trunc(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb_trunc failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 4 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* open journal file for reading */
    readback = fopen(filename, "r");

    /* run a collection of calls to write_flush_verify(). These calls 
     * write specific lengths of data into the journal buffers and 
     * then flushes them to disk, and ensures that what makes it to 
     * disk is as expected 
     */

    for (i=0; i<12; i++) {

	write_flush_verify(&jbrb_struct, 
			   (int)HDstrlen(filldata[i]), 
			   filldata[i], 
			   readback);

	if ( show_progress )
	    HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		      checkpoint++, (int)pass);

    } /* end for */

    /* run a collection of calls to write_noflush_verify(). These 
     * calls write specific lengths of data into the journal buffers 
     * multiple times, but only flushes at the end of the set of writes. 
     * This tests to ensure that the automatic flush calls in 
     * H5C_jb__write_to_buffer are working properly. The routine then 
     * ensures that what makes it it disk is as expected 
     */

    /* Initialize repeat array to specify how many times to repeat each write
       within the write_noflush_verify calls. */
    repeatnum[0] = 16;
    repeatnum[1] = 6;
    repeatnum[2] = 16;
    repeatnum[3] = 16;
    repeatnum[4] = 6;
    repeatnum[5] = 16;
    repeatnum[6] = 16;
    repeatnum[7] = 6;
    repeatnum[8] = 16;
    repeatnum[9] = 16;
    repeatnum[10] = 6;
    repeatnum[11] = 16;

    for (i=0; i<12; i++) {

        write_noflush_verify(&jbrb_struct,
                             (int)HDstrlen(filldata[i]),
                             filldata[i],
                             readback,
                             repeatnum[i]);
        
	if ( show_progress )
	    HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		      checkpoint++, (int)pass);

    } /* end for */
    
    /* close journal file pointer */
    fclose(readback);

    /* Truncate the journal file */
    if ( pass ) {

	if ( H5C_jb__trunc(&jbrb_struct) != SUCCEED ) {

		pass = FALSE;
		failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* take down the journal file */
    if ( pass ) {

	if (H5C_jb__takedown(&jbrb_struct) != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    /* report pass / failure information */
    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);

    }

    return;

} /* check_buffer_writes */



/***************************************************************************
 * Function: 	write_flush_verify
 *
 * Purpose:  	Helper function for check_buffer_writes test. Writes a 
 *              piece of data of specified size into the journal buffer, then
 *              flushes the journal buffers. The data is read back and
 *              verified for correctness.
 *
 * Return:      void
 *
 * Programmer: 	Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Thursday, February 21, 2008
 * 
 **************************************************************************/
static void 
write_flush_verify(H5C_jbrb_t * struct_ptr, 
		   int size, 
		   char * data, 
		   FILE * readback)
{
    char verify[150];

    if ( pass ) {

	if ( H5C_jb__write_to_buffer(struct_ptr, (size_t)size, 
				      data, 0, (uint64_t)0) != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__write_to_buffer failed";

	} /* end if */

    } /* end if */

    if ( pass ) {

	if ( H5C_jb__flush(struct_ptr) != SUCCEED ) {

	    pass = FALSE;
            failure_mssg = "H5C_jb_flush failed";

        } /* end if */

    } /* end if */

    if ( pass ) {

	fgets(verify, size+10, readback);
		
	if (HDstrcmp(verify, data) != 0) {

	    pass = FALSE;
	    failure_mssg = "Journal entry not written correctly";

	} /* end if */

    } /* end if */

    return;

} /* write_flush_verify */



/***************************************************************************
 * Function: 	write_noflush_verify
 *
 * Purpose:  	Helper function for check_buffer_writes test. Writes a 
 *              piece of data of specified size into the journal buffer
 *              multiple times, without calling H5C_jb__flush in between
 *              writes. After all writes are completed, H5C_jb__flush is 
 *              called, and the data is read back from the journal file and
 *              verified for correctness.
 *
 * Return:      void
 *
 * Programmer: 	Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Thursday, February 21, 2008
 * 
 **************************************************************************/
static void 
write_noflush_verify(H5C_jbrb_t * struct_ptr, 
		     int size, 
		     char * data, 
		     FILE * readback, 
		     int repeats)
{
    int i;
    char verify[150];	

    for (i=0; i<repeats; i++) {

        if ( pass ) {
	
            if ( H5C_jb__write_to_buffer(struct_ptr, (size_t)size, 
				          data, 0, (uint64_t)0) != SUCCEED ) {

                pass = FALSE;
                failure_mssg = "H5C_jb__write_to_buffer failed";

            } /* end if */

        } /* end if */

    } /* end for */

    if ( pass ) {

        if ( H5C_jb__flush(struct_ptr) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb_flush failed";

	} /* end if */	

    } /* end if */

    for (i=0; i<repeats; i++) {

        if ( pass ) {
            fgets(verify, size+10, readback);
            if (HDstrcmp(verify, data) != 0) {

                pass = FALSE;
                failure_mssg = "Journal entry not written correctly";

            } /* end if */

	} /* end if */

    } /* end for */

    return;

} /* write_noflush_verify */



/***************************************************************************
 * Function: 	check_message_format
 *
 * Purpose:  	Verify that the functions that write messages into the journal
 *              buffers actually write the correct messages.
 *
 * Return:      void
 *
 * Programmer: 	Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Tuesday, February 26, 2008
 *
 * Changes:	JRM -- 3/21/09
 *		Updated test to handle the new journal creation time strings
 *		in which all white space is replaced with underscores.
 *
 * 		JRM -- 4/16/09
 *		Updated for the addition of new parameters to 
 *		H5C_jb__init().
 * 
 **************************************************************************/
static void 
check_message_format(void)
{
    const char * fcn_name = "check_message_format(): ";
    char filename[512];
    char time_buf[32];
    char verify[9][500];
    char from_journal[9][500];
    char * p;
    hbool_t show_progress = FALSE;
    int32_t checkpoint = 1;
    int i;
    herr_t result;
    FILE * readback;
    H5C_jbrb_t jbrb_struct;
    time_t current_date;

    TESTING("journal file message format");

    pass = TRUE;

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
             == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed";

        } /* end if */

    } /* end if */

    /* Give structure its magic number */
    jbrb_struct.magic = H5C__H5C_JBRB_T_MAGIC;

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal_magic */          123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            16, 
                               /* Number of Buffers */      3, 
                               /* Use Synchronois I/O */    FALSE, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != 0) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 2";

       	} /* end if */

    } /* end if */

    if ( show_progress ) /* 1 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Start a transaction */
    if ( pass ) {

        if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                        /* trans number */  (uint64_t)1) 
           != SUCCEED ) {
    
            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 2 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)1, 
                                    /* base address */  (haddr_t)0, 
                                    /* data length  */  1, 
                                    /* data         */  (const uint8_t *)"A") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 3 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)1, 
                                    /* base address */  (haddr_t)1, 
                                    /* data length  */  2, 
                                    /* data         */  (const uint8_t *)"AB") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 4 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)1, 
                                    /* base address */  (haddr_t)3, 
                                    /* data length  */  4, 
                                    /* data         */  (const uint8_t *)"CDEF") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 5 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* End transaction */
    if ( pass ) {
        if ( H5C_jb__end_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                      /* trans number */  (uint64_t)1) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 6 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Start a transaction */
    if ( pass ) {

        if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                        /* trans number */  (uint64_t)2) 
           != SUCCEED ) {
    
            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 7 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)2, 
                                    /* base address */  (haddr_t)285, 
                                    /* data length  */  11, 
                                    /* data         */  (const uint8_t *)"Test Data?!") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 8 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* End transaction */
    if ( pass ) {
        if ( H5C_jb__end_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                      /* trans number */  (uint64_t)2) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 9 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( pass ) {

        current_date = time(NULL);

        /* load ascii representation of current_date into time_buf[],
         * replacing white space with underscores.
         */
        time_buf[31] = '\0'; /* just to be safe */

        if ( (p = HDctime(&current_date)) == NULL ) {

            pass = FALSE;
            failure_mssg = "HDctime() failed";

        } else {

            /* copy the string into time_buf, replacing white space with
             * underscores.
             *
             * Do this to make parsing the header easier.
             */
            i = 0;

            while ( ( i < 31 ) && ( *p != '\0' ) ) {

                if ( isspace(*p) ) {

                    time_buf[i] = '_';

                } else {

                    time_buf[i] = *p;
                }

                i++;
                p++;
            }

            time_buf[i] = '\0';
        }
    }

    if ( show_progress ) /* 10 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    if ( pass ) {

        /* Fill out verify array with expected messages */
        sprintf(verify[0], "0 ver_num 1 target_file_name HDF5.file journal_magic 123 creation_date %10.10s human_readable 1\n", time_buf);
        sprintf(verify[1], "1 bgn_trans 1\n");
        sprintf(verify[2], "2 trans_num 1 length 1 base_addr 0x0 body  41 \n");
        sprintf(verify[3], "2 trans_num 1 length 2 base_addr 0x1 body  41 42 \n");
        sprintf(verify[4], "2 trans_num 1 length 4 base_addr 0x3 body  43 44 45 46 \n");
        sprintf(verify[5], "3 end_trans 1\n");
        sprintf(verify[6], "1 bgn_trans 2\n");
        sprintf(verify[7], "2 trans_num 2 length 11 base_addr 0x11d body  54 65 73 74 20 44 61 74 61 3f 21 \n");
        sprintf(verify[8], "3 end_trans 2\n");

        /* verify that messages in journal are same as expected */
        readback = fopen(filename, "r");
        for (i = 0; i < 9; i++) {

            if ( pass) {

                fgets(from_journal[i], 300, readback);

                if ( HDstrcmp(verify[i], from_journal[i]) != 0) {

                    if ( show_progress ) {

                        HDfprintf(stdout, "verify[%d]       = \"%s\"\n", 
                                  i, verify[i]);
                        HDfprintf(stdout, "from_journal[%d] = \"%s\"\n", 
                                  i, from_journal[i]);
                    }

                    pass = FALSE;
                    failure_mssg = "journal file not written correctly 1";

                } /* end if */

            } /* end if */

        } /* end for */
 
        fclose(readback);
    }

    /* Truncate the journal file */
    if ( pass ) {

	if ( H5C_jb__trunc(&jbrb_struct) != SUCCEED ) {

		pass = FALSE;
		failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 11 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Start a transaction */
    if ( pass ) {

        if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                        /* trans number */  (uint64_t)3) 
           != SUCCEED ) {
    
            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 12 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)3, 
                                    /* base address */  (haddr_t)28591, 
                                    /* data length  */  6, 
                                    /* data         */  (const uint8_t *)"#1nN`}") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 13 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* End transaction */
    if ( pass ) {
        if ( H5C_jb__end_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                      /* trans number */  (uint64_t)3) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 14 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Add a comment */
    if ( pass ) {
        if ( H5C_jb__comment(/* H5C_jbrb_t     */  &jbrb_struct, 
                              /* comment message */  "This is a comment!") 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__comment failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 14 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Add a comment */
    if ( pass ) {
        if ( H5C_jb__comment(/* H5C_jbrb_t     */  &jbrb_struct, 
                              /* comment message */  "This is another comment!") 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__comment failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 14 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( pass ) {

        current_date = time(NULL);

        /* load ascii representation of current_date into time_buf[],
         * replacing white space with underscores.
         */

        time_buf[31] = '\0'; /* just to be safe */

        if ( (p = HDctime(&current_date)) == NULL ) {

            pass = FALSE;
            failure_mssg = "HDctime() failed";

        } else {

            /* copy the string into time_buf, replacing white space with
             * underscores.
             *
             * Do this to make parsing the header easier.
             */
            i = 0;

            while ( ( i < 31 ) && ( *p != '\0' ) ) {

                if ( isspace(*p) ) {

                    time_buf[i] = '_';

                } else {

                    time_buf[i] = *p;
                }

                i++;
                p++;
            }

            time_buf[i] = '\0';
        }
    }

    if ( show_progress ) /* 15 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    if ( pass ) {

        /* Fill out verify array with expected messages */
        sprintf(verify[0], "0 ver_num 1 target_file_name HDF5.file journal_magic 123 creation_date %10.10s human_readable 1\n", time_buf);
        sprintf(verify[1], "1 bgn_trans 3\n");
        sprintf(verify[2], "2 trans_num 3 length 6 base_addr 0x6faf body  23 31 6e 4e 60 7d \n");
        sprintf(verify[3], "3 end_trans 3\n");
        sprintf(verify[4], "C comment This is a comment!\n");
        sprintf(verify[5], "C comment This is another comment!\n");

        /* verify that messages in journal are same as expected */
        readback = fopen(filename, "r");
        for (i = 0; i < 6; i++) {

            if ( pass) {

                fgets(from_journal[i], 300, readback);

                if ( HDstrcmp(verify[i], from_journal[i]) != 0) {

                    if ( show_progress ) {

                        HDfprintf(stdout, "verify[%d]       = \"%s\"\n", 
                                  i, verify[i]);
                        HDfprintf(stdout, "from_journal[%d] = \"%s\"\n", 
                                  i, from_journal[i]);
                    }

                    pass = FALSE;
                    failure_mssg = "journal file not written correctly 2";

                } /* end if */

            } /* end if */

        } /* end for */

        fclose(readback);
    }

    /* Truncate the journal file */
    if ( pass ) {

	if ( H5C_jb__trunc(&jbrb_struct) != SUCCEED ) {

		pass = FALSE;
		failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* take down the journal file */
    if ( pass ) {

	if (H5C_jb__takedown(&jbrb_struct) != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 16 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* report pass / failure information */
    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);

    }

    return;

} /* end check_message_format */


/***************************************************************************
 * Function: 	check_binary_message_format
 *
 * Purpose:  	Verify that the functions that write binary messages into 
 *		the journal buffers actually write the correct messages.
 *
 *		Note that this test was hacked from Mike's similar test
 *		for the human readable journal messages.  Unlike Mike's
 *		code, it also tests the eoa message.
 *
 * Return:      void
 *
 * Programmer: 	John Mainzer
 *              5/2/09
 *
 * Changes:	None.
 * 
 **************************************************************************/

static void 
check_binary_message_format(void)
{
    const char * fcn_name = "check_binary_message_format()";
    char filename[512];
    char time_buf[32];
    char * p;
    hbool_t show_progress = FALSE;
    int32_t checkpoint = 1;
    int i;
    int fd;
    herr_t result;
    H5C_jbrb_t jbrb_struct;
    time_t current_date;

    TESTING("binary journal file message format");

    pass = TRUE;

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
             == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed";

        } /* end if */

    } /* end if */

    /* Give structure its magic number */
    jbrb_struct.magic = H5C__H5C_JBRB_T_MAGIC;

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal_magic */          123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            16, 
                               /* Number of Buffers */      3, 
                               /* Use Synchronois I/O */    FALSE, 
                               /* human readable journal */ FALSE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != 0) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 2";

       	} /* end if */

    } /* end if */

    if ( show_progress ) /* 1 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Start a transaction */
    if ( pass ) {

        if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                        /* trans number */  (uint64_t)1) 
           != SUCCEED ) {
    
            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 2 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)1, 
                                    /* base address */  (haddr_t)0, 
                                    /* data length  */  1, 
                                    /* data         */  (const uint8_t *)"A") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 3 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write an eoa message */
    if ( pass ) {

        if ( H5C_jb__eoa(/* H5C_jbrb_t */  &jbrb_struct, 
                          /* eoa         */  (haddr_t)0x01020304)
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__eoa failed(1)";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 4 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)1, 
                                    /* base address */  (haddr_t)1, 
                                    /* data length  */  2, 
                                    /* data         */  (const uint8_t *)"AB") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 5 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)1, 
                                    /* base address */  (haddr_t)3, 
                                    /* data length  */  4, 
                                    /* data         */  (const uint8_t *)"CDEF") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 6 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);
 
    /* End transaction */
    if ( pass ) {
        if ( H5C_jb__end_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                      /* trans number */  (uint64_t)1) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction failed (1)";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 7 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write an eoa message */
    if ( pass ) {

        if ( H5C_jb__eoa(/* H5C_jbrb_t */  &jbrb_struct, 
                          /* eoa         */  (haddr_t)0x0102030405)
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__eoa failed(2)";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 8 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Start a transaction */
    if ( pass ) {

        if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                        /* trans number */  (uint64_t)2) 
           != SUCCEED ) {
    
            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 9 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                    /* trans number */  (uint64_t)2, 
                                    /* base address */  (haddr_t)285, 
                                    /* data length  */  11, 
                                    /* data         */  (const uint8_t *)"Test Data?!") 
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 10 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* End transaction */
    if ( pass ) {
        if ( H5C_jb__end_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                      /* trans number */  (uint64_t)2) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction failed (2)";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 11 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( pass ) {

        current_date = time(NULL);

        /* load ascii representation of current_date into time_buf[],
         * replacing white space with underscores.
         */
        time_buf[31] = '\0'; /* just to be safe */

        if ( (p = HDctime(&current_date)) == NULL ) {

            pass = FALSE;
            failure_mssg = "HDctime() failed";

        } else {

            /* copy the string into time_buf, replacing white space with
             * underscores.
             *
             * Do this to make parsing the header easier.
             */
            i = 0;

            while ( ( i < 31 ) && ( *p != '\0' ) ) {

                if ( isspace(*p) ) {

                    time_buf[i] = '_';

                } else {

                    time_buf[i] = *p;
                }

                i++;
                p++;
            }

            time_buf[i] = '\0';
        }
    }

    if ( show_progress ) /* 12 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    if ( pass ) {

        char expected_header[256];
        int expected_header_len;

        uint8_t expected_msg_1[] =
        {
            /* mssg 1: begin transaction 1 */
                /* header:    */ 'b', 't', 'r', 'n', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_1_len = 13;

        uint8_t expected_msg_2[] =
        {
            /* mssg 2: journal entry */
                /* header:    */ 'j', 'e', 'n', 't',
                /* version:   */ 0x00, 
                /* trans num: */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* base addr: */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* length:    */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* body:      */ 'A',
                /* chksum:    */ 0x7c, 0x5f, 0xad, 0xda
        };
        int expected_msg_2_len = 34;

        uint8_t expected_msg_3[] =
        {
            /* mssg 3: eoas */
                /* header:    */ 'e', 'o', 'a', 's', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_3_len = 13;

        uint8_t expected_msg_4[] =
        {
            /* mssg 4: journal entry */
                /* header:    */ 'j', 'e', 'n', 't',
                /* version:   */ 0x00, 
                /* trans num: */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* base addr: */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* length:    */ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* body:      */ 'A', 'B',
                /* chksum:    */ 0x33, 0x93, 0x98, 0x21
        };
        int expected_msg_4_len = 35;

        uint8_t expected_msg_5[] =
        {
            /* mssg 5: journal entry */
                /* header:    */ 'j', 'e', 'n', 't',
                /* version:   */ 0x00, 
                /* trans num: */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* base addr: */ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* length:    */ 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* body:      */ 'C', 'D', 'E', 'F',
                /* chksum:    */ 0x6e, 0x7d, 0xaf, 0x57
        };
        int expected_msg_5_len = 37;

        uint8_t expected_msg_6[] =
        {
            /* mssg 6: end transaction 1 */
                /* header:    */ 'e', 't', 'r', 'n', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_6_len = 13;

        uint8_t expected_msg_7[] =
        {
            /* mssg 7: eoas */
                /* header:    */ 'e', 'o', 'a', 's', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00
        };
        int expected_msg_7_len = 13;

        uint8_t expected_msg_8[] =
        {
            /* mssg 8: begin transaction 2 */
                /* header:    */ 'b', 't', 'r', 'n', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_8_len = 13;

        uint8_t expected_msg_9[] =
        {
            /* mssg 9: journal entry */
                /* h9ader:    */ 'j', 'e', 'n', 't',
                /* version:   */ 0x00, 
                /* trans num: */ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* base addr: */ 0x1d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* length:    */ 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* body:      */ 'T', 'e', 's', 't', ' ', 'D', 'a', 't', 
                                 'a', '?', '!',
                /* chksum:    */ 0x01, 0x7f, 0xf3, 0x43
        };
        int expected_msg_9_len = 44;

        uint8_t expected_msg_10[] =
        {
            /* mssg 10: end transaction 2 */
                /* header:    */ 'e', 't', 'r', 'n', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_10_len = 13;

       
        sprintf(expected_header, "0 ver_num 1 target_file_name HDF5.file journal_magic 123 creation_date %10.10s human_readable 0 offset_width 8 length_width 8\n", time_buf);
        expected_header_len = HDstrlen(expected_header);

        if ( (fd = HDopen(filename, O_RDONLY, 0777)) == -1 ) {

            pass = FALSE;
            failure_mssg = "Can't open journal file for test (1).";
 
        } 
        
        if ( pass ) {

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ (uint8_t *)expected_header, 
                /* expected msg len     */ expected_header_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual headers differ.",
                /* read failure msg     */ "error reading header.",
                /* eof failure msg      */ "encountered eof in header msg.",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_1, 
                /* expected msg len     */ expected_msg_1_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 1 differ.",
                /* read failure msg     */ "error reading msg 1.",
                /* eof failure msg      */ "encountered eof in msg 1.",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_2, 
                /* expected msg len     */ expected_msg_2_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 2 differ.",
                /* read failure msg     */ "error reading msg 2.",
                /* eof failure msg      */ "encountered eof in msg 2",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_3, 
                /* expected msg len     */ expected_msg_3_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 3 differ.",
                /* read failure msg     */ "error reading msg 3.",
                /* eof failure msg      */ "encountered eof in msg 3",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_4, 
                /* expected msg len     */ expected_msg_4_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 4 differ.",
                /* read failure msg     */ "error reading msg 4.",
                /* eof failure msg      */ "encountered eof in msg 4",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_5, 
                /* expected msg len     */ expected_msg_5_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 5 differ.",
                /* read failure msg     */ "error reading msg 5.",
                /* eof failure msg      */ "encountered eof in msg 5",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_6, 
                /* expected msg len     */ expected_msg_6_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 6 differ.",
                /* read failure msg     */ "error reading msg 6.",
                /* eof failure msg      */ "encountered eof in msg 6",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_7, 
                /* expected msg len     */ expected_msg_7_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg7 differ.",
                /* read failure msg     */ "error reading msg 7.",
                /* eof failure msg      */ "encountered eof in msg 7",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_8, 
                /* expected msg len     */ expected_msg_8_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 8 differ.",
                /* read failure msg     */ "error reading msg 8.",
                /* eof failure msg      */ "encountered eof in msg 8",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_9, 
                /* expected msg len     */ expected_msg_9_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 9 differ.",
                /* read failure msg     */ "error reading msg 9.",
                /* eof failure msg      */ "encountered eof in msg 9",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_10, 
                /* expected msg len     */ expected_msg_10_len,
                /* last_msg             */ TRUE,
                /* mismatch failure msg */ "expected and actual msg 10 differ.",
                /* read failure msg     */ "error reading msg 10.",
                /* eof failure msg      */ "encountered eof in msg 10",
                /* not last msg failure */ "msg 10 does not end file");

            if ( HDclose(fd) != 0 ) {

                pass = FALSE;
                failure_mssg = "Unable to close journal file (1).";
            }
        }
    }

    /* Truncate the journal file */
    if ( pass ) {

	if ( H5C_jb__trunc(&jbrb_struct) != SUCCEED ) {

		pass = FALSE;
		failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 13 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write an eoa message */
    if ( pass ) {

        if ( H5C_jb__eoa(/* H5C_jbrb_t */ &jbrb_struct, 
                          /* eoa         */ (haddr_t)0x010203040506)
             != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__eoa failed(3)";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 14 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Start a transaction */
    if ( pass ) {

        if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                        /* trans number */  (uint64_t)3) 
           != SUCCEED ) {
    
            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 15 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Write a journal entry */
    if ( pass ) {

        if ( H5C_jb__journal_entry(/* H5C_jbrb_t  */  &jbrb_struct, 
                                 /* trans number */  (uint64_t)3, 
                                 /* base address */  (haddr_t)28591, 
                                 /* data length  */  6, 
                                 /* data         */  (const uint8_t *)"#1nN`}" )
             != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 16 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* End transaction */
    if ( pass ) {
        if ( H5C_jb__end_transaction(/* H5C_jbrb_t  */  &jbrb_struct, 
                                      /* trans number */  (uint64_t)3) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction failed (3)";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 17 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Add a comment */
    if ( pass ) {
        if ( H5C_jb__comment(/* H5C_jbrb_t     */  &jbrb_struct, 
                              /* comment message */  "This is a comment!") 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__comment failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 18 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Add a comment */
    if ( pass ) {
        if ( H5C_jb__comment(/* H5C_jbrb_t     */  &jbrb_struct, 
                              /* comment message */  "This is another comment!") 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__comment failed";
            
        } /* end if */

    } /* end if */

    if ( show_progress ) /* 19 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( pass ) {

        current_date = time(NULL);

        /* load ascii representation of current_date into time_buf[],
         * replacing white space with underscores.
         */

        time_buf[31] = '\0'; /* just to be safe */

        if ( (p = HDctime(&current_date)) == NULL ) {

            pass = FALSE;
            failure_mssg = "HDctime() failed";

        } else {

            /* copy the string into time_buf, replacing white space with
             * underscores.
             *
             * Do this to make parsing the header easier.
             */
            i = 0;

            while ( ( i < 31 ) && ( *p != '\0' ) ) {

                if ( isspace(*p) ) {

                    time_buf[i] = '_';

                } else {

                    time_buf[i] = *p;
                }

                i++;
                p++;
            }

            time_buf[i] = '\0';
        }
    }

    if ( show_progress ) /* 20 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    if ( pass ) {

        char expected_header[256];
        int expected_header_len;

        uint8_t expected_msg_11[] =
        {
            /* mssg 11: eoas */
                /* header:    */ 'e', 'o', 'a', 's', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00
        };
        int expected_msg_11_len = 13;


        uint8_t expected_msg_12[] =
        {
            /* mssg 12: begin transaction 3 */
                /* header:    */ 'b', 't', 'r', 'n', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_12_len = 13;

        uint8_t expected_msg_13[] =
        {
            /* mssg 13: journal entry */
                /* header:    */ 'j', 'e', 'n', 't',
                /* version:   */ 0x00, 
                /* trans num: */ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* base addr: */ 0xaf, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* length:    */ 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                /* body:      */ '#', '1', 'n', 'N', '`', '}',
                /* chksum:    */ 0x6b, 0x60, 0x0d, 0x6d
        };
        int expected_msg_13_len = 39;

        uint8_t expected_msg_14[] =
        {
            /* mssg 14: end transaction 1 */
                /* header:    */ 'e', 't', 'r', 'n', 
                /* version:   */ 0x00, 
                /* trans num: */ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        int expected_msg_14_len = 13;
       
        sprintf(expected_header, "0 ver_num 1 target_file_name HDF5.file journal_magic 123 creation_date %10.10s human_readable 0 offset_width 8 length_width 8\n", time_buf);
        expected_header_len = HDstrlen(expected_header);

        if ( (fd = HDopen(filename, O_RDONLY, 0777)) == -1 ) {

            pass = FALSE;
            failure_mssg = "Can't open journal file for test (2).";
 
        } 

        if ( pass ) {

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ (uint8_t *)expected_header, 
                /* expected msg len     */ expected_header_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual headers differ.",
                /* read failure msg     */ "error reading header.",
                /* eof failure msg      */ "encountered eof in header msg.",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_11, 
                /* expected msg len     */ expected_msg_11_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 11 differ.",
                /* read failure msg     */ "error reading msg 11.",
                /* eof failure msg      */ "encountered eof in msg 11.",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_12, 
                /* expected msg len     */ expected_msg_12_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 12 differ.",
                /* read failure msg     */ "error reading msg 12.",
                /* eof failure msg      */ "encountered eof in msg 12",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_13, 
                /* expected msg len     */ expected_msg_13_len,
                /* last_msg             */ FALSE,
                /* mismatch failure msg */ "expected and actual msg 13 differ.",
                /* read failure msg     */ "error reading msg 13.",
                /* eof failure msg      */ "encountered eof in msg 13",
                /* not last msg failure */ NULL);

            verify_journal_msg(
		/* fd                   */ fd, 
                /* expected_msg         */ expected_msg_14, 
                /* expected msg len     */ expected_msg_14_len,
                /* last_msg             */ TRUE,
                /* mismatch failure msg */ "expected and actual msg 14 differ.",
                /* read failure msg     */ "error reading msg 14.",
                /* eof failure msg      */ "encountered eof in msg 14",
                /* not last msg failure */ "msg 14 does not end file");


            if ( HDclose(fd) != 0 ) {

                pass = FALSE;
                failure_mssg = "Unable to close journal file (1).";
            }
        }
    }

    /* Truncate the journal file */
    if ( pass ) {

	if ( H5C_jb__trunc(&jbrb_struct) != SUCCEED ) {

		pass = FALSE;
		failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* take down the journal file */
    if ( pass ) {

	if (H5C_jb__takedown(&jbrb_struct) != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 20 */ 
	HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
		  checkpoint++, (int)pass);

    /* report pass / failure information */
    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);

    }

    return;

} /* check_binary_message_format() */


/***************************************************************************
 * Function: 	verify_journal_msg
 *
 * Purpose:  	Verify that the supplied expected journal message is 
 *		the next in the message in the indicated journal file.
 *
 *		Do nothing it the expected message matches the file
 *		contents.  If there is a mismatch, set pass to false
 *		and set the failure message as specified.
 *
 *		Exit without any action if pass is false on entry.
 *
 * Return:      void
 *
 * Programmer:  J Mainzer
 *
 * Changes:	None.
 * 
 **************************************************************************/

static void
verify_journal_msg(int fd,
                   uint8_t expected_msg[],
                   int expected_msg_len,
                   hbool_t last_msg,
                   const char * mismatch_failure_msg,
                   const char * read_failure_msg,
                   const char * eof_failure_msg,
                   const char * not_last_msg_msg)
{
    const char * fcn_name = "verify_journal_msg()";
    hbool_t verbose = TRUE;
    uint8_t ch;
    int i = 0;
    ssize_t ret_val;

    if ( pass ) {

        if ( ( fd < 0 ) ||
             ( expected_msg == NULL ) ||
             ( expected_msg_len <= 0 ) ||
             ( mismatch_failure_msg == NULL ) ||
             ( read_failure_msg  == NULL ) ||
             ( eof_failure_msg == NULL ) ||
             ( ( last_msg) && ( not_last_msg_msg == NULL ) ) ) {

            pass = FALSE;
            failure_mssg = "verify_journal_msg(): Bad params on entry.";
        }
    }

    while ( ( pass ) && ( i < expected_msg_len ) ) 
    {
        ret_val = read(fd, (void *)(&ch), (size_t)1);

        if ( ret_val == 1 ) {

            if ( ch != expected_msg[i] ) {

                pass = FALSE;
	        failure_mssg = mismatch_failure_msg;
            }

        } else if ( ret_val == -1 ) {

            if ( verbose ) {

                HDfprintf(stdout, "%s: read failed with errno = %d (%s).\n",
                          fcn_name, errno, strerror(errno));
            }

            pass = FALSE;
            failure_mssg = mismatch_failure_msg;

        } else if ( ret_val == 0 ) {

            if ( verbose ) {

                HDfprintf(stdout, "%s: unexpected EOF.\n", fcn_name);
            }

            pass = FALSE;
            failure_mssg = eof_failure_msg;

        } else {

            if ( verbose ) {

                HDfprintf(stdout, "%s: read returned unexpected value (%d).\n", 
                          fcn_name, (int)ret_val);
            }

            pass = FALSE;
            failure_mssg = "read returned unexpected value.";

        }

        i++;
            
    }

    if ( ( pass ) && ( last_msg ) ) {

        ret_val = read(fd, (void *)(&ch), (size_t)1);

        if ( ret_val != 0 ) {

            if ( verbose ) {

                HDfprintf(stdout, "%s: msg not at eof as expected.\n", fcn_name);
            }

            pass = FALSE;
            failure_mssg = not_last_msg_msg;
        }
    }

    return;

} /* verify_journal_msg() */


/***************************************************************************
 * Function: 	check_legal_calls
 *
 * Purpose:  	Verify that all H5C_jb functions prevent use when appropriate.
 *
 * Return:      void
 *
 * Programmer:  Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Tuesday, February 26, 2008
 *
 * Changes:	JRM -- 4/16/09
 *		Updated for the addition of new parameters to 
 *		H5C_jb__init().
 * 
 **************************************************************************/

static void 
check_legal_calls(void)
{
    const char * fcn_name = "check_legal_calls(): ";
    char filename[512];
    herr_t result;
    H5C_jbrb_t jbrb_struct;
    hbool_t show_progress = FALSE;
    int32_t checkpoint = 1;

    TESTING("journaling routine compatibility");

    pass = TRUE;

    /* Give structure its magic number */
    jbrb_struct.magic = H5C__H5C_JBRB_T_MAGIC;

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
             == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 1 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Initialize H5C_jbrb_t structure. This call should SUCCEED. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal magic */	    123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            4000, 
                               /* Number of Buffers */      3, 
                               /* Use Synchronois I/O */    FALSE, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != SUCCEED) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 3";

       	} /* end if */

    } /* end if */

    if ( show_progress ) /* 2 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);
#if 0
    /* Start transaction 2. This should FAIL because transaction 1 has
       not occurred yet. Ensure that it fails, and flag an error if it 
       does not. */
    /* transaction numbers need not be sequential, only monitonically 
     * increasing -- thus this is not an error any more.
     *                                                    -- JRM
     */
    if ( pass ) {

	if ( H5C_jb__start_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                        /* Transaction # */  (uint64_t)2)
           == SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction should have failed";

	} /* end if */

    } /* end if */ 
#endif
    if ( show_progress ) /* 3 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* End transaction 1. This should FAIL because transaction 1 has
       not started yet. Ensure that it fails, and flag an error if it 
       does not. */
    if ( pass ) {

	if ( H5C_jb__end_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                      /* Transaction # */  (uint64_t)1)
           == SUCCEED ) {
        
            pass = FALSE;
            failure_mssg = "H5C_jb__end_transaction should have failed";
 
	} /* end if */

    } /* end if */

    if ( show_progress ) /* 4 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to write a journal entry before transaction has started..
       This should FAIL because transaction 1 has not started yet. Ensure 
       that it fails, and flag an error if it does not. */
    if ( pass ) {

	if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  &jbrb_struct, 
                                    /* Transaction # */  (uint64_t)1,
                                    /* Base Address  */  (haddr_t)123456789, 
                                    /* Length        */  16, 
                                    /* Body          */  (const uint8_t *)"This should fail")
           == SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__journal_entry should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 5 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Start transaction 1. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__start_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                        /* Transaction # */  (uint64_t)1)
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__start_transaction failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 6 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to start transaction 1 again. This should FAIL because 
       transaction 1 is already open. Ensure that it fails, and flag an
       error if it does not. */
    if ( pass ) {

	if ( H5C_jb__start_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                        /* Transaction # */  (uint64_t)1)
           == SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__start_transaction should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 7 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to end transaction 1. This should FAIL because no 
       journal entry has been written under this transaction. */
    if ( pass ) {

	if ( H5C_jb__end_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                      /* Transaction # */  (uint64_t)1)
           == SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__end_transaction should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 8 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to write a journal entry into the wrong transaction number.
       This should FAIL because specified transaction number isn't in 
       progress. Ensure that it fails, and flag an error if it does not. */
    if ( pass ) {

	if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  &jbrb_struct, 
                                    /* Transaction # */  (uint64_t)2,
                                    /* Base Address  */  (haddr_t)123456789, 
                                    /* Length        */  16, 
                                    /* Body          */  (const uint8_t *)"This should fail")
           == SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__journal_entry should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 9 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write a journal entry during transaction 1. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  &jbrb_struct, 
                                    /* Transaction # */  (uint64_t)1,
                                    /* Base Address  */  (haddr_t)123456789, 
                                    /* Length        */  51, 
                                    /* Body          */  (const uint8_t *)"This is the first transaction during transaction 1.")
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__journal_entry failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 10 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to flush buffers. This should FAIL because a transaction
       is still in progress. Ensure that it fails, and flag an error
       if it does not. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
             == SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__flush should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 11 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* End transaction 1. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__end_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                      /* Transaction # */  (uint64_t)1)
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__end_transaction failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 12 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to start transaction 1 again. This should FAIL because
       transaction 1 has already occurred. Ensure that it fails, and flag
       an error if it does not. */
    if ( pass ) {

	if ( H5C_jb__start_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                        /* Transaction # */  (uint64_t)1)
           == SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__start_transaction should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 13 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Start transaction 2. This should SUCCEED.*/
    if ( pass ) {

	if ( H5C_jb__start_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                        /* Transaction # */  (uint64_t)2)
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__start_transaction failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 14 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write a journal entry during transaction 2. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  &jbrb_struct, 
                                    /* Transaction # */  (uint64_t)2,
                                    /* Base Address  */  (haddr_t)7465, 
                                    /* Length        */  51, 
                                    /* Body          */  (const uint8_t *)"This is the first transaction during transaction 2!")
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__journal_entry failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 15 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write a journal entry during transaction 2. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  &jbrb_struct, 
                                    /* Transaction # */  (uint64_t)2,
                                    /* Base Address  */  (haddr_t)123456789, 
                                    /* Length        */  60, 
                                    /* Body          */  (const uint8_t *)"... And here's your second transaction during transaction 2.")
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__journal_entry failed";
 
	} /* end if */

    } /* end if */

    if ( show_progress ) /* 16 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* End transaction 2. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__end_transaction(/* H5C_jbrb_t   */  &jbrb_struct, 
                                      /* Transaction # */  (uint64_t)2)
           != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__end_transaction failed";
  
	} /* end if */

    } /* end if */

    if ( show_progress ) /* 17 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to truncate the journal file. This should FAIL because the
       journal buffers have not been flushed yet. Ensure that it fails, and
       flag and error if it does not. */
    if ( pass ) {

	if ( H5C_jb__trunc(/* H5C_jbrb_t */  &jbrb_struct) 
           == SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__trunc should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 18 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to take down the ring buffer. This should FAIL because the 
       journal buffers have not been flushed yet. Ensure that it fails, and
       flag and error if it does not. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           == SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 19 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 20 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Attempt to take down the ring buffer. This should FAIL because the 
       journal file has not been truncated. Ensure that it fails, and
       flag and error if it does not. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           == SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown should have failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 21 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Truncate the journal file. This should SUCCEED. */
    if ( pass ) {

	if ( H5C_jb__trunc(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 22 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Take down the journal file. This should SUCCEED. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";
 
	} /* end if */

   } /* end if */

    if ( show_progress ) /* 23 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* report pass / failure information */
    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);

    }

    return;

} /* end check_legal_calls */



/***************************************************************************
 * Function: 	check_transaction_tracking
 *
 * Purpose:  	Verify that the ring buffer successfully tracks when
 *              transactions make it to disk. 
 *
 * Return:      void
 *
 * Programmer: 	Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Tuesday, February 26, 2008
 *
 * Changes:	JRM -- 4/16/09
 *		Updated for the addition of new parameters to 
 *		H5C_jb__init().
 * 
 **************************************************************************/
static void 
check_transaction_tracking(hbool_t use_aio)
{
    const char * fcn_name = "check_transaction_tracking(): ";
    char filename[512];
    int i;
    herr_t result;
    H5C_jbrb_t jbrb_struct;
    hbool_t show_progress = FALSE;
    int32_t checkpoint = 1;
    int expected_tval[12];

    if ( use_aio ) {

        TESTING("aio journal file transaction tracking");

    } else {

        TESTING("sio journal file transaction tracking");
    }

    pass = TRUE;

    /* setup the file name */
    if ( pass ) {

        if ( h5_fixname(FILENAMES[1], H5P_DEFAULT, filename, sizeof(filename))
             == NULL ) {

            pass = FALSE;
            failure_mssg = "h5_fixname() failed";

        } /* end if */

    } /* end if */

    if ( show_progress ) /* 1 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Give structure its magic number */
    jbrb_struct.magic = H5C__H5C_JBRB_T_MAGIC;

    /* ===================================================
     * First ring buffer configuration.
     * 4 Buffers, each size 250.
     * Writing transactions of size 100.
     * Test cases: 
     *     - writing multiple transactions in each buffer
     *     - writing end transaction message to exact end
     *       of a journal buffer, as well as the exact end
     *       of the ring buffer.
     * =================================================== */

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal magic */	    123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            250, 
                               /* Number of Buffers */      4, 
                               /* Use Synchronois I/O */    use_aio, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != SUCCEED) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 4";

       	} /* end if */

    } /* end if */


    /* H5C_jb__init() no longer generates the header message -- instead
     * it is generated by the first real journal entry.  This causes 
     * problems in this test, so generate the header message manually
     * and then flush it.
     */
    if ( pass ) {

        if ( H5C_jb__write_header_entry(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__write_header_entry failed";
        }
    }


    if ( show_progress ) /* 2 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 3 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write journal entries and verify that the transactions that get to
       disk are accurately reported after each write. The following test the
       case where multiple journal entries reside in each buffer before a flush
       occurs. Also, the case when a transaction ends on a buffer boundary
       is also tested. */

    /* set up array of expected transaction values on disk */
    expected_tval[0] = 0;
    expected_tval[1] = 0;
    expected_tval[2] = 0;
    expected_tval[3] = 2;
    expected_tval[4] = 2;
    expected_tval[5] = 5;
    expected_tval[6] = 5;
    expected_tval[7] = 5;
    expected_tval[8] = 7;
    expected_tval[9] = 7;
    expected_tval[10] = 10;

    /* write 20 messages and verify that expected values are as indicated in
       the expected_tval array */
    for (i = 1; i < 11; i++) {

        write_verify_trans_num(/* H5C_jbrb_t   */ &jbrb_struct, 
                       /* transaction num */ (uint64_t)i, 
                       /* min expected trans */ (uint64_t)expected_tval[i - 1],
                       /* expected trans */ (uint64_t)expected_tval[i]);

    } /* end for */

    if ( show_progress ) /* 4 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    /* Truncate the journal file. */
    if ( pass ) {

	if ( H5C_jb__trunc(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* Take down the journal file. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 5 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* ===================================================
     * Second ring buffer configuration
     * 4 Buffers, each size 100.
     * Writing transactions of size 100.
     * Test cases: 
     *     - end transaction messages appear on buffer
     *       boundaries.
     * =================================================== */

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal magic */	    123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            100, 
                               /* Number of Buffers */      4, 
                               /* Use Synchronois I/O */    FALSE, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != SUCCEED) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 5";

       	} /* end if */

    } /* end if */

    /* generate the header message manually */
    if ( pass ) {

        if ( H5C_jb__write_header_entry(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__write_header_entry failed";
        }
    }

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 6 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write journal entries and verify that the transactions that get to
       disk are accurately reported after each write. The following tests the
       case where end transaction messages hit exactly at the end of the 
       ring buffer. */
    for (i=1; i<20; i++) {

        write_verify_trans_num(/* H5C_ujbrb_t */&jbrb_struct, 
                           /* transaction num */(uint64_t)i, 
                           /* min expected trans on disk */ (uint64_t)(i - 1),
                           /* expected trans on disk */ (uint64_t)i);

    } /* end for */

    if ( show_progress ) /* 7 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    /* Truncate the journal file. */
    if ( pass ) {

	if ( H5C_jb__trunc(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* Take down the journal file. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    /* ===================================================
     * Third ring buffer configuration
     * 10 Buffers, each size 30.
     * Writing transactions of size 100.
     * Test cases: 
     *     - end transaction messages start in one buffer
     *       and end in the following buffer.
     *     - end transaction messages start in the last 
     *       buffer and loop around to the first buffer.
     *     - multiple buffers are filled between end 
     *       transaction messages.
     * =================================================== */

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
                               /* journal_magic */	    123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            30, 
                               /* Number of Buffers */      10, 
                               /* Use Synchronois I/O */    FALSE, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != SUCCEED) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 6";

       	} /* end if */

    } /* end if */

    /* generate the header message manually */
    if ( pass ) {

        if ( H5C_jb__write_header_entry(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__write_header_entry failed";
        }
    }


    if ( show_progress ) /* 8 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 9 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write journal entries and verify that the transactions that get to
       disk are accurately reported after each write. The following tests the
       case where end transaction messages start in one buffer and end in
       another buffer. Also tests the case where one transaction ends several
       buffers ahead of the next transaction end. */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)1, 
                           (uint64_t)0,
			   (uint64_t)0); /* 1 in bufs, 0 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)2, 
                           (uint64_t)0,
			   (uint64_t)1); /* 2 in bufs, 1 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)3, 
                           (uint64_t)1,
			   (uint64_t)3); /* nothing in bufs, 3 on disk */
    H5C_jb__write_to_buffer(&jbrb_struct, 10, "XXXXXXXXX\n", 0, (uint64_t)0);   
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)4, 
                           (uint64_t)3,
			   (uint64_t)3); /* 1 in bufs, 0 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)5, 
                           (uint64_t)3,
			   (uint64_t)5); /* 2 in bufs, 1 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)6, 
                           (uint64_t)5,
			   (uint64_t)5); /* nothing in bufs, 3 on disk */
    H5C_jb__write_to_buffer(&jbrb_struct, 10, "XXXXXXXXX\n", 0, (uint64_t)0);   
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)7, 
                           (uint64_t)5,
			   (uint64_t)7); /* 1 in bufs, 0 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)8, 
                           (uint64_t)7,
			   (uint64_t)7); /* 2 in bufs, 1 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)9, 
                           (uint64_t)7,
			   (uint64_t)8); /* nothing in bufs, 3 on disk */
    H5C_jb__write_to_buffer(&jbrb_struct, 10, "XXXXXXXXX\n", 0, (uint64_t)0);   
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)10, 
                           (uint64_t)8,
			   (uint64_t)9); /* 1 in bufs, 0 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)11, 
                           (uint64_t)9,
			   (uint64_t)10); /* 2 in bufs, 1 on disk */
    write_verify_trans_num(&jbrb_struct, 
		           (uint64_t)12, 
                           (uint64_t)10,
			   (uint64_t)12); /* nothing in buf, 3 on disk */

    if ( show_progress ) /* 10 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    /* Truncate the journal file. */
    if ( pass ) {

	if ( H5C_jb__trunc(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* Take down the journal file. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    /* ===================================================
     * Fourth ring buffer configuration
     * 35 Buffers, each size 1.
     * Writing transactions of size 100.
     * Test cases: 
     *     - end transaction messages are longer than the 
     *       entire ring buffer structure. note this is an
     *       extreme corner case situation as buffer sizes
     *       should generally be much larger than an end
     *       transaction message.
     * =================================================== */

    /* Initialize H5C_jbrb_t structure. */
    if ( pass ) {

        /* Note that the sizeof_addr & sizeof_size parameters are 
         * ignored when human_readable is TRUE.
         */

       	result = H5C_jb__init(/* H5C_jbrb_t */            &jbrb_struct, 
			       /* journal_magic */	    123,
                               /* HDF5 file name */         HDF5_FILE_NAME,
                               /* journal file name */      filename, 
                               /* Buffer size */            1, 
                               /* Number of Buffers */      35, 
                               /* Use Synchronois I/O */    FALSE, 
                               /* human readable journal */ TRUE,
                               /* sizeof_addr */            8,
                               /* sizeof_size */            8);

        if ( result != SUCCEED) {

            pass = FALSE;
            failure_mssg = "H5C_jb_init failed, check 7";

       	} /* end if */

    } /* end if */

    /* generate the header message manually */
    if ( pass ) {

        if ( H5C_jb__write_header_entry(&jbrb_struct) != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__write_header_entry failed";
        }
    }


    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 11 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Write journal entries and verify that the transactions that get to
       disk are accurately reported after each write. The following tests the
       case where end transaction messages take up several journal buffers, and
       ensures that the trans_tracking array is properly propogated */
    for (i=1; i<5; i++) {

        write_verify_trans_num(/* H5C_jbrb_t */  &jbrb_struct, 
                           /* transaction num */  (uint64_t)i, 
                           /* min expected returned trans */  (uint64_t)(i - 1),
                           /* expected returned trans */  (uint64_t)i);

    } /* end for */

    if ( show_progress ) /* 12 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* Flush the journal buffers. */
    if ( pass ) {

	if ( H5C_jb__flush(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__flush failed";

	} /* end if */

    } /* end if */

    /* Truncate the journal file. */
    if ( pass ) {

	if ( H5C_jb__trunc(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED ) {

            pass = FALSE;
            failure_mssg = "H5C_jb__trunc failed";

	} /* end if */

    } /* end if */

    /* Take down the journal file. */
    if ( pass ) {

	if (H5C_jb__takedown(/* H5C_jbrb_t */  &jbrb_struct) 
           != SUCCEED) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__takedown failed";

	} /* end if */

    } /* end if */

    if ( show_progress ) /* 13 */ 
        HDfprintf(stdout, "%s%0d -- pass = %d\n", fcn_name, 
                  checkpoint++, (int)pass);

    /* report pass / failure information */
    if ( pass ) { PASSED(); } else { H5_FAILED(); }

    if ( ! pass ) {

	failures++;
        HDfprintf(stdout, "%s: failure_mssg = \"%s\".\n",
                  fcn_name, failure_mssg);

    }

    return;

} /* end check_transaction_tracking */



/***************************************************************************
 * Function: 	write_verify_trans_num
 *
 * Purpose:  	Helper function for check_transaction_tracking test. Writes a 
 *              journal entry of length 100 into the ring buffer, provided that
 *              the transaction number of the journal entry is less than 1000, 
 *              and then verifies that the recorded last transaction on disk is 
 *              as specified in verify_val. 
 *
 * Return:      void
 *
 * Programmer: 	Mike McGreevy <mcgreevy@hdfgroup.org>
 *              Thursday, February 28, 2008
 *
 * Changes:	Modified the function to deal with the use of asynchronous
 *		syncs.  Specifically added the min_verify_val parameter,
 *		and code to detect when the journal write code is using 
 *		aio and aio_fsync() to determine when transactions are on
 *		disk.  
 *
 *		When the journal write code is using aio_fsync() the 
 *		inital requirement is that the last trans on disk 
 *		returned fall in the closed interval [min_verify_val,
 *		verify_val].  If the reported last trans on disk is 
 *		not equal to verify_val, the function must wait until
 *		all pending asynchronous syncs have completed, and 
 *		query for the last trans on disk again.  This time 
 *		it must equal verify_val.
 *
 *		If the journal code is not using aio_fsync(), the 
 *		processing of the function is unchanged.
 *
 *						JRM -- 2/14/10
 * 
 **************************************************************************/
static void
write_verify_trans_num(H5C_jbrb_t * struct_ptr, 
                       uint64_t trans_num, 
                       uint64_t min_verify_val,
                       uint64_t verify_val)
{
    hbool_t verbose = FALSE;
    uint64_t trans_verify;
    
    /* Write an entire transaction. (start, journal entry, end).
     * As long as the supplied transaction number is less than 1000,
     * the total length of the transaction will be 100. For cases where
     * the transaction number increases in number of digits, the amount
     * of data in the body is reduced to account for the extra trans digits,
     * so transactions remain at size 100. Note that data is converted
     * into hex, so reducing input by one character reduces journal entry 
     * by three (two hex characters and a space).
     */  
    if ( pass ) {
        
       	if ( H5C_jb__start_transaction(/* H5C_jbrb_t  */  struct_ptr, 
                                        /* trans number */  trans_num)
           != SUCCEED) {

            pass = FALSE;
            failure_mssg = "H5C_jb__start_transaction failed";

       	} /* end if */


        if (trans_num < 10) {

	        if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  struct_ptr, 
                                            /* Transaction # */  trans_num,
                                            /* Base Address  */  (haddr_t)16, 
                                            /* Length        */  9, 
                                            /* Body          */  (const uint8_t *)"XXXXXXXXX")
                   != SUCCEED ) {

	            pass = FALSE;
	            failure_mssg = "H5C_jb__journal_entry failed";

	        } /* end if */

        } /* end if */

        else if (trans_num < 100) {

	        if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  struct_ptr, 
                                            /* Transaction # */  trans_num,
                                            /* Base Address  */  (haddr_t)16, 
                                            /* Length        */  8, 
                                            /* Body          */  (const uint8_t *)"XXXXXXXX")
                   != SUCCEED ) {

	            pass = FALSE;
	            failure_mssg = "H5C_jb__journal_entry failed";

	        } /* end if */

        } /* end else if */

        else {

	        if ( H5C_jb__journal_entry(/* H5C_jbrb_t   */  struct_ptr, 
                                            /* Transaction # */  trans_num,
                                            /* Base Address  */  (haddr_t)16, 
                                            /* Length        */  7, 
                                            /* Body          */  (const uint8_t *)"XXXXXXX")
                   != SUCCEED ) {

	            pass = FALSE;
	            failure_mssg = "H5C_jb__journal_entry failed";

	        } /* end if */

        } /* end else */

	if ( H5C_jb__end_transaction(/* H5C_jbrb_t   */  struct_ptr, 
                                      /* Transaction # */  trans_num)
             != SUCCEED ) {

	    pass = FALSE;
	    failure_mssg = "H5C_jb__end_transaction failed";

	} /* end if */

    } /* end if */

    /* Make sure the last transaction that's on disk is as expected. */
    if ( pass ) {

        if ( H5C_jb__get_last_transaction_on_disk(
                                              /* H5C_jbrb_t  */  struct_ptr,
                                              /* trans number */  &trans_verify)
           != SUCCEED ) {
            
            pass = FALSE;
            failure_mssg = "H5C_jb__get_last_transaction_on_disk failed(1)";

        } /* end if */

        if ( struct_ptr->use_aio_fsync ) {

            if ( ( trans_verify < min_verify_val ) ||
                 ( verify_val < trans_verify ) ) {

                pass = FALSE;
                failure_mssg = "H5C_jb__get_last_transaction_on_disk returned initial value that is out of range.";
            }

            /* we must wait until the async writes and syncs complete
             * before the expected value will be returned by 
             * H5C_jb__get_last_transaction_on_disk().
             */

            if ( ( pass ) && ( verify_val != trans_verify ) ) {

                if ( H5C_jb_aio__await_completion_of_all_pending_writes(
						  struct_ptr) != SUCCEED ) {

                    pass = FALSE;
                    failure_mssg = "H5C_jb_aio__await_completion_of_all_pending_writes() failed.";
                }
            }

            if ( ( pass ) && ( verify_val != trans_verify ) ) {

                if ( H5C_jb_aio__await_completion_of_all_async_fsyncs(
						  struct_ptr) != SUCCEED ) {

                    pass = FALSE;
                    failure_mssg = "H5C_jb_aio__await_completion_of_all_async_fsyncs() failed.";
                }
            }
                
            if ( ( pass ) && ( verify_val != trans_verify ) ) {

                if ( H5C_jb__get_last_transaction_on_disk(
                                              /* H5C_jbrb_t  */  struct_ptr,
                                              /* trans number */  &trans_verify)
                     != SUCCEED ) {
            
                    pass = FALSE;
                    failure_mssg = "H5C_jb__get_last_transaction_on_disk failed(2)";

                } /* end if */
            } /* end if

            if ( ( pass ) && ( trans_verify != verify_val ) ) {

                pass = FALSE;

                if ( verbose ) {

                    HDfprintf(stdout, "min/actual/max = %lld/%lld/%lld.\n",
                              (long long)min_verify_val,
                              (long long)trans_verify,
                              (long long)verify_val);
                }

                failure_mssg = "H5C_jb__get_last_transaction_on_disk returned the wrong transaction number!(1)";

            }
        } else {
            if ( trans_verify != verify_val) {

                pass = FALSE;
                failure_mssg = "H5C_jb__get_last_transaction_on_disk returned the wrong transaction number!(2)";

            } /* end if */
        } /* end if */
    } /* end if */

    return;

} /* end write_verify_trans_num */


/*-------------------------------------------------------------------------
 * Function:	main
 *
 * Purpose:	Run tests on the cache code contained in H5C.c
 *
 * Return:	Success:
 *
 *		Failure:
 *
 * Programmer:	John Mainzer
 *              6/24/04
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */

int
main(void)
{
    int express_test;

    failures = 0;

    H5open();

    express_test = GetTestExpress();

    if ( express_test >= 3 ) {

        skip_long_tests = TRUE;

    } else {

        skip_long_tests = FALSE;

    }
    
    /* SIO Human readable smoke checks */
#if 1
    mdj_smoke_check_00(TRUE, FALSE);
#endif
#if 1
    mdj_smoke_check_01(TRUE, FALSE);
#endif
#if 1
    mdj_smoke_check_02(TRUE, FALSE);
#endif
#if 1
    mdj_api_example_test(TRUE, FALSE, 32, (16 * 1024));
#endif


    /* SIO Binary readable smoke checks */
#if 1
    mdj_smoke_check_00(FALSE, FALSE);
#endif
#if 1
    mdj_smoke_check_01(FALSE, FALSE);
#endif
#if 1
    mdj_smoke_check_02(FALSE, FALSE);
#endif
#if 1
    mdj_api_example_test(FALSE, FALSE, 32, (16 * 1024));
#endif
    

    /* AIO Human readable smoke checks */
#if 1
    mdj_smoke_check_00(TRUE, TRUE);
#endif
#if 1
    mdj_smoke_check_01(TRUE, TRUE);
#endif
#if 1
    mdj_smoke_check_02(TRUE, TRUE);
#endif
#if 1
    mdj_api_example_test(TRUE, TRUE, 32, (16 * 1024));
#endif


    /* AIO Binary readable smoke checks */
#if 1
    mdj_smoke_check_00(FALSE, TRUE);
#endif
#if 1
    mdj_smoke_check_01(FALSE, TRUE);
#endif
#if 1
    mdj_smoke_check_02(FALSE, TRUE);
#endif
#if 1
    mdj_api_example_test(FALSE, TRUE, 32, (16 * 1024));
#endif


    /* targeted tests */
#if 1
    check_buffer_writes(FALSE);
#endif
#if 1
    check_buffer_writes(TRUE);
#endif
#if 1
    check_legal_calls();
#endif
#if 1 
    check_message_format();
#endif
#if 1
    check_transaction_tracking(FALSE);
#endif
#if 1
    check_transaction_tracking(TRUE);
#endif
#if 1
    check_binary_message_format();
#endif
#if 1
    check_superblock_extensions();
#endif 
#if 1
    check_mdjsc_callbacks();
#endif

    return(failures);

} /* main() */

