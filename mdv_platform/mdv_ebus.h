/**
 * @file mdv_ebus.h
 * @author Vladislav Volkov (wwwVladislav@gmail.com)
 * @brief Event bus implementation
 * @version 0.1
 * @date 2019-10-06
 *
 * @copyright Copyright (c) 2019, Vladislav Volkov
 *
 */
#pragma once
#include "mdv_def.h"
#include "mdv_threadpool.h"
#include <stdatomic.h>


/// Event bus configuration
typedef struct mdv_ebus_config
{
    mdv_threadpool_config   threadpool;     ///< thread pool options

    struct
    {
        size_t queues_count;                ///< Event queues count
        uint16_t max_id;                    ///< Maximum event identifier
    } event;
} mdv_ebus_config;


/// Event bus
typedef struct mdv_ebus mdv_ebus;


/// Event
typedef struct mdv_event mdv_event;


/// Event type
typedef uint32_t mdv_event_type;


typedef mdv_event * (*mdv_event_retain_fn)(mdv_event *);
typedef uint32_t (*mdv_event_release_fn)(mdv_event *);


/// Interface for events
typedef struct
{
    mdv_event_retain_fn  retain;        ///< Function for event retain
    mdv_event_release_fn release;       ///< Function for event release
} mdv_ievent;


/// Event
struct mdv_event
{
    mdv_ievent             *vptr;           ///< Virtual methods table
    mdv_event_type          type;           ///< Event type
    atomic_uint_fast32_t    rc;             ///< References counter
};


/// Event handler type
typedef mdv_errno (*mdv_event_handler)(void *arg, mdv_event *event);


/// Event handler for specific type
typedef struct
{
    mdv_event_type      type;               ///< Event type
    mdv_event_handler   handler;            ///< Event handler
} mdv_event_handler_type;


/**
 * @brief Creates new event given size
 *
 * @param size [in] event type
 * @param size [in] event size in bytes
 *
 * @return new dynamically allocated event
 */
mdv_event * mdv_event_create(mdv_event_type type, size_t size);


/**
 * @brief Retains an event.
 * @details Reference counter is increased by one.
 */
mdv_event * mdv_event_retain(mdv_event *event);



/**
 * @brief Releases an event.
 * @details Reference counter is decreased by one.
 *          When the reference counter reaches zero, the event is freed.
 */
uint32_t mdv_event_release(mdv_event *event);


/**
 * @brief Creates new event bus
 *
 * @param config [in]   Event bus configuration
 *
 * @return pointer to new event bus
 */
mdv_ebus * mdv_ebus_create(mdv_ebus_config const *config);


/**
 * @brief Retains event bus.
 * @details Reference counter is increased by one.
 */
mdv_ebus * mdv_ebus_retain(mdv_ebus *ebus);


/**
 * @brief Releases event bus.
 * @details Reference counter is decreased by one.
 *          When the reference counter reaches zero, the event bus and all events are freed.
 */
uint32_t mdv_ebus_release(mdv_ebus *ebus);


/**
 * @brief Registers new event handler for specific type
 *
 * @param ebus [in]     Event bus
 * @param type [in]     Event type
 * @param arg [in]      This argument is passed as argument to event handler
 * @param handler [in]  Event handling function
 *
 * @return On success returns MDV_OK
 * @return On error return nonzero error code.
 */
mdv_errno mdv_ebus_subscribe(mdv_ebus *ebus,
                             mdv_event_type type,
                             void *arg,
                             mdv_event_handler handler);


/**
 * @brief Registers several event handlers
 *
 * @param ebus [in]     Event bus
 * @param arg [in]      This argument is passed as argument to event handler
 * @param handlers [in] Event handlers
 * @param count [in]    Event handlers count
 *
 * @return On success returns MDV_OK
 * @return On error return nonzero error code.
 */
mdv_errno mdv_ebus_subscribe_all(mdv_ebus *ebus,
                                 void *arg,
                                 mdv_event_handler_type const *handlers,
                                 size_t count);


/**
 * @brief Unregisters event handler registered by mdv_ebus_subscribe()
 *
 * @param ebus [in]     Event bus
 * @param type [in]     Event type
 * @param arg [in]      This argument is passed as argument to event handler
 * @param handler [in]  Event handling function
 */
void mdv_ebus_unsubscribe(mdv_ebus *ebus,
                          mdv_event_type type,
                          void *arg,
                          mdv_event_handler handler);


/**
 * @brief Unregisters several event handlers
 *
 * @param ebus [in]     Event bus
 * @param arg [in]      This argument is passed as argument to event handler
 * @param handlers [in] Event handlers
 * @param count [in]    Event handlers count
 */
void mdv_ebus_unsubscribe_all(mdv_ebus *ebus,
                              void *arg,
                              mdv_event_handler_type const *handlers,
                              size_t count);


/// Modes for event publishing.
enum mdv_evt_flags
{
    MDV_EVT_DEFAULT = 0,        ///< Defauls mode. Event is simply added to queue.

    MDV_EVT_UNIQUE  = 1 << 0,   ///< New event is stored in internal queue only once.
                                ///< If event bus contains already registered event the
                                ///< same type the functions returns MDV_EEXIST.

    MDV_EVT_SYNC    = 1 << 1,   ///< Synchronous mode. All registered handlers are called immediately.
};


/**
 * @brief Event notification
 * @details New event is stored in internal queue and asynchronous processed by thread pool.
 *
 * @param ebus [in]     Event bus
 * @param event [in]    Event data structure
 * @param flags [in]    This argument can include one of the following flags from mdv_evt_flags enum.
 *
 * @return On success returns MDV_OK
 * @return On error return nonzero error code.
 */
mdv_errno mdv_ebus_publish(mdv_ebus *ebus,
                           mdv_event *event,
                           int flags);
