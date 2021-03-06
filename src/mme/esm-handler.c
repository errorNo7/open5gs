/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "mme-context.h"
#include "nas-path.h"
#include "sgsap-path.h"
#include "mme-gtp-path.h"

#include "esm-build.h"
#include "esm-handler.h"

#undef OGS_LOG_DOMAIN
#define OGS_LOG_DOMAIN __esm_log_domain

int esm_handle_pdn_connectivity_request(mme_bearer_t *bearer, 
        ogs_nas_eps_pdn_connectivity_request_t *req)
{
    mme_ue_t *mme_ue = NULL;
    mme_sess_t *sess = NULL;
    uint8_t security_protected_required = 0;

    ogs_assert(bearer);
    sess = bearer->sess;
    ogs_assert(sess);
    mme_ue = sess->mme_ue;
    ogs_assert(mme_ue);

    ogs_assert(req);

    ogs_assert(MME_UE_HAVE_IMSI(mme_ue));
    ogs_assert(SECURITY_CONTEXT_IS_VALID(mme_ue));

    memcpy(&sess->request_type, &req->request_type, sizeof(sess->request_type));

    security_protected_required = 0;
    if (req->presencemask &
        OGS_NAS_EPS_PDN_CONNECTIVITY_REQUEST_ESM_INFORMATION_TRANSFER_FLAG_PRESENT) {
        ogs_nas_esm_information_transfer_flag_t *esm_information_transfer_flag =
            &req->esm_information_transfer_flag;
        security_protected_required = 
                esm_information_transfer_flag->security_protected_required;
        ogs_debug("    EIT(ESM information transfer)[%d]",
                security_protected_required);
    }

    if (req->presencemask &
            OGS_NAS_EPS_PDN_CONNECTIVITY_REQUEST_ACCESS_POINT_NAME_PRESENT) {
        sess->pdn = mme_pdn_find_by_apn(mme_ue, req->access_point_name.apn);
        if (!sess->pdn) {
            /* Invalid APN */
            nas_eps_send_pdn_connectivity_reject(
                    sess, ESM_CAUSE_MISSING_OR_UNKNOWN_APN);
            return OGS_ERROR;
        }
    }

    if (req->presencemask &
        OGS_NAS_EPS_PDN_CONNECTIVITY_REQUEST_PROTOCOL_CONFIGURATION_OPTIONS_PRESENT) {
        ogs_nas_protocol_configuration_options_t
            *protocol_configuration_options = 
            &req->protocol_configuration_options;

        OGS_NAS_STORE_DATA(&sess->ue_pco, protocol_configuration_options);
    }

    if (security_protected_required) {
        CLEAR_BEARER_TIMER(bearer->t3489);
        nas_eps_send_esm_information_request(bearer);

        return OGS_OK;
    }

    if (!sess->pdn) {
        /* Default APN */
        sess->pdn = mme_default_pdn(mme_ue);
    }

    if (sess->pdn) {
        ogs_debug("    APN[%s]", sess->pdn->apn);
        mme_gtp_send_create_session_request(sess);
    } else {
        nas_eps_send_pdn_connectivity_reject(
                sess, ESM_CAUSE_MISSING_OR_UNKNOWN_APN);
        return OGS_ERROR;
    }

    return OGS_OK;
}

int esm_handle_information_response(mme_sess_t *sess, 
        ogs_nas_eps_esm_information_response_t *rsp)
{
    mme_ue_t *mme_ue = NULL;

    ogs_assert(sess);
    mme_ue = sess->mme_ue;
    ogs_assert(mme_ue);

    ogs_assert(rsp);

    if (rsp->presencemask &
            OGS_NAS_EPS_ESM_INFORMATION_RESPONSE_ACCESS_POINT_NAME_PRESENT) {
        sess->pdn = mme_pdn_find_by_apn(mme_ue, rsp->access_point_name.apn);
    }

    if (rsp->presencemask &
        OGS_NAS_EPS_ESM_INFORMATION_RESPONSE_PROTOCOL_CONFIGURATION_OPTIONS_PRESENT) {
        ogs_nas_protocol_configuration_options_t
            *protocol_configuration_options = 
                &rsp->protocol_configuration_options;
        OGS_NAS_STORE_DATA(&sess->ue_pco, protocol_configuration_options);
    }

    if (sess->pdn) {
        ogs_debug("    APN[%s]", sess->pdn->apn);
        if (SESSION_CONTEXT_IS_AVAILABLE(mme_ue)) {
            mme_csmap_t *csmap = mme_csmap_find_by_tai(&mme_ue->tai);
            mme_ue->csmap = csmap;

            if (csmap) {
                sgsap_send_location_update_request(mme_ue);
            } else {
                nas_eps_send_attach_accept(mme_ue);
            }
        } else {
            mme_gtp_send_create_session_request(sess);
        }
    } else {
        nas_eps_send_pdn_connectivity_reject(
                sess, ESM_CAUSE_MISSING_OR_UNKNOWN_APN);
        return OGS_ERROR;
    }

    return OGS_OK;
}

int esm_handle_bearer_resource_allocation_request(
        mme_bearer_t *bearer, ogs_nas_eps_message_t *message)
{
    ogs_assert(bearer);
    nas_eps_send_bearer_resource_allocation_reject(
            bearer, ESM_CAUSE_SERVICE_OPTION_NOT_SUPPORTED);

    return OGS_OK;
}

int esm_handle_bearer_resource_modification_request(
        mme_bearer_t *bearer, ogs_nas_eps_message_t *message)
{
    mme_ue_t *mme_ue = NULL;

    ogs_assert(bearer);
    mme_ue = bearer->mme_ue;
    ogs_assert(mme_ue);

    mme_gtp_send_bearer_resource_command(bearer, message);

    return OGS_OK;
}
