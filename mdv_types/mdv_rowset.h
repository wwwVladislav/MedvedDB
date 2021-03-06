/**
 * @file mdv_rowset.h
 * @author Vladislav Volkov (wwwvladislav@gmail.com)
 * @brief Interface and useful types definitions for row set access.
 * @details Row set is associated with specific table or view.
 * @version 0.1
 * @date 2019-11-19
 *
 * @copyright Copyright (c) 2019, Vladislav Volkov
 *
 */
#pragma once
#include "mdv_data.h"
#include "mdv_row.h"
#include "mdv_table.h"
#include <mdv_def.h>
#include <mdv_enumerator.h>
#include <mdv_list.h>
#include <stdatomic.h>


/// Predicate for row filtering
typedef int (*mdv_row_filter)(void *arg, mdv_row const *row_slice);


typedef mdv_list_entry(mdv_row) mdv_rowlist_entry;


/// Set of rows
typedef struct mdv_rowset mdv_rowset;


typedef mdv_rowset *     (*mdv_rowset_retain_fn)    (mdv_rowset *);
typedef uint32_t         (*mdv_rowset_release_fn)   (mdv_rowset *);
typedef mdv_table *      (*mdv_rowset_table_fn)     (mdv_rowset *);
typedef size_t           (*mdv_rowset_append_fn)    (mdv_rowset *, mdv_data const **, size_t);
typedef void             (*mdv_rowset_emplace_fn)   (mdv_rowset *, mdv_rowlist_entry *);
typedef mdv_enumerator * (*mdv_rowset_enumerator_fn)(mdv_rowset *);


/// Interface for row sets
typedef struct
{
    mdv_rowset_retain_fn     retain;        ///< Function for rowset retain
    mdv_rowset_release_fn    release;       ///< Function for rowset release
    mdv_rowset_table_fn      table;         ///< Function for accessing to the table associated with rowset
    mdv_rowset_append_fn     append;        ///< Function for append new row to rowset
    mdv_rowset_emplace_fn    emplace;       ///< Function for emplace new row in rowset
    mdv_rowset_enumerator_fn enumerator;    ///< Function for rowset iterator creation
} mdv_irowset;


struct mdv_rowset
{
    mdv_irowset const      *vptr;       ///< Interface for rowset
};


/**
 * @brief Create new rowset allocated in memory
 *
 * @return rowset or NULL
 */
mdv_rowset * mdv_rowset_create(mdv_table *table);


/**
 * @brief Retains rowset.
 * @details Reference counter is increased by one.
 */
mdv_rowset * mdv_rowset_retain(mdv_rowset *rowset);


/**
 * @brief Releases rowset.
 * @details Reference counter is decreased by one.
 *          When the reference counter reaches zero, the rowset is freed.
 */
uint32_t mdv_rowset_release(mdv_rowset *rowset);


/**
 * @brief Returns table associated with rowset
 */
mdv_table * mdv_rowset_table(mdv_rowset *rowset);


/**
 * @brief Appends rows to rowset
 *
 * @param rowset [in]   set of rows
 * @param rows [in]     rows array
 * @param count [in]    rows count in array
 *
 * @return number of appended rows
 */
size_t mdv_rowset_append(mdv_rowset *rowset, mdv_data const **rows, size_t count);


/**
 * @brief Appends row to rowset
 *
 * @param rowset [in]   set of rows
 * @param entry [in]    rows set entry
 */
void mdv_rowset_emplace(mdv_rowset *rowset, mdv_rowlist_entry *entry);


/**
 * @brief Creates rowset entries iterator
 */
mdv_enumerator * mdv_rowset_enumerator(mdv_rowset *rowset);
