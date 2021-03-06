/**
 * @file mdv_evt_types.h
 * @author Vladislav Volkov (wwwvladislav@gmail.com)
 * @brief Internal event types identifiers
 * @version 0.1
 * @date 2019-10-21
 *
 * @copyright Copyright (c) 2019, Vladislav Volkov
 *
 */
#pragma once


enum
{
    MDV_EVT_BROADCAST_POST,
    MDV_EVT_BROADCAST,
    MDV_EVT_LINK_STATE,
    MDV_EVT_LINK_CHECK,
    MDV_EVT_TOPOLOGY,
    MDV_EVT_TOPOLOGY_SYNC,
    MDV_EVT_TABLE_CREATE,
    MDV_EVT_TABLE_GET,
    MDV_EVT_TABLES_GET,
    MDV_EVT_ROWDATA_INSERT,
    MDV_EVT_ROWDATA_GET,
    MDV_EVT_TRLOG_GET,
    MDV_EVT_TRLOG_CHANGED,
    MDV_EVT_TRLOG_APPLY,
    MDV_EVT_TRLOG_SYNC,
    MDV_EVT_TRLOG_STATE,
    MDV_EVT_TRLOG_DATA,
    MDV_EVT_SELECT,
    MDV_EVT_VIEW,
    MDV_EVT_VIEW_FETCH,
    MDV_EVT_VIEW_DATA,
    MDV_EVT_STATUS,
    MDV_EVT_COUNT
};

