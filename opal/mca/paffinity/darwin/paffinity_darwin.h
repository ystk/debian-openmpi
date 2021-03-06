/*
 * Copyright (c) 2004-2008 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007      Cisco, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#ifndef MCA_PAFFINITY_DARWIN_EXPORT_H
#define MCA_PAFFINITY_DARWIN_EXPORT_H

#include "opal_config.h"

#include "opal/mca/mca.h"
#include "opal/mca/paffinity/paffinity.h"

BEGIN_C_DECLS

/*
 * Globally exported variable
 */

OPAL_DECLSPEC extern const opal_paffinity_base_component_2_0_0_t mca_paffinity_darwin_component;

/* query function */
int opal_paffinity_darwin_component_query(mca_base_module_t **module, int *priority);

END_C_DECLS

#endif /* MCA_PAFFINITY_DARWIN_EXPORT_H */
