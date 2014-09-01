/* -*- C -*-
 *
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/** @file:
 *
 */

/*
 * includes
 */
#include "orte_config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "opal/mca/mca.h"
#include "opal/util/output.h"
#include "opal/mca/base/mca_base_param.h"

#include "opal/dss/dss.h"
#include "orte/constants.h"
#include "orte/types.h"
#include "orte/util/proc_info.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/rml/rml.h"
#include "orte/mca/rml/rml_types.h"
#include "orte/util/name_fns.h"
#include "orte/runtime/orte_globals.h"
#include "orte/runtime/orte_wait.h"

#include "orte/mca/filem/filem.h"
#include "orte/mca/filem/base/base.h"

/*
 * Functions to process some FileM specific commands
 */
static void filem_base_process_get_proc_node_name_cmd(orte_process_name_t* sender,
                                                      opal_buffer_t* buffer);
static void filem_base_process_get_remote_path_cmd(orte_process_name_t* sender,
                                                   opal_buffer_t* buffer);

static bool recv_issued=false;

int orte_filem_base_comm_start(void)
{
    int rc;

    /* Only active in HNP and daemons */
    if( !ORTE_PROC_IS_HNP && !ORTE_PROC_IS_DAEMON ) {
        return ORTE_SUCCESS;
    }
    if ( recv_issued ) {
        return ORTE_SUCCESS;
    }
    
    OPAL_OUTPUT_VERBOSE((5, orte_filem_base_output,
                         "%s filem:base: Receive: Start command recv",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    if (ORTE_SUCCESS != (rc = orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD,
                                                      ORTE_RML_TAG_FILEM_BASE,
                                                      ORTE_RML_PERSISTENT,
                                                      orte_filem_base_recv,
                                                      NULL))) {
        ORTE_ERROR_LOG(rc);
    }

    recv_issued = true;
    
    return rc;
}


int orte_filem_base_comm_stop(void)
{
    int rc;

    /* Only active in HNP and daemons */
    if( !ORTE_PROC_IS_HNP && !ORTE_PROC_IS_DAEMON ) {
        return ORTE_SUCCESS;
    }
    if ( recv_issued ) {
        return ORTE_SUCCESS;
    }
    
    OPAL_OUTPUT_VERBOSE((5, orte_filem_base_output,
                         "%s filem:base:receive stop comm",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    
    if (ORTE_SUCCESS != (rc = orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORTE_RML_TAG_FILEM_BASE))) {
        ORTE_ERROR_LOG(rc);
    }
    recv_issued = false;
    
    return rc;
}


/*
 * handle message from proxies
 * NOTE: The incoming buffer "buffer" is OBJ_RELEASED by the calling program.
 * DO NOT RELEASE THIS BUFFER IN THIS CODE
 */
void orte_filem_base_recv(int status, orte_process_name_t* sender,
                        opal_buffer_t* buffer, orte_rml_tag_t tag,
                        void* cbdata)
{
    orte_filem_cmd_flag_t command;
    orte_std_cntr_t count;
    int rc;

    OPAL_OUTPUT_VERBOSE((5, orte_filem_base_output,
                         "%s filem:base: Receive a command message.",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    count = 1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(buffer, &command, &count, ORTE_FILEM_CMD))) {
        ORTE_ERROR_LOG(rc);
        return;
    }
    
    switch (command) {
        case ORTE_FILEM_GET_PROC_NODE_NAME_CMD:
            OPAL_OUTPUT_VERBOSE((10, orte_filem_base_output,
                                 "%s filem:base: Command: Get Proc node name command",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

            filem_base_process_get_proc_node_name_cmd(sender, buffer);
            break;

        case ORTE_FILEM_GET_REMOTE_PATH_CMD:
            OPAL_OUTPUT_VERBOSE((10, orte_filem_base_output,
                                 "%s filem:base: Command: Get remote path command",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

            filem_base_process_get_remote_path_cmd(sender, buffer);
            break;
 
        default:
            ORTE_ERROR_LOG(ORTE_ERR_VALUE_OUT_OF_BOUNDS);
    }
}

static void filem_base_process_get_proc_node_name_cmd(orte_process_name_t* sender,
                                                      opal_buffer_t* buffer)
{
    opal_buffer_t answer;
    orte_std_cntr_t count;
    orte_job_t *jdata = NULL;
    orte_proc_t **procs = NULL;
    orte_process_name_t name;
    int rc;

    OBJ_CONSTRUCT(&answer, opal_buffer_t);

    /*
     * Unpack the data
     */
    count = 1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(buffer, &name, &count, ORTE_NAME))) {
        ORTE_ERROR_LOG(rc);
        goto CLEANUP;
    }

    /*
     * Process the data
     */
    /* get the job data object for this proc */
    if (NULL == (jdata = orte_get_job_data_object(name.jobid))) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        ORTE_UPDATE_EXIT_STATUS(1);
        orte_trigger_event(&orte_exit);
        goto CLEANUP;
    }
    /* get the proc object for it */
    procs = (orte_proc_t**)jdata->procs->addr;
    if (NULL == procs[name.vpid] || NULL == procs[name.vpid]->node) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        ORTE_UPDATE_EXIT_STATUS(1);
        orte_trigger_event(&orte_exit);
        goto CLEANUP;
    }

    /*
     * Send back the answer
     */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(&answer, &(procs[name.vpid]->node->name), 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        ORTE_UPDATE_EXIT_STATUS(1);
        orte_trigger_event(&orte_exit);
        goto CLEANUP;
    }

    if (0 > (rc = orte_rml.send_buffer(sender, &answer, ORTE_RML_TAG_FILEM_BASE_RESP, 0))) {
        ORTE_ERROR_LOG(rc);
    }

 CLEANUP:
    OBJ_DESTRUCT(&answer);

}

/*
 * This function is responsible for:
 * - Constructing the remote absolute path for the specified file/dir
 * - Verify the existence of the file/dir
 * - Determine if the specified file/dir is in fact a file or dir or unknown if not found.
 */
static void filem_base_process_get_remote_path_cmd(orte_process_name_t* sender,
                                                   opal_buffer_t* buffer)
{
    opal_buffer_t answer;
    orte_std_cntr_t count;
    char *filename = NULL;
    char *tmp_name = NULL;
    char cwd[OPAL_PATH_MAX];
    int file_type = ORTE_FILEM_TYPE_UNKNOWN;
    struct stat file_status;
    int rc;

    /*
     * Unpack the data
     */
    OBJ_CONSTRUCT(&answer, opal_buffer_t);

    count = 1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(buffer, &filename, &count, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        goto CLEANUP;
    }

    /*
     * Determine the absolute path of the file
     */
    if(filename[0] != '/') { /* if it is not an absolute path already */
        getcwd(cwd, sizeof(cwd));
        asprintf(&tmp_name, "%s/%s", cwd, filename);
    }
    else {
        tmp_name = strdup(filename);
    }

    opal_output_verbose(10, orte_filem_base_output,
                        "filem:base: process_get_remote_path_cmd: %s -> %s: Filename Requested (%s) translated to (%s)",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(sender),
                        filename, tmp_name);

    /*
     * Determine if the file/dir exists at that absolute path
     * Determine if the file/dir is a file or a directory
     */
    if(0 != (rc = stat(tmp_name, &file_status) ) ){
        file_type = ORTE_FILEM_TYPE_UNKNOWN;
    }
    else {
        /* Is it a directory? */
        if(S_ISDIR(file_status.st_mode)) {
            file_type = ORTE_FILEM_TYPE_DIR;
        }
        else if(S_ISREG(file_status.st_mode)) {
            file_type = ORTE_FILEM_TYPE_FILE;
        }
    }

    /*
     * Pack up the response
     * Send back the reference type
     * - ORTE_FILEM_TYPE_FILE    = File
     * - ORTE_FILEM_TYPE_DIR     = Directory
     * - ORTE_FILEM_TYPE_UNKNOWN = Could not be determined, or does not exist
     */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(&answer, &tmp_name, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        ORTE_UPDATE_EXIT_STATUS(1);
        orte_trigger_event(&orte_exit);
        goto CLEANUP;
    }
    if (ORTE_SUCCESS != (rc = opal_dss.pack(&answer, &file_type, 1, OPAL_INT))) {
        ORTE_ERROR_LOG(rc);
        ORTE_UPDATE_EXIT_STATUS(1);
        orte_trigger_event(&orte_exit);
        goto CLEANUP;
    }

    if (0 > (rc = orte_rml.send_buffer(sender, &answer, ORTE_RML_TAG_FILEM_BASE_RESP, 0))) {
        ORTE_ERROR_LOG(rc);
    }

 CLEANUP:
    if( NULL != filename) {
        free(filename);
        filename = NULL;
    }
    if( NULL != tmp_name) {
        free(tmp_name);
        tmp_name = NULL;
    }

    OBJ_DESTRUCT(&answer);
}
