/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2006      Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * Copyright (c) 2006      University of Houston. All rights reserved.
 * Copyright (c) 2009      Sun Microsystems, Inc.  All rights reserved.
 *
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include "opal/event/event.h"
#include "opal/runtime/opal_progress.h"
#include "opal/mca/maffinity/base/base.h"
#include "opal/mca/base/base.h"
#include "orte/util/show_help.h"
#include "opal/sys/atomic.h"

#include "orte/util/proc_info.h"
#include "orte/mca/oob/base/base.h"
#include "orte/mca/rml/rml.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/grpcomm/grpcomm.h"
#include "orte/runtime/runtime.h"

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/group/group.h"
#include "ompi/errhandler/errcode.h"
#include "ompi/communicator/communicator.h"
#include "ompi/datatype/datatype.h"
#include "ompi/op/op.h"
#include "ompi/file/file.h"
#include "ompi/info/info.h"
#include "ompi/runtime/mpiruntime.h"
#include "ompi/attribute/attribute.h"
#include "ompi/mca/pml/pml.h"
#include "ompi/mca/pml/base/base.h"
#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"
#include "ompi/mca/topo/topo.h"
#include "ompi/mca/topo/base/base.h"
#include "ompi/mca/io/io.h"
#include "ompi/mca/io/base/base.h"
#include "ompi/mca/mpool/base/base.h"
#include "ompi/mca/rcache/base/base.h"
#include "ompi/mca/pml/base/pml_base_bsend.h"
#include "ompi/runtime/params.h"
#include "ompi/mca/mpool/base/mpool_base_tree.h"
#include "ompi/mca/dpm/base/base.h"
#include "ompi/mca/pubsub/base/base.h"

#if OPAL_ENABLE_FT == 1
#include "ompi/mca/crcp/crcp.h"
#include "ompi/mca/crcp/base/base.h"
#endif
#include "ompi/runtime/ompi_cr.h"

int ompi_mpi_finalize(void)
{
    int ret;
    static int32_t finalize_has_already_started = 0;
    opal_list_item_t *item;

    /* Be a bit social if an erroneous program calls MPI_FINALIZE in
       two different threads, otherwise we may deadlock in
       ompi_comm_free() (or run into other nasty lions, tigers, or
       bears) */

    if (! opal_atomic_cmpset_32(&finalize_has_already_started, 0, 1)) {
        /* Note that if we're already finalized, we cannot raise an
           MPI exception.  The best that we can do is write something
           to stderr. */
        char hostname[MAXHOSTNAMELEN];
        pid_t pid = getpid();
        gethostname(hostname, sizeof(hostname));

        orte_show_help("help-mpi-runtime.txt",
                       "mpi_finalize:invoked_multiple_times",
                       true, hostname, pid);
        return MPI_ERR_OTHER;
    }

    /* Per MPI-2:4.8, we have to free MPI_COMM_SELF before doing
       anything else in MPI_FINALIZE (to include setting up such that
       MPI_FINALIZED will return true). */

    if (NULL != ompi_mpi_comm_self.comm.c_keyhash) {
        ompi_attr_delete_all(COMM_ATTR, &ompi_mpi_comm_self,
                             ompi_mpi_comm_self.comm.c_keyhash);
        OBJ_RELEASE(ompi_mpi_comm_self.comm.c_keyhash);
        ompi_mpi_comm_self.comm.c_keyhash = NULL;
    }

    /* Proceed with MPI_FINALIZE */

    ompi_mpi_finalized = true;

#if OMPI_ENABLE_PROGRESS_THREADS == 0
    opal_progress_set_event_flag(OPAL_EVLOOP_ONELOOP);
#endif

    /* Redo ORTE calling opal_progress_event_users_increment() during
       MPI lifetime, to get better latency when not using TCP */
    opal_progress_event_users_increment();

    /* If maffinity was setup, tear it down */
    if (ompi_mpi_maffinity_setup) {
        opal_maffinity_base_close();
    }

    /* wait for everyone to reach this point
       This is a grpcomm barrier instead of an MPI barrier because an
       MPI barrier doesn't ensure that all messages have been transmitted
       before exiting, so the possibility of a stranded message exists.
    */
    if (OMPI_SUCCESS != (ret = orte_grpcomm.barrier())) {
        ORTE_ERROR_LOG(ret);
        return ret;
    }

    /*
     * Shutdown the Checkpoint/Restart Mech.
     */
    if (OMPI_SUCCESS != (ret = ompi_cr_finalize())) {
        ORTE_ERROR_LOG(ret);
    }

    /* Shut down any bindings-specific issues: C++, F77, F90 */

    /* Remove all memory associated by MPI_REGISTER_DATAREP (per
       MPI-2:9.5.3, there is no way for an MPI application to
       *un*register datareps, but we don't want the OMPI layer causing
       memory leaks). */
    while (NULL != (item = opal_list_remove_first(&ompi_registered_datareps))) {
        OBJ_RELEASE(item);
    }
    OBJ_DESTRUCT(&ompi_registered_datareps);

    /* Remove all F90 types from the hash tables. As the OBJ_DESTRUCT will
     * call a special destructor able to release predefined types, we can
     * simply call the OBJ_DESTRUCT on the hash table and all memory will
     * be correctly released.
     */
    OBJ_DESTRUCT( &ompi_mpi_f90_integer_hashtable );
    OBJ_DESTRUCT( &ompi_mpi_f90_real_hashtable );
    OBJ_DESTRUCT( &ompi_mpi_f90_complex_hashtable );

    /* Free communication objects */

    /* free window resources */

    /* free file resources */
    if (OMPI_SUCCESS != (ret = ompi_file_finalize())) {
        return ret;
    }

    /* free window resources */
    if (OMPI_SUCCESS != (ret = ompi_win_finalize())) {
        return ret;
    }
    if (OMPI_SUCCESS != (ret = ompi_osc_base_finalize())) {
        return ret;
    }

    /* free pml resource */ 
    if(OMPI_SUCCESS != (ret = mca_pml_base_finalize())) { 
      return ret;
    }
    /* free communicator resources */
    if (OMPI_SUCCESS != (ret = ompi_comm_finalize())) {
        return ret;
    }

    /* free requests */
    if (OMPI_SUCCESS != (ret = ompi_request_finalize())) {
        return ret;
    }

    /* If requested, print out a list of memory allocated by ALLOC_MEM
       but not freed by FREE_MEM */
    if (0 != ompi_debug_show_mpi_alloc_mem_leaks) {
        mca_mpool_base_tree_print();
    }

    /* Now that all MPI objects dealing with communications are gone,
       shut down MCA types having to do with communications */
    if (OMPI_SUCCESS != (ret = mca_pml_base_close())) {
        return ret;
    }

    /* shut down buffered send code */
    mca_pml_base_bsend_fini();

#if OPAL_ENABLE_FT == 1
    /*
     * Shutdown the CRCP Framework, must happen after PML shutdown
     */
    if (OMPI_SUCCESS != (ret = ompi_crcp_base_close() ) ) {
        ORTE_ERROR_LOG(ret);
        return ret;
    }
#endif

    /* Free secondary resources */

    /* free attr resources */
    if (OMPI_SUCCESS != (ret = ompi_attr_finalize())) {
        return ret;
    }

    /* free group resources */
    if (OMPI_SUCCESS != (ret = ompi_group_finalize())) {
        return ret;
    }

    /* free proc resources */
    if ( OMPI_SUCCESS != (ret = ompi_proc_finalize())) {
        return ret;
    }
    
    /* finalize the pubsub functions */
    if ( OMPI_SUCCESS != (ret = ompi_pubsub_base_close())) {
        return ret;
    }
    
    /* finalize the DPM framework */
    if ( OMPI_SUCCESS != (ret = ompi_dpm_base_close())) {
        return ret;
    }
    
    /* free internal error resources */
    if (OMPI_SUCCESS != (ret = ompi_errcode_intern_finalize())) {
        return ret;
    }
     
    /* free error code resources */
    if (OMPI_SUCCESS != (ret = ompi_mpi_errcode_finalize())) {
        return ret;
    }

    /* free errhandler resources */
    if (OMPI_SUCCESS != (ret = ompi_errhandler_finalize())) {
        return ret;
    }

    /* Free all other resources */

    /* free op resources */
    if (OMPI_SUCCESS != (ret = ompi_op_finalize())) {
        return ret;
    }

    /* free ddt resources */
    if (OMPI_SUCCESS != (ret = ompi_ddt_finalize())) {
        return ret;
    }

    /* free info resources */
    if (OMPI_SUCCESS != (ret = ompi_info_finalize())) {
        return ret;
    }

    /* Close down MCA modules */

    /* io is opened lazily, so it's only necessary to close it if it
       was actually opened */

    if (mca_io_base_components_opened_valid ||
        mca_io_base_components_available_valid) {
        if (OMPI_SUCCESS != (ret = mca_io_base_close())) {
            return ret;
        }
    }
    if (OMPI_SUCCESS != (ret = mca_topo_base_close())) {
        return ret;
    }
    if (OMPI_SUCCESS != (ret = ompi_osc_base_close())) {
        return ret;
    }
    if (OMPI_SUCCESS != (ret = mca_coll_base_close())) {
        return ret;
    }
    if (OMPI_SUCCESS != (ret = mca_mpool_base_close())) {
        return ret;
    }
    if (OMPI_SUCCESS != (ret = mca_rcache_base_close())) { 
        return ret;
    }
    
    /* Leave the RTE */

    if (OMPI_SUCCESS != (ret = orte_finalize())) {
        return ret;
    }

    /* All done */

    return MPI_SUCCESS;
}
