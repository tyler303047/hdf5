/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/****************/
/* Module Setup */
/****************/

#include "H5Dmodule.h" /* This source code file is part of the H5D module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5CXprivate.h" /* API Contexts                             */
#include "H5Dpkg.h"      /* Datasets                                 */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5ESprivate.h" /* Event Sets                               */
#include "H5FLprivate.h" /* Free lists                               */
#include "H5Iprivate.h"  /* IDs                                      */
#include "H5VLprivate.h" /* Virtual Object Layer                     */

#include "H5VLnative_private.h" /* Native VOL connector                     */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Local Prototypes */
/********************/

/* Helper routines for sync/async API calls */
static hid_t  H5D__create_api_common(hid_t loc_id, const char *name, hid_t type_id, hid_t space_id,
                                     hid_t lcpl_id, hid_t dcpl_id, hid_t dapl_id, hid_t es_id,
                                     const char *caller);
static herr_t H5D__read_api_common(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id,
                                   hid_t dxpl_id, void *buf, hid_t es_id, const char *caller);
static herr_t H5D__write_api_common(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id,
                                    hid_t dxpl_id, const void *buf, hid_t es_id, const char *caller);
static herr_t H5D__close_api_common(hid_t dset_id, hid_t es_id, const char *caller);

/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
hbool_t H5_PKG_INIT_VAR = FALSE;

/*****************************/
/* Library Private Variables */
/*****************************/

/* Declare extern the free list to manage blocks of type conversion data */
H5FL_BLK_EXTERN(type_conv);

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5D__create_api_common
 *
 * Purpose:     This is the common function for creating HDF5 datasets.
 *
 * Return:      Success:    A dataset ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
static hid_t
H5D__create_api_common(hid_t loc_id, const char *name, hid_t type_id, hid_t space_id, hid_t lcpl_id,
                       hid_t dcpl_id, hid_t dapl_id, hid_t es_id, const char *caller)
{
    void *            dset      = NULL;              /* New dataset's info */
    H5ES_t *          es        = NULL;              /* Event set for the operation              */
    void *            token     = NULL, **token_ptr; /* Request token for async operation        */
    H5VL_object_t *   token_obj = NULL;              /* Async token VOL object */
    H5VL_object_t *   vol_obj   = NULL;              /* object of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t             ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_STATIC

    /* Check arguments */
    if (!name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "name parameter cannot be NULL")
    if (!*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "name parameter cannot be an empty string")

    /* Get link creation property list */
    if (H5P_DEFAULT == lcpl_id)
        lcpl_id = H5P_LINK_CREATE_DEFAULT;
    else if (TRUE != H5P_isa_class(lcpl_id, H5P_LINK_CREATE))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "lcpl_id is not a link creation property list")

    /* Get dataset creation property list */
    if (H5P_DEFAULT == dcpl_id)
        dcpl_id = H5P_DATASET_CREATE_DEFAULT;
    else if (TRUE != H5P_isa_class(dcpl_id, H5P_DATASET_CREATE))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID,
                    "dcpl_id is not a dataset create property list ID")

    /* Set the DCPL for the API context */
    H5CX_set_dcpl(dcpl_id);

    /* Set the LCPL for the API context */
    H5CX_set_lcpl(lcpl_id);

    /* Verify access property list and set up collective metadata if appropriate */
    if (H5CX_set_apl(&dapl_id, H5P_CLS_DACC, loc_id, TRUE) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, H5I_INVALID_HID, "can't set access property list info")

    /* Get the location object */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid location identifier")

    /* Set location parameters */
    loc_params.type     = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_get_type(loc_id);

    /* Get the event set and set up request token pointer for operation */
    if (H5ES_NONE != es_id) {
        /* Get event set */
        if (NULL == (es = (H5ES_t *)H5I_object_verify(es_id, H5I_EVENTSET)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an event set")

        /* Point at token for operation to set up */
        token_ptr = &token;
    } /* end if */
    else
        /* Synchronous operation */
        token_ptr = H5_REQUEST_NULL;

    /* Create the dataset */
    if (NULL == (dset = H5VL_dataset_create(vol_obj, &loc_params, name, lcpl_id, type_id, space_id, dcpl_id,
                                            dapl_id, H5P_DATASET_XFER_DEFAULT, token_ptr)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, H5I_INVALID_HID, "unable to create dataset")

    /* If there's an event set and a token was created, add the token to it */
    if (H5ES_NONE != es_id && NULL != token) {
        /* Create vol object for token */
        if (NULL == (token_obj = H5VL_create_object(token, vol_obj->connector))) {
            if (H5VL_request_free(token) < 0)
                HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request")
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't create vol object for request token")
        } /* end if */

        /* Add token to event set */
        if (H5ES_insert_new(es, token_obj,
                            H5ARG_TRACE9(caller, "i*siiiiii*s", loc_id, name, type_id, space_id, lcpl_id, dcpl_id, dapl_id, es_id, caller)) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINSERT, FAIL, "can't insert token into event set")
        token_obj = NULL;
    } /* end if */

    /* Get an ID for the dataset */
    if ((ret_value = H5VL_register(H5I_DATASET, dset, vol_obj->connector, TRUE)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to register dataset")

done:
    if (H5I_INVALID_HID == ret_value) {
        if (dset && H5VL_dataset_close(vol_obj, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CLOSEERROR, H5I_INVALID_HID, "unable to release dataset")
        if (token_obj && H5VL_free_object(token_obj) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request token")
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5D__create_api_common() */

/*-------------------------------------------------------------------------
 * Function:    H5Dcreate2
 *
 * Purpose:     Creates a new dataset named NAME at LOC_ID, opens the
 *              dataset for access, and associates with that dataset constant
 *              and initial persistent properties including the type of each
 *              datapoint as stored in the file (TYPE_ID), the size of the
 *              dataset (SPACE_ID), and other initial miscellaneous
 *              properties (DCPL_ID).
 *
 *              All arguments are copied into the dataset, so the caller is
 *              allowed to derive new types, dataspaces, and creation
 *              parameters from the old ones and reuse them in calls to
 *              create other datasets.
 *
 * Return:      Success:    The object ID of the new dataset. At this
 *                          point, the dataset is ready to receive its
 *                          raw data. Attempting to read raw data from
 *                          the dataset will probably return the fill
 *                          value. The dataset should be closed when the
 *                          caller is no longer interested in it.
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dcreate2(hid_t loc_id, const char *name, hid_t type_id, hid_t space_id, hid_t lcpl_id, hid_t dcpl_id,
           hid_t dapl_id)
{
    hid_t ret_value; /* Return value */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE7("i", "i*siiiii", loc_id, name, type_id, space_id, lcpl_id, dcpl_id, dapl_id);

    /* Create the dataset synchronously */
    if ((ret_value = H5D__create_api_common(loc_id, name, type_id, space_id, lcpl_id, dcpl_id, dapl_id,
                                            H5ES_NONE, FUNC)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, H5I_INVALID_HID, "unable to synchronously create dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dcreate2() */

/*-------------------------------------------------------------------------
 * Function:    H5Dcreate_async
 *
 * Purpose:     Asynchronous version of H5Dcreate
 *
 * Return:      Success:    A dataset ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dcreate_async(hid_t loc_id, const char *name, hid_t type_id, hid_t space_id, hid_t lcpl_id, hid_t dcpl_id,
                hid_t dapl_id, hid_t es_id)
{
    hid_t ret_value; /* Return value */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE8("i", "i*siiiiii", loc_id, name, type_id, space_id, lcpl_id, dcpl_id, dapl_id, es_id);

    /* Create the dataset asynchronously */
    if ((ret_value = H5D__create_api_common(loc_id, name, type_id, space_id, lcpl_id, dcpl_id, dapl_id, es_id,
                                            FUNC)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, H5I_INVALID_HID, "unable to asynchronously create dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dcreate_async() */

/*-------------------------------------------------------------------------
 * Function:    H5Dcreate_anon
 *
 * Purpose:     Creates a new dataset named NAME at LOC_ID, opens the
 *              dataset for access, and associates with that dataset constant
 *              and initial persistent properties including the type of each
 *              datapoint as stored in the file (TYPE_ID), the size of the
 *              dataset (SPACE_ID), and other initial miscellaneous
 *              properties (DCPL_ID).
 *
 *              All arguments are copied into the dataset, so the caller is
 *              allowed to derive new types, dataspaces, and creation
 *              parameters from the old ones and reuse them in calls to
 *              create other datasets.
 *
 *              The resulting ID should be linked into the file with
 *              H5Olink or it will be deleted when closed.
 *
 * Return:      Success:    The object ID of the new dataset. At this
 *                          point, the dataset is ready to receive its
 *                          raw data. Attempting to read raw data from
 *                          the dataset will probably return the fill
 *                          value. The dataset should be linked into
 *                          the group hierarchy before being closed or
 *                          it will be deleted. The dataset should be
 *                          closed when the caller is no longer interested
 *                          in it.
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dcreate_anon(hid_t loc_id, hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id)
{
    void *            dset    = NULL; /* dset object from VOL connector */
    H5VL_object_t *   vol_obj = NULL; /* object of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t             ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE5("i", "iiiii", loc_id, type_id, space_id, dcpl_id, dapl_id);

    /* Check arguments */
    if (H5P_DEFAULT == dcpl_id)
        dcpl_id = H5P_DATASET_CREATE_DEFAULT;
    else if (TRUE != H5P_isa_class(dcpl_id, H5P_DATASET_CREATE))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "not dataset create property list ID")

    /* Set the DCPL for the API context */
    H5CX_set_dcpl(dcpl_id);

    /* Verify access property list and set up collective metadata if appropriate */
    if (H5CX_set_apl(&dapl_id, H5P_CLS_DACC, loc_id, TRUE) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, H5I_INVALID_HID, "can't set access property list info")

    /* get the location object */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid location identifier")

    /* Set location parameters */
    loc_params.type     = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_get_type(loc_id);

    /* Create the dataset */
    if (NULL ==
        (dset = H5VL_dataset_create(vol_obj, &loc_params, NULL, H5P_LINK_CREATE_DEFAULT, type_id, space_id,
                                    dcpl_id, dapl_id, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, H5I_INVALID_HID, "unable to create dataset")

    /* Get an atom for the dataset */
    if ((ret_value = H5VL_register(H5I_DATASET, dset, vol_obj->connector, TRUE)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to register dataset")

done:
    /* Cleanup on failure */
    if (H5I_INVALID_HID == ret_value)
        if (dset && H5VL_dataset_close(vol_obj, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CLOSEERROR, H5I_INVALID_HID, "unable to release dataset")

    FUNC_LEAVE_API(ret_value)
} /* end H5Dcreate_anon() */

/*-------------------------------------------------------------------------
 * Function:    H5Dopen2
 *
 * Purpose:     Finds a dataset named NAME at LOC_ID, opens it, and returns
 *              its ID. The dataset should be close when the caller is no
 *              longer interested in it.
 *
 *              Takes a dataset access property list
 *
 * Return:      Success:    Object ID of the dataset
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dopen2(hid_t loc_id, const char *name, hid_t dapl_id)
{
    void *            dset    = NULL; /* dset object from VOL connector */
    H5VL_object_t *   vol_obj = NULL; /* object of loc_id */
    H5VL_loc_params_t loc_params;
    hid_t             ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE3("i", "i*si", loc_id, name, dapl_id);

    /* Check args */
    if (!name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "name parameter cannot be NULL")
    if (!*name)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "name parameter cannot be an empty string")

    /* Verify access property list and set up collective metadata if appropriate */
    if (H5CX_set_apl(&dapl_id, H5P_CLS_DACC, loc_id, FALSE) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, H5I_INVALID_HID, "can't set access property list info")

    /* get the location object */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid location identifier")

    /* Set the location parameters */
    loc_params.type     = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_get_type(loc_id);

    /* Open the dataset */
    if (NULL == (dset = H5VL_dataset_open(vol_obj, &loc_params, name, dapl_id, H5P_DATASET_XFER_DEFAULT,
                                          H5_REQUEST_NULL)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, H5I_INVALID_HID, "unable to open dataset")

    /* Register an atom for the dataset */
    if ((ret_value = H5VL_register(H5I_DATASET, dset, vol_obj->connector, TRUE)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTREGISTER, H5I_INVALID_HID, "can't register dataset atom")

done:
    if (H5I_INVALID_HID == ret_value)
        if (dset && H5VL_dataset_close(vol_obj, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CLOSEERROR, H5I_INVALID_HID, "unable to release dataset")

    FUNC_LEAVE_API(ret_value)
} /* end H5Dopen2() */

/*-------------------------------------------------------------------------
 * Function:    H5D__close_api_common
 *
 * Purpose:     This is the common function for closing HDF5 datasets.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5D__close_api_common(hid_t dset_id, hid_t es_id, const char *caller)
{
    H5ES_t *       es        = NULL;              /* Event set for the operation */
    void *         token     = NULL, **token_ptr; /* Request token for async operation */
    H5VL_object_t *token_obj = NULL;              /* Async token VOL object */
    H5VL_t *       connector = NULL;              /* VOL connector */
    herr_t         ret_value = SUCCEED;           /* Return value */

    FUNC_ENTER_STATIC

    /* Check args */
    if (H5I_DATASET != H5I_get_type(dset_id))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataset ID")

    /* Get the event set and set up request token pointer for operation */
    if (H5ES_NONE != es_id) {
        H5VL_object_t *vol_obj = NULL;

        /* Get event set */
        if (NULL == (es = (H5ES_t *)H5I_object_verify(es_id, H5I_EVENTSET)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an event set")

        /* Get dataset object's connector and increate its rc, so it doesn't get
         * closed if closing the dataset closes the file */
        if (NULL == (vol_obj = H5VL_vol_object(dset_id)))
            HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get VOL object for dataset")
        connector = vol_obj->connector;
        H5VL_conn_inc_rc(connector);

        /* Point at token for operation to set up */
        token_ptr = &token;
    } /* end if */
    else
        /* Synchronous operation */
        token_ptr = H5_REQUEST_NULL;

    /* Decrement the counter on the dataset.  It will be freed if the count
     * reaches zero.
     */
    if (H5I_dec_app_ref_always_close_async(dset_id, token_ptr) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "can't decrement count on dataset ID")

    /* If there's an event set and a token was created, add the token to it */
    if (H5ES_NONE != es_id && NULL != token) {
        /* Create vol object for token */
        if (NULL == (token_obj = H5VL_create_object(token, connector))) {
            if (H5VL_request_free(token) < 0)
                HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request")
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't create vol object for request token")
        } /* end if */

        /* Add token to event set */
        if (H5ES_insert_new(es, token_obj, H5ARG_TRACE3(caller, "ii*s", dset_id, es_id, caller)) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINSERT, FAIL, "can't insert token into event set")

        if (H5VL_conn_dec_rc(connector) < 0) {
            connector = NULL;
            HGOTO_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "can't decrement ref count on connector")
        } /* end if */
        connector = NULL;
    } /* end if */

done:
    if (ret_value < 0) {
        if (token_obj && H5VL_free_object(token_obj) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request token")
        if (connector && H5VL_conn_dec_rc(connector) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "can't decrement ref count on connector")
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5D__close_api_common() */

/*-------------------------------------------------------------------------
 * Function:    H5Dclose
 *
 * Purpose:     Closes access to a dataset and releases resources used by
 *              it. It is illegal to subsequently use that same dataset
 *              ID in calls to other dataset functions.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dclose(hid_t dset_id)
{
    herr_t ret_value = SUCCEED; /* Return value                     */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", dset_id);

    /* Synchronously close the dataset ID */
    if (H5D__close_api_common(dset_id, H5ES_NONE, FUNC) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, FAIL, "synchronous dataset close failed")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dclose() */

/*-------------------------------------------------------------------------
 * Function:    H5Dclose_async
 *
 * Purpose:     Asynchronous version of H5Dclose
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dclose_async(hid_t dset_id, hid_t es_id)
{
    herr_t ret_value = SUCCEED; /* Return value                     */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ii", dset_id, es_id);

    /* Asynchronously close the dataset ID */
    if (H5D__close_api_common(dset_id, es_id, FUNC) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, FAIL, "asynchronous dataset close failed")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dclose_async() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_space
 *
 * Purpose:     Returns a copy of the file dataspace for a dataset.
 *
 * Return:      Success:    ID for a copy of the dataspace.  The data
 *                          space should be released by calling
 *                          H5Sclose().
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dget_space(hid_t dset_id)
{
    H5VL_object_t *vol_obj   = NULL;            /* Dataset structure    */
    hid_t          ret_value = H5I_INVALID_HID; /* Return value         */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE1("i", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid dataset identifier")

    /* Get the dataspace */
    if (H5VL_dataset_get(vol_obj, H5VL_DATASET_GET_SPACE, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL,
                         &ret_value) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, H5I_INVALID_HID, "unable to get dataspace")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_space() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_space_status
 *
 * Purpose:     Returns the status of dataspace allocation.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dget_space_status(hid_t dset_id, H5D_space_status_t *allocation /*out*/)
{
    H5VL_object_t *vol_obj   = NULL;    /* Dataset structure    */
    herr_t         ret_value = SUCCEED; /* Return value         */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ix", dset_id, allocation);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataset identifier")

    /* Get dataspace status */
    if ((ret_value = H5VL_dataset_get(vol_obj, H5VL_DATASET_GET_SPACE_STATUS, H5P_DATASET_XFER_DEFAULT,
                                      H5_REQUEST_NULL, allocation)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "unable to get space status")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Dget_space_status() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_type
 *
 * Purpose:     Returns a copy of the file datatype for a dataset.
 *
 * Return:      Success:    ID for a copy of the datatype. The data
 *                          type should be released by calling
 *                          H5Tclose().
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dget_type(hid_t dset_id)
{
    H5VL_object_t *vol_obj;                     /* Dataset structure    */
    hid_t          ret_value = H5I_INVALID_HID; /* Return value         */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE1("i", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid dataset identifier")

    /* Get the datatype */
    if (H5VL_dataset_get(vol_obj, H5VL_DATASET_GET_TYPE, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL,
                         &ret_value) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, H5I_INVALID_HID, "unable to get datatype")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_type() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_create_plist
 *
 * Purpose:     Returns a copy of the dataset creation property list.
 *
 * Return:      Success:    ID for a copy of the dataset creation
 *                          property list.  The template should be
 *                          released by calling H5P_close().
 *
 *              Failure:    H5I_INVALID_HID
 *
 * Programmer:  Robb Matzke
 *              Tuesday, February  3, 1998
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dget_create_plist(hid_t dset_id)
{
    H5VL_object_t *vol_obj;                     /* Dataset structure    */
    hid_t          ret_value = H5I_INVALID_HID; /* Return value         */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE1("i", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid dataset identifier")

    /* Get the dataset creation property list */
    if (H5VL_dataset_get(vol_obj, H5VL_DATASET_GET_DCPL, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL,
                         &ret_value) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, H5I_INVALID_HID, "unable to get dataset creation properties")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_create_plist() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_access_plist
 *
 * Purpose:     Returns a copy of the dataset access property list.
 *
 * Description: H5Dget_access_plist returns the dataset access property
 *              list identifier of the specified dataset.
 *
 *              The chunk cache parameters in the returned property lists will be
 *              those used by the dataset.  If the properties in the file access
 *              property list were used to determine the dataset’s chunk cache
 *              configuration, then those properties will be present in the
 *              returned dataset access property list.  If the dataset does not
 *              use a chunked layout, then the chunk cache properties will be set
 *              to the default.  The chunk cache properties in the returned list
 *              are considered to be “set”, and any use of this list will override
 *              the corresponding properties in the file’s file access property
 *              list.
 *
 *              All link access properties in the returned list will be set to the
 *              default values.
 *
 * Return:      Success:    ID for a copy of the dataset access
 *                          property list.  The template should be
 *                          released by calling H5Pclose().
 *
 *              Failure:    H5I_INVALID_HID
 *
 * Programmer:  Neil Fortner
 *              Wednesday, October 29, 2008
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Dget_access_plist(hid_t dset_id)
{
    H5VL_object_t *vol_obj;                     /* Dataset structure    */
    hid_t          ret_value = H5I_INVALID_HID; /* Return value         */

    FUNC_ENTER_API(H5I_INVALID_HID)
    H5TRACE1("i", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, H5I_INVALID_HID, "invalid dataset identifier")

    /* Get the dataset access property list */
    if (H5VL_dataset_get(vol_obj, H5VL_DATASET_GET_DAPL, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL,
                         &ret_value) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, H5I_INVALID_HID, "unable to get dataset access properties")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_access_plist() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_storage_size
 *
 * Purpose:     Returns the amount of storage that is required for the
 *              dataset. For chunked datasets this is the number of allocated
 *              chunks times the chunk size.
 *
 * Return:      Success:    The amount of storage space allocated for the
 *                          dataset, not counting meta data. The return
 *                          value may be zero if no data has been stored.
 *
 *              Failure:    Zero
 *
 *-------------------------------------------------------------------------
 */
hsize_t
H5Dget_storage_size(hid_t dset_id)
{
    H5VL_object_t *vol_obj;       /* Dataset for this operation   */
    hsize_t        ret_value = 0; /* Return value                 */

    FUNC_ENTER_API(0)
    H5TRACE1("h", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, 0, "invalid dataset identifier")

    /* Get the storage size */
    if (H5VL_dataset_get(vol_obj, H5VL_DATASET_GET_STORAGE_SIZE, H5P_DATASET_XFER_DEFAULT, H5_REQUEST_NULL,
                         &ret_value) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, 0, "unable to get storage size")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_storage_size() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_offset
 *
 * Purpose:     Returns the address of dataset in file.
 *
 * Return:      Success:    The address of dataset
 *
 *              Failure:    HADDR_UNDEF (can also be a valid return value!)
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5Dget_offset(hid_t dset_id)
{
    H5VL_object_t *vol_obj;                 /* Dataset for this operation   */
    haddr_t        ret_value = HADDR_UNDEF; /* Return value                 */

    FUNC_ENTER_API(HADDR_UNDEF)
    H5TRACE1("a", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, HADDR_UNDEF, "invalid dataset identifier")

    /* Get the offset */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_OFFSET, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, &ret_value) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, HADDR_UNDEF, "unable to get offset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_offset() */

/*-------------------------------------------------------------------------
 * Function:    H5D__read_api_common
 *
 * Purpose:     Common helper routine for sync/async dataset read operations.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5D__read_api_common(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id,
                     void *buf, hid_t es_id, const char *caller)
{
    H5VL_object_t *vol_obj   = NULL;              /* Dataset VOL object */
    H5ES_t *       es        = NULL;              /* Event set for the operation */
    void *         token     = NULL, **token_ptr; /* Request token for async operation */
    H5VL_object_t *token_obj = NULL;              /* Async token VOL object */
    herr_t         ret_value = SUCCEED;           /* Return value */

    FUNC_ENTER_STATIC

    /* Check arguments */
    if (mem_space_id < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid memory dataspace ID")
    if (file_space_id < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid file dataspace ID")

    /* Get dataset pointer */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id is not a dataset ID")

    /* Get the default dataset transfer property list if the user didn't provide one */
    if (H5P_DEFAULT == dxpl_id)
        dxpl_id = H5P_DATASET_XFER_DEFAULT;
    else if (TRUE != H5P_isa_class(dxpl_id, H5P_DATASET_XFER))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not xfer parms")

    /* Get the event set and set up request token pointer for operation */
    if (H5ES_NONE != es_id) {
        /* Get event set */
        if (NULL == (es = (H5ES_t *)H5I_object_verify(es_id, H5I_EVENTSET)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an event set")

        /* Point at token for operation to set up */
        token_ptr = &token;
    } /* end if */
    else
        /* Synchronous operation */
        token_ptr = H5_REQUEST_NULL;

    /* Read the data */
    if (H5VL_dataset_read(vol_obj, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, token_ptr) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't read data")

    /* If there's an event set and a token was created, add the token to it */
    if (H5ES_NONE != es_id && NULL != token) {
        /* Create vol object for token */
        if (NULL == (token_obj = H5VL_create_object(token, vol_obj->connector))) {
            if (H5VL_request_free(token) < 0)
                HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request")
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't create vol object for request token")
        } /* end if */

        /* Add token to event set */
        if (H5ES_insert_new(es, token_obj,
                            H5ARG_TRACE8(caller, "iiiii*xi*s", dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, es_id, caller)) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINSERT, FAIL, "can't insert token into event set")
    } /* end if */

done:
    if (ret_value < 0 && token_obj)
        if (H5VL_free_object(token_obj) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request token")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5D__read_api_common() */

/*-------------------------------------------------------------------------
 * Function:    H5Dread
 *
 * Purpose:     Reads (part of) a DSET from the file into application
 *              memory BUF. The part of the dataset to read is defined with
 *              MEM_SPACE_ID and FILE_SPACE_ID. The data points are
 *              converted from their file type to the MEM_TYPE_ID specified.
 *              Additional miscellaneous data transfer properties can be
 *              passed to this function with the PLIST_ID argument.
 *
 *              The FILE_SPACE_ID can be the constant H5S_ALL which indicates
 *              that the entire file dataspace is to be referenced.
 *
 *              The MEM_SPACE_ID can be the constant H5S_ALL in which case
 *              the memory dataspace is the same as the file dataspace
 *              defined when the dataset was created.
 *
 *              The number of elements in the memory dataspace must match
 *              the number of elements in the file dataspace.
 *
 *              The PLIST_ID can be the constant H5P_DEFAULT in which
 *              case the default data transfer properties are used.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Robb Matzke
 *              Thursday, December 4, 1997
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dread(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id,
        void *buf /*out*/)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "iiiiix", dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf);

    /* Read the data */
    if (H5D__read_api_common(dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, H5ES_NONE,
                             FUNC) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't synchronously read data")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dread() */

/*-------------------------------------------------------------------------
 * Function:    H5Dread_async
 *
 * Purpose:     Asynchronously read dataset elements.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Houjun Tang
 *              Oct 15, 2019
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dread_async(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id,
              void *buf /*out*/, hid_t es_id)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "iiiiixi", dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, es_id);

    /* Read the data */
    if (H5D__read_api_common(dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, es_id, FUNC) <
        0)
        HGOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't asynchronously read data")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dread_async() */

/*-------------------------------------------------------------------------
 * Function:    H5Dread_chunk
 *
 * Purpose:     Reads an entire chunk from the file directly.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Matthew Strong (GE Healthcare)
 *              14 February 2016
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5Dread_chunk(hid_t dset_id, hid_t dxpl_id, const hsize_t *offset, uint32_t *filters, void *buf)
{
    H5VL_object_t *vol_obj   = NULL;
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "ii*h*Iu*x", dset_id, dxpl_id, offset, filters, buf);

    /* Check arguments */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id is not a dataset ID")
    if (!buf)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buf cannot be NULL")
    if (!offset)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offset cannot be NULL")
    if (!filters)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "filters cannot be NULL")

    /* Get the default dataset transfer property list if the user didn't provide one */
    if (H5P_DEFAULT == dxpl_id)
        dxpl_id = H5P_DATASET_XFER_DEFAULT;
    else if (TRUE != H5P_isa_class(dxpl_id, H5P_DATASET_XFER))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dxpl_id is not a dataset transfer property list ID")

    /* Read the raw chunk */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_CHUNK_READ, dxpl_id, H5_REQUEST_NULL, offset,
                              filters, buf) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't read unprocessed chunk data")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dread_chunk() */

/*-------------------------------------------------------------------------
 * Function:    H5D__write_api_common
 *
 * Purpose:     Common helper routine for sync/async dataset write operations.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5D__write_api_common(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id,
                      hid_t dxpl_id, const void *buf, hid_t es_id, const char *caller)
{
    H5ES_t *       es        = NULL;              /* Event set for the operation */
    void *         token     = NULL, **token_ptr; /* Request token for async operation */
    H5VL_object_t *token_obj = NULL;              /* Async token VOL object */
    H5VL_object_t *vol_obj   = NULL;              /* Dataset VOL object */
    herr_t         ret_value = SUCCEED;           /* Return value */

    FUNC_ENTER_STATIC

    /* Check arguments */
    if (mem_space_id < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid memory dataspace ID")
    if (file_space_id < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid file dataspace ID")

    /* Get dataset pointer */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id is not a dataset ID")

    /* Get the default dataset transfer property list if the user didn't provide one */
    if (H5P_DEFAULT == dxpl_id)
        dxpl_id = H5P_DATASET_XFER_DEFAULT;
    else if (TRUE != H5P_isa_class(dxpl_id, H5P_DATASET_XFER))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not xfer parms")

    /* Get the event set and set up request token pointer for operation */
    if (H5ES_NONE != es_id) {
        /* Get event set */
        if (NULL == (es = (H5ES_t *)H5I_object_verify(es_id, H5I_EVENTSET)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an event set")

        /* Point at token for operation to set up */
        token_ptr = &token;
    } /* end if */
    else
        /* Synchronous operation */
        token_ptr = H5_REQUEST_NULL;

    /* Write the data */
    if (H5VL_dataset_write(vol_obj, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, token_ptr) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "can't write data")

    /* If there's an event set and a token was created, add the token to it */
    if (H5ES_NONE != es_id && NULL != token) {
        /* Create vol object for token */
        if (NULL == (token_obj = H5VL_create_object(token, vol_obj->connector))) {
            if (H5VL_request_free(token) < 0)
                HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request")
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't create vol object for request token")
        } /* end if */

        /* Add token to event set */
        if (H5ES_insert_new(es, token_obj,
                            H5ARG_TRACE8(caller, "iiiii*xi*s", dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, es_id, caller)) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINSERT, FAIL, "can't insert token into event set")
    } /* end if */

done:
    if (ret_value < 0 && token_obj)
        if (H5VL_free_object(token_obj) < 0)
            HDONE_ERROR(H5E_DATASET, H5E_CANTFREE, FAIL, "can't free request token")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5D__write_api_common() */

/*-------------------------------------------------------------------------
 * Function:    H5Dwrite
 *
 * Purpose:     Writes (part of) a DSET from application memory BUF to the
 *              file. The part of the dataset to write is defined with the
 *              MEM_SPACE_ID and FILE_SPACE_ID arguments. The data points
 *              are converted from their current type (MEM_TYPE_ID) to their
 *              file datatype. Additional miscellaneous data transfer
 *              properties can be passed to this function with the
 *              PLIST_ID argument.
 *
 *              The FILE_SPACE_ID can be the constant H5S_ALL which indicates
 *              that the entire file dataspace is to be referenced.
 *
 *              The MEM_SPACE_ID can be the constant H5S_ALL in which case
 *              the memory dataspace is the same as the file dataspace
 *              defined when the dataset was created.
 *
 *              The number of elements in the memory dataspace must match
 *              the number of elements in the file dataspace.
 *
 *              The PLIST_ID can be the constant H5P_DEFAULT in which
 *              case the default data transfer properties are used.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:  Robb Matzke
 *              Thursday, December 4, 1997
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dwrite(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id,
         const void *buf)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "iiiii*x", dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf);

    /* Write the data */
    if (H5D__write_api_common(dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, H5ES_NONE,
                              FUNC) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "can't synchronously write data")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dwrite() */

/*-------------------------------------------------------------------------
 * Function:    H5Dwrite_async
 *
 * Purpose:     For asynchronous VOL with request token
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Houjun Tang
 *              Oct 15, 2019
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dwrite_async(hid_t dset_id, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id,
               const void *buf, hid_t es_id)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "iiiii*xi", dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, es_id);

    /* Write the data */
    if (H5D__write_api_common(dset_id, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, es_id, FUNC) <
        0)
        HGOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "can't asynchronously write data")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dwrite_async() */

/*-------------------------------------------------------------------------
 * Function:    H5Dwrite_chunk
 *
 * Purpose:     Writes an entire chunk to the file directly.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 * Programmer:	Raymond Lu
 *		        30 July 2012
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dwrite_chunk(hid_t dset_id, hid_t dxpl_id, uint32_t filters, const hsize_t *offset, size_t data_size,
               const void *buf)
{
    H5VL_object_t *vol_obj = NULL;
    uint32_t       data_size_32;        /* Chunk data size (limited to 32-bits currently) */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "iiIu*hz*x", dset_id, dxpl_id, filters, offset, data_size, buf);

    /* Check arguments */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataset ID")
    if (!buf)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "buf cannot be NULL")
    if (!offset)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offset cannot be NULL")
    if (0 == data_size)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "data_size cannot be zero")

    /* Make sure data size is less than 4 GiB */
    data_size_32 = (uint32_t)data_size;
    if (data_size != (size_t)data_size_32)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid data_size - chunks cannot be > 4 GiB")

    /* Get the default dataset transfer property list if the user didn't provide one */
    if (H5P_DEFAULT == dxpl_id)
        dxpl_id = H5P_DATASET_XFER_DEFAULT;
    else if (TRUE != H5P_isa_class(dxpl_id, H5P_DATASET_XFER))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dxpl_id is not a dataset transfer property list ID")

    /* Write chunk */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_CHUNK_WRITE, dxpl_id, H5_REQUEST_NULL, filters,
                              offset, data_size_32, buf) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "can't write unprocessed chunk data")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dwrite_chunk() */

/*-------------------------------------------------------------------------
 * Function:    H5Diterate
 *
 * Purpose: This routine iterates over all the elements selected in a memory
 *      buffer.  The callback function is called once for each element selected
 *      in the dataspace.  The selection in the dataspace is modified so
 *      that any elements already iterated over are removed from the selection
 *      if the iteration is interrupted (by the H5D_operator_t function
 *      returning non-zero) in the "middle" of the iteration and may be
 *      re-started by the user where it left off.
 *
 *      NOTE: Until "subtracting" elements from a selection is implemented,
 *          the selection is not modified.
 *
 * Parameters:
 *      void *buf;          IN/OUT: Pointer to the buffer in memory containing
 *                              the elements to iterate over.
 *      hid_t type_id;      IN: Datatype ID for the elements stored in BUF.
 *      hid_t space_id;     IN: Dataspace ID for BUF, also contains the
 *                              selection to iterate over.
 *      H5D_operator_t op; IN: Function pointer to the routine to be
 *                              called for each element in BUF iterated over.
 *      void *operator_data;    IN/OUT: Pointer to any user-defined data
 *                              associated with the operation.
 *
 * Operation information:
 *      H5D_operator_t is defined as:
 *          typedef herr_t (*H5D_operator_t)(void *elem, hid_t type_id,
 *              unsigned ndim, const hsize_t *point, void *operator_data);
 *
 *      H5D_operator_t parameters:
 *          void *elem;         IN/OUT: Pointer to the element in memory containing
 *                                  the current point.
 *          hid_t type_id;      IN: Datatype ID for the elements stored in ELEM.
 *          unsigned ndim;       IN: Number of dimensions for POINT array
 *          const hsize_t *point; IN: Array containing the location of the element
 *                                  within the original dataspace.
 *          void *operator_data;    IN/OUT: Pointer to any user-defined data
 *                                  associated with the operation.
 *
 *      The return values from an operator are:
 *          Zero causes the iterator to continue, returning zero when all
 *              elements have been processed.
 *          Positive causes the iterator to immediately return that positive
 *              value, indicating short-circuit success.  The iterator can be
 *              restarted at the next element.
 *          Negative causes the iterator to immediately return that value,
 *              indicating failure. The iterator can be restarted at the next
 *              element.
 *
 * Return:  Returns the return value of the last operator if it was non-zero,
 *          or zero if all elements were processed. Otherwise returns a
 *          negative value.
 *
 * Programmer:  Quincey Koziol
 *              Friday, June 11, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Diterate(void *buf, hid_t type_id, hid_t space_id, H5D_operator_t op, void *operator_data)
{
    H5T_t *           type;      /* Datatype */
    H5S_t *           space;     /* Dataspace for iteration */
    H5S_sel_iter_op_t dset_op;   /* Operator for iteration */
    herr_t            ret_value; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "*xiiDO*x", buf, type_id, space_id, op, operator_data);

    /* Check args */
    if (NULL == op)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid operator")
    if (NULL == buf)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid buffer")
    if (H5I_DATATYPE != H5I_get_type(type_id))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid datatype")
    if (NULL == (type = (H5T_t *)H5I_object_verify(type_id, H5I_DATATYPE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an valid base datatype")
    if (NULL == (space = (H5S_t *)H5I_object_verify(space_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataspace")
    if (!(H5S_has_extent(space)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspace does not have extent set")

    dset_op.op_type          = H5S_SEL_ITER_OP_APP;
    dset_op.u.app_op.op      = op;
    dset_op.u.app_op.type_id = type_id;

    ret_value = H5S_select_iterate(buf, type, space, &dset_op, operator_data);

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Diterate() */

/*-------------------------------------------------------------------------
 * Function:    H5Dvlen_get_buf_size
 *
 * Purpose: This routine checks the number of bytes required to store the VL
 *      data from the dataset, using the space_id for the selection in the
 *      dataset on disk and the type_id for the memory representation of the
 *      VL data, in memory.  The *size value is modified according to how many
 *      bytes are required to store the VL data in memory.
 *
 * Return:  Non-negative on success, negative on failure
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, August 11, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dvlen_get_buf_size(hid_t dataset_id, hid_t type_id, hid_t space_id, hsize_t *size /*out*/)
{
    H5VL_object_t *vol_obj;   /* Dataset for this operation */
    hbool_t        supported; /* Whether 'get vlen buf size' operation is supported by VOL connector */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "iiix", dataset_id, type_id, space_id, size);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object(dataset_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataset identifier")
    if (H5I_DATATYPE != H5I_get_type(type_id))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid datatype identifier")
    if (H5I_DATASPACE != H5I_get_type(space_id))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataspace identifier")
    if (size == NULL)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid 'size' pointer")

    /* Check if the 'get_vlen_buf_size' callback is supported */
    supported = FALSE;
    if (H5VL_introspect_opt_query(vol_obj, H5VL_SUBCLS_DATASET, H5VL_NATIVE_DATASET_GET_VLEN_BUF_SIZE,
                                  &supported) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, H5I_INVALID_HID,
                    "can't check for 'get vlen buf size' operation")
    if (supported) {
        /* Make the 'get_vlen_buf_size' callback */
        if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_VLEN_BUF_SIZE, H5P_DATASET_XFER_DEFAULT,
                                  H5_REQUEST_NULL, type_id, space_id, size) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "unable to get vlen buf size")
    } /* end if */
    else {
        /* Perform a generic operation that will work with all VOL connectors */
        if (H5D__vlen_get_buf_size_gen(vol_obj, type_id, space_id, size) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "unable to get vlen buf size")
    } /* end else */

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dvlen_get_buf_size() */

/*-------------------------------------------------------------------------
 * Function:    H5Dset_extent
 *
 * Purpose:     Modifies the dimensions of a dataset.
 *              Can change to a smaller dimension.
 *
 * Return:	Non-negative on success, negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dset_extent(hid_t dset_id, const hsize_t size[])
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation   */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "i*h", dset_id, size);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")
    if (!size)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "size array cannot be NULL")

    /* Set up collective metadata if appropriate */
    if (H5CX_set_loc(dset_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set collective metadata read info")

    /* Set the extent */
    if ((ret_value = H5VL_dataset_specific(vol_obj, H5VL_DATASET_SET_EXTENT, H5P_DATASET_XFER_DEFAULT,
                                           H5_REQUEST_NULL, size)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "unable to set dataset extent")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dset_extent() */

/*-------------------------------------------------------------------------
 * Function:    H5Dflush
 *
 * Purpose:     Flushes all buffers associated with a dataset.
 *
 * Return:      Non-negative on success, negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dflush(hid_t dset_id)
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation   */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")

    /* Set up collective metadata if appropriate */
    if (H5CX_set_loc(dset_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set collective metadata read info")

    /* Flush dataset information cached in memory
     * XXX: Note that we need to pass the ID to the VOL since the H5F_flush_cb_t
     *      callback needs it and that's in the public API.
     */
    if ((ret_value = H5VL_dataset_specific(vol_obj, H5VL_DATASET_FLUSH, H5P_DATASET_XFER_DEFAULT,
                                           H5_REQUEST_NULL, dset_id)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTFLUSH, FAIL, "unable to flush dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Dflush */

/*-------------------------------------------------------------------------
 * Function:    H5Dwait
 *
 * Purpose:     Wait for all operations on a dataset.
 *              Tang: added for async
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dwait(hid_t dset_id)
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")

    /* Set up collective metadata if appropriate */
    if (H5CX_set_loc(dset_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set collective metadata read info")

    if ((ret_value = H5VL_dataset_specific(vol_obj, H5VL_DATASET_WAIT, H5P_DATASET_XFER_DEFAULT,
                                           H5_REQUEST_NULL, dset_id)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTOPERATE, FAIL, "unable to wait dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Dwait*/

/*-------------------------------------------------------------------------
 * Function:    H5Drefresh
 *
 * Purpose:     Refreshes all buffers associated with a dataset.
 *
 * Return:      Non-negative on success, negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Drefresh(hid_t dset_id)
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation   */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")

    /* Set up collective metadata if appropriate */
    if (H5CX_set_loc(dset_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set collective metadata read info")

    /* Refresh the dataset object */
    if ((ret_value = H5VL_dataset_specific(vol_obj, H5VL_DATASET_REFRESH, H5P_DATASET_XFER_DEFAULT,
                                           H5_REQUEST_NULL, dset_id)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTLOAD, FAIL, "unable to refresh dataset")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Drefresh() */

/*-------------------------------------------------------------------------
 * Function:    H5Dformat_convert (Internal)
 *
 * Purpose:     For chunked:
 *                  Convert the chunk indexing type to version 1 B-tree if not
 *              For compact/contiguous:
 *                  Downgrade layout version to 3 if greater than 3
 *              For virtual:
 *                  No conversion
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Vailin Choi
 *              Feb 2015
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dformat_convert(hid_t dset_id)
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation   */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", dset_id);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")

    /* Set up collective metadata if appropriate */
    if (H5CX_set_loc(dset_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSET, FAIL, "can't set collective metadata read info")

    /* Convert the dataset */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_FORMAT_CONVERT, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_INTERNAL, FAIL, "can't convert dataset format")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Dformat_convert */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_chunk_index_type (Internal)
 *
 * Purpose:     Retrieve a dataset's chunk indexing type
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Vailin Choi
 *              Feb 2015
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dget_chunk_index_type(hid_t dset_id, H5D_chunk_index_t *idx_type /*out*/)
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation   */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)
    H5TRACE2("e", "ix", dset_id, idx_type);

    /* Check args */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")
    if (NULL == idx_type)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "idx_type parameter cannot be NULL")

    /* Get the chunk indexing type */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_CHUNK_INDEX_TYPE, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, idx_type) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get chunk index type")

done:
    FUNC_LEAVE_API(ret_value)
} /* H5Dget_chunk_index_type() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_chunk_storage_size
 *
 * Purpose:     Returns the size of an allocated chunk.
 *
 *              Intended for use with the H5D(O)read_chunk API call so
 *              the caller can construct an appropriate buffer.
 *
 * Return:	Non-negative on success, negative on failure
 *
 * Programmer:  Matthew Strong (GE Healthcare)
 *              20 October 2016
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dget_chunk_storage_size(hid_t dset_id, const hsize_t *offset, hsize_t *chunk_nbytes /*out*/)
{
    H5VL_object_t *vol_obj;             /* Dataset for this operation   */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "i*hx", dset_id, offset, chunk_nbytes);

    /* Check arguments */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "dset_id parameter is not a valid dataset identifier")
    if (NULL == offset)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offset parameter cannot be NULL")
    if (NULL == chunk_nbytes)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "chunk_nbytes parameter cannot be NULL")

    /* Get the dataset creation property list */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_CHUNK_STORAGE_SIZE, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, offset, chunk_nbytes) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get storage size of chunk")

done:
    FUNC_LEAVE_API(ret_value);
} /* H5Dget_chunk_storage_size() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_num_chunks
 *
 * Purpose:     Retrieves the number of chunks that have non-empty intersection
 *              with a specified selection.
 *
 * Note:        Currently, this function only gets the number of all written
 *              chunks, regardless the dataspace.
 *
 * Parameters:
 *              hid_t dset_id;      IN: Chunked dataset ID
 *              hid_t fspace_id;    IN: File dataspace ID
 *              hsize_t *nchunks;   OUT: Number of non-empty chunks
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Binh-Minh Ribler
 *              May 2019 (HDFFV-10677)
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dget_num_chunks(hid_t dset_id, hid_t fspace_id, hsize_t *nchunks /*out*/)
{
    H5VL_object_t *vol_obj   = NULL; /* Dataset for this operation */
    herr_t         ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "iix", dset_id, fspace_id, nchunks);

    /* Check arguments */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataset identifier")
    if (NULL == nchunks)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid argument (null)")

    /* Get the number of written chunks */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_NUM_CHUNKS, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, fspace_id, nchunks) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "Can't get number of chunks")

done:
    FUNC_LEAVE_API(ret_value);
} /* H5Dget_num_chunks() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_chunk_info
 *
 * Purpose:     Retrieves information about a chunk specified by its index.
 *
 * Parameters:
 *              hid_t dset_id;          IN: Chunked dataset ID
 *              hid_t fspace_id;        IN: File dataspace ID
 *              hsize_t index;          IN: Index of written chunk
 *              hsize_t *offset         OUT: Logical position of the chunk’s
 *                                           first element in the dataspace
 *              unsigned *filter_mask   OUT: Mask for identifying the filters in use
 *              haddr_t *addr           OUT: Address of the chunk
 *              hsize_t *size           OUT: Size of the chunk
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Binh-Minh Ribler
 *              May 2019 (HDFFV-10677)
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dget_chunk_info(hid_t dset_id, hid_t fspace_id, hsize_t chk_index, hsize_t *offset /*out*/,
                  unsigned *filter_mask /*out*/, haddr_t *addr /*out*/, hsize_t *size /*out*/)
{
    H5VL_object_t *vol_obj   = NULL; /* Dataset for this operation */
    hsize_t        nchunks   = 0;
    herr_t         ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE7("e", "iihxxxx", dset_id, fspace_id, chk_index, offset, filter_mask, addr, size);

    /* Check arguments */
    if (NULL == offset && NULL == filter_mask && NULL == addr && NULL == size)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                    "invalid arguments, must have at least one non-null output argument")
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataset identifier")

    /* Get the number of written chunks to check range */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_NUM_CHUNKS, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, fspace_id, &nchunks) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "Can't get number of chunks")

    /* Check range for chunk index */
    if (chk_index >= nchunks)
        HGOTO_ERROR(H5E_DATASET, H5E_BADRANGE, FAIL, "chunk index is out of range")

    /* Call private function to get the chunk info given the chunk's index */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_CHUNK_INFO_BY_IDX, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, fspace_id, chk_index, offset, filter_mask, addr, size) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "Can't get chunk info by index")

done:
    FUNC_LEAVE_API(ret_value);
} /* H5Dget_chunk_info() */

/*-------------------------------------------------------------------------
 * Function:    H5Dget_chunk_info_by_coord
 *
 * Purpose:     Retrieves information about a chunk specified by its logical
 *              coordinates.
 *
 * Parameters:
 *              hid_t dset_id;          IN: Chunked dataset ID
 *              hsize_t *offset         IN: Logical position of the chunk’s
 *                                           first element in the dataspace
 *              unsigned *filter_mask   OUT: Mask for identifying the filters in use
 *              haddr_t *addr           OUT: Address of the chunk
 *              hsize_t *size           OUT: Size of the chunk
 *
 * Return:      Non-negative on success, negative on failure
 *
 * Programmer:  Binh-Minh Ribler
 *              May 2019 (HDFFV-10677)
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dget_chunk_info_by_coord(hid_t dset_id, const hsize_t *offset, unsigned *filter_mask /*out*/,
                           haddr_t *addr /*out*/, hsize_t *size /*out*/)
{
    H5VL_object_t *vol_obj   = NULL; /* Dataset for this operation */
    herr_t         ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "i*hxxx", dset_id, offset, filter_mask, addr, size);

    /* Check arguments */
    if (NULL == (vol_obj = (H5VL_object_t *)H5I_object_verify(dset_id, H5I_DATASET)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid dataset identifier")
    if (NULL == filter_mask && NULL == addr && NULL == size)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                    "invalid arguments, must have at least one non-null output argument")
    if (NULL == offset)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid argument (null)")

    /* Call private function to get the chunk info given the chunk's index */
    if (H5VL_dataset_optional(vol_obj, H5VL_NATIVE_DATASET_GET_CHUNK_INFO_BY_COORD, H5P_DATASET_XFER_DEFAULT,
                              H5_REQUEST_NULL, offset, filter_mask, addr, size) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "Can't get chunk info by its logical coordinates")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Dget_chunk_info_by_coord() */
