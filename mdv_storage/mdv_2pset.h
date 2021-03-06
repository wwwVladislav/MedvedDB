/**
 * @file mdv_2pset.h
 * @author Vladislav Volkov (wwwvladislav@gmail.com)
 * @brief DB objects storage
 * @version 0.1
 * @date 2019-11-15
 *
 * @copyright Copyright (c) 2019, Vladislav Volkov
 *
 */
#pragma once
#include <mdv_def.h>
#include <mdv_data.h>
#include <mdv_enumerator.h>


/// DB objects storage
typedef struct mdv_2pset mdv_2pset;


/**
 * @brief Creates new or opens existing DB objects storage
 *
 * @param root_dir [in]     Root directory for DB objects storage
 * @param storage_name [in] storage name
 *
 * @return DB objects storage
 */
mdv_2pset * mdv_2pset_open(char const *root_dir, char const *storage_name);


/**
 * @brief Add reference counter
 *
 * @param objs [in] DB objects storage
 *
 * @return pointer which provided as argument
 */
mdv_2pset * mdv_2pset_retain(mdv_2pset *objs);


/**
 * @brief Decrement references counter. Storage is closed and memory is freed if references counter is zero.
 *
 * @param objs [in] DB objects storage
 *
 * @return references counter
 */
uint32_t mdv_2pset_release(mdv_2pset *objs);


/**
 * @brief Reserves identifiers range for objects
 *
 * @param objs [in]     DB objects storage
 * @param range [in]    Identifiers range length
 * @param id [out]      first identifier from range
 *
 * @return On success, return MDV_OK.
 * @return On error, return non zero value
 */
mdv_errno mdv_2pset_reserve_ids_range(mdv_2pset *objs, uint32_t range, uint64_t *id);


/**
 * @brief Stores information about the new object.
 *
 * @param objs [in]     DB objects storage
 * @param id [in]       Unique object identifier
 * @param obj [in]      Serialized object
 *
 * @return On success, return MDV_OK.
 * @return On error, return non zero value
 */
mdv_errno mdv_2pset_add(mdv_2pset *objs, mdv_data const *id, mdv_data const *obj);


/**
 * @brief Stores object batch.
 *
 * @param objs [in]     DB objects storage
 * @param arg [in]      Pointer which is provided as argument to batch items generator
 * @param next [in]     batch items generator
 *
 * @return On success, return MDV_OK.
 * @return On error, return non zero value
 */
mdv_errno mdv_2pset_add_batch(mdv_2pset *objs, void *arg, bool (*next)(void *arg, mdv_data *id, mdv_data *obj));


/**
 * @brief Reads and returns the stored object
 *
 * @param objs [in]     DB objects storage
 * @param id [in]       Unique object identifier
 * @param restore [in]  Function for object reading (or deserialization)
 *
 * @return On success, returns nonzero object pointer
 * @return On error, returns NULL
 */
void * mdv_2pset_get(mdv_2pset *objs, mdv_data const *id, void * (*restore)(mdv_data const *));


/**
 * @brief Creates objects iterator from begin of objects collection
 *
 * @param objs [in]     DB objects storage
 *
 * @return objects iterator from begin of objects collection
 */
mdv_enumerator * mdv_2pset_enumerator(mdv_2pset *objs);


/**
 * @brief Creates objects iterator from given position of objects collection
 *
 * @param objs [in]     DB objects storage
 * @param id [in]       Unique object identifier
 *
 * @return objects iterator
 */
mdv_enumerator * mdv_2pset_enumerator_from(mdv_2pset *objs, mdv_data const *id);
