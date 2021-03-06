
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "service_area_restriction.h"

OpenAPI_service_area_restriction_t *OpenAPI_service_area_restriction_create(
    OpenAPI_restriction_type_t *restriction_type,
    OpenAPI_list_t *areas,
    int max_num_of_t_as,
    int max_num_of_t_as_for_not_allowed_areas
    )
{
    OpenAPI_service_area_restriction_t *service_area_restriction_local_var = OpenAPI_malloc(sizeof(OpenAPI_service_area_restriction_t));
    if (!service_area_restriction_local_var) {
        return NULL;
    }
    service_area_restriction_local_var->restriction_type = restriction_type;
    service_area_restriction_local_var->areas = areas;
    service_area_restriction_local_var->max_num_of_t_as = max_num_of_t_as;
    service_area_restriction_local_var->max_num_of_t_as_for_not_allowed_areas = max_num_of_t_as_for_not_allowed_areas;

    return service_area_restriction_local_var;
}

void OpenAPI_service_area_restriction_free(OpenAPI_service_area_restriction_t *service_area_restriction)
{
    if (NULL == service_area_restriction) {
        return;
    }
    OpenAPI_lnode_t *node;
    OpenAPI_restriction_type_free(service_area_restriction->restriction_type);
    OpenAPI_list_for_each(service_area_restriction->areas, node) {
        OpenAPI_area_free(node->data);
    }
    OpenAPI_list_free(service_area_restriction->areas);
    ogs_free(service_area_restriction);
}

cJSON *OpenAPI_service_area_restriction_convertToJSON(OpenAPI_service_area_restriction_t *service_area_restriction)
{
    cJSON *item = NULL;

    if (service_area_restriction == NULL) {
        ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [ServiceAreaRestriction]");
        return NULL;
    }

    item = cJSON_CreateObject();
    if (service_area_restriction->restriction_type) {
        cJSON *restriction_type_local_JSON = OpenAPI_restriction_type_convertToJSON(service_area_restriction->restriction_type);
        if (restriction_type_local_JSON == NULL) {
            ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [restriction_type]");
            goto end;
        }
        cJSON_AddItemToObject(item, "restrictionType", restriction_type_local_JSON);
        if (item->child == NULL) {
            ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [restriction_type]");
            goto end;
        }
    }

    if (service_area_restriction->areas) {
        cJSON *areasList = cJSON_AddArrayToObject(item, "areas");
        if (areasList == NULL) {
            ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [areas]");
            goto end;
        }

        OpenAPI_lnode_t *areas_node;
        if (service_area_restriction->areas) {
            OpenAPI_list_for_each(service_area_restriction->areas, areas_node) {
                cJSON *itemLocal = OpenAPI_area_convertToJSON(areas_node->data);
                if (itemLocal == NULL) {
                    ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [areas]");
                    goto end;
                }
                cJSON_AddItemToArray(areasList, itemLocal);
            }
        }
    }

    if (service_area_restriction->max_num_of_t_as) {
        if (cJSON_AddNumberToObject(item, "maxNumOfTAs", service_area_restriction->max_num_of_t_as) == NULL) {
            ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [max_num_of_t_as]");
            goto end;
        }
    }

    if (service_area_restriction->max_num_of_t_as_for_not_allowed_areas) {
        if (cJSON_AddNumberToObject(item, "maxNumOfTAsForNotAllowedAreas", service_area_restriction->max_num_of_t_as_for_not_allowed_areas) == NULL) {
            ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed [max_num_of_t_as_for_not_allowed_areas]");
            goto end;
        }
    }

end:
    return item;
}

OpenAPI_service_area_restriction_t *OpenAPI_service_area_restriction_parseFromJSON(cJSON *service_area_restrictionJSON)
{
    OpenAPI_service_area_restriction_t *service_area_restriction_local_var = NULL;
    cJSON *restriction_type = cJSON_GetObjectItemCaseSensitive(service_area_restrictionJSON, "restrictionType");

    OpenAPI_restriction_type_t *restriction_type_local_nonprim = NULL;
    if (restriction_type) {
        restriction_type_local_nonprim = OpenAPI_restriction_type_parseFromJSON(restriction_type);
    }

    cJSON *areas = cJSON_GetObjectItemCaseSensitive(service_area_restrictionJSON, "areas");

    OpenAPI_list_t *areasList;
    if (areas) {
        cJSON *areas_local_nonprimitive;
        if (!cJSON_IsArray(areas)) {
            ogs_error("OpenAPI_service_area_restriction_parseFromJSON() failed [areas]");
            goto end;
        }

        areasList = OpenAPI_list_create();

        cJSON_ArrayForEach(areas_local_nonprimitive, areas ) {
            if (!cJSON_IsObject(areas_local_nonprimitive)) {
                ogs_error("OpenAPI_service_area_restriction_parseFromJSON() failed [areas]");
                goto end;
            }
            OpenAPI_area_t *areasItem = OpenAPI_area_parseFromJSON(areas_local_nonprimitive);

            OpenAPI_list_add(areasList, areasItem);
        }
    }

    cJSON *max_num_of_t_as = cJSON_GetObjectItemCaseSensitive(service_area_restrictionJSON, "maxNumOfTAs");

    if (max_num_of_t_as) {
        if (!cJSON_IsNumber(max_num_of_t_as)) {
            ogs_error("OpenAPI_service_area_restriction_parseFromJSON() failed [max_num_of_t_as]");
            goto end;
        }
    }

    cJSON *max_num_of_t_as_for_not_allowed_areas = cJSON_GetObjectItemCaseSensitive(service_area_restrictionJSON, "maxNumOfTAsForNotAllowedAreas");

    if (max_num_of_t_as_for_not_allowed_areas) {
        if (!cJSON_IsNumber(max_num_of_t_as_for_not_allowed_areas)) {
            ogs_error("OpenAPI_service_area_restriction_parseFromJSON() failed [max_num_of_t_as_for_not_allowed_areas]");
            goto end;
        }
    }

    service_area_restriction_local_var = OpenAPI_service_area_restriction_create (
        restriction_type ? restriction_type_local_nonprim : NULL,
        areas ? areasList : NULL,
        max_num_of_t_as ? max_num_of_t_as->valuedouble : 0,
        max_num_of_t_as_for_not_allowed_areas ? max_num_of_t_as_for_not_allowed_areas->valuedouble : 0
        );

    return service_area_restriction_local_var;
end:
    return NULL;
}

OpenAPI_service_area_restriction_t *OpenAPI_service_area_restriction_copy(OpenAPI_service_area_restriction_t *dst, OpenAPI_service_area_restriction_t *src)
{
    cJSON *item = NULL;
    char *content = NULL;

    ogs_assert(src);
    item = OpenAPI_service_area_restriction_convertToJSON(src);
    if (!item) {
        ogs_error("OpenAPI_service_area_restriction_convertToJSON() failed");
        return NULL;
    }

    content = cJSON_Print(item);
    cJSON_Delete(item);

    if (!content) {
        ogs_error("cJSON_Print() failed");
        return NULL;
    }

    item = cJSON_Parse(content);
    ogs_free(content);
    if (!item) {
        ogs_error("cJSON_Parse() failed");
        return NULL;
    }

    OpenAPI_service_area_restriction_free(dst);
    dst = OpenAPI_service_area_restriction_parseFromJSON(item);
    cJSON_Delete(item);

    return dst;
}

