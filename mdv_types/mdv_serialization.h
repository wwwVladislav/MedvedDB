#pragma once
#include "mdv_table.h"
#include "mdv_rowset.h"
#include <mdv_binn.h>
#include <mdv_topology.h>
#include <mdv_bitset.h>
#include <stdbool.h>


bool                mdv_binn_table_desc(mdv_table_desc const *table, binn *obj);
mdv_table_desc *    mdv_unbinn_table_desc(binn const *obj);

bool                mdv_binn_table(mdv_table const *table, binn *obj);
mdv_table      *    mdv_unbinn_table(binn const *obj);
mdv_rowlist_entry * mdv_unbinn_table_as_row_slice(binn const *obj, mdv_bitset const *mask);

bool                mdv_binn_table_uuid(mdv_uuid const *uuid, binn *obj);
bool                mdv_unbinn_table_uuid(binn const *obj, mdv_uuid *uuid);

bool                mdv_binn_row(mdv_row const *row, mdv_table_desc const *table_desc, binn *list);
mdv_rowlist_entry * mdv_unbinn_row(binn const *list, mdv_table_desc const *table_desc);
mdv_rowlist_entry * mdv_unbinn_row_slice(binn const *list, mdv_table_desc const *table_desc, mdv_bitset const *mask);

bool                mdv_binn_rowset(mdv_rowset *rowset, binn *list);
mdv_rowset *        mdv_unbinn_rowset(binn const *list, mdv_table *table);

bool                mdv_binn_bitset(mdv_bitset const *bitset, binn *obj);
mdv_bitset     *    mdv_unbinn_bitset(binn const *obj);


/**
 * @brief Serialize network topology
 *
 * @param topology [in] network topology
 * @param obj [out]     serialized topology
 *
 * @return On success returns true.
 * @return On error returns false.
 */
bool mdv_topology_serialize(mdv_topology *topology, binn *obj);


/**
 * @brief Deserialize network topology
 *
 * @param obj [in]       serialized topology
 *
 * @return On success returns non NULL pointer to topology.
 * @return On error returns NULL.
 */
mdv_topology * mdv_topology_deserialize(binn const *obj);


/**
 * @brief UUID serialization
 */
bool mdv_binn_uuid(mdv_uuid const *uuid, binn *obj);


/**
 * @brief UUID deserialization
 */
bool mdv_unbinn_uuid(binn *obj, mdv_uuid *uuid);
