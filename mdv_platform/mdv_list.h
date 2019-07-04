/**
 * @file
 * @brief List.
 */

/*
    List structure:
        head -> entry1-> ... ->entryN-> NULL

    Each list entry contains pointer to the next entry and data. Last entry points to null.
 */

#pragma once
#include <stddef.h>


/// Base type for list entry
typedef struct mdv_list_entry_base
{
    struct mdv_list_entry_base *next;       ///< Pointer to the next list entry
    char item[1];                           ///< List item
} mdv_list_entry_base;


/**
 * @brief List entry definition
 *
 * @param type [in] list item type
 */
#define mdv_list_entry(type)                \
    struct                                  \
    {                                       \
        mdv_list_entry_base *next;          \
        type item;                          \
    }


/// List
typedef struct mdv_list
{
    mdv_list_entry_base *next;              ///< Pointer to the next list entry
    mdv_list_entry_base *last;              ///< Pointer to the last list entry
} mdv_list;


/// @cond Doxygen_Suppress
int  _mdv_list_push_back(mdv_list *l, void const *val, size_t size);
void _mdv_list_emplace_back(mdv_list *l, mdv_list_entry_base *entry);
void _mdv_list_clear(mdv_list *l);
void _mdv_list_remove_next(mdv_list *l, mdv_list_entry_base *entry);
/// @endcond


/**
 * @brief Append new item to the back of list
 *
 * @param l [in]    list
 * @param val [in]  value
 *
 * @return nonzero if new item is inserted
 * @return zero if non memory
 */
#define mdv_list_push_back(l, val)          \
    _mdv_list_push_back(&(l), &(val), sizeof(val))


/**
 * @brief Append new item to the back of list
 *
 * @param l [in]        list
 * @param val_ptr [in]  value pointer
 * @param size [in]     value size
 *
 *
 * @return nonzero if new item is inserted
 * @return zero if non memory
 */
#define mdv_list_push_back_ptr(l, val_ptr, size)    \
    _mdv_list_push_back(&(l), val_ptr, size)


/**
 * @brief Check is list empty
 *
 * @param l [in]    list
 *
 * @return 1 if list empty
 * @return 1 if list contains values
 */
#define mdv_list_empty(l) ((l).next == 0)


/**
 * @brief Append new entry to the back of list
 *
 * @param l [in]      list
 * @param entry [in]  new entry
 *
 */
#define mdv_list_emplace_back(l, entry)     \
    _mdv_list_emplace_back(&(l), entry)


/**
 * @brief Clear list
 *
 * @param l [in]    list
 */
#define mdv_list_clear(l)                   \
    _mdv_list_clear(&(l))


/**
 * @brief list entries iterator
 *
 * @param l [in]        list
 * @param type [in]     list item type
 * @param entry [out]   list entry
 */
#define mdv_list_foreach(l, type, entry)                    \
    for(mdv_list_entry(type) *entry = (void*)(l).next; entry; entry = (void*)entry->next)


/**
 * @brief Remove next entry from the list
 *
 * @param l [in]        list
 * @param entry [in]    list entry
 */
#define mdv_list_remove_next(l, entry)                      \
    _mdv_list_remove_next(&(l), (mdv_list_entry_base *)(entry))
