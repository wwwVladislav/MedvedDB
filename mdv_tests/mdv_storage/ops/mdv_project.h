#pragma once
#include "mdv_scan_list.h"
#include "mdv_test_utils.h"

#include <minunit.h>
#include <binn.h>
#include <ops/mdv_project.h>
#include <mdv_filesystem.h>
#include <stdio.h>
#include <string.h>


MU_TEST(op_project_range)
{
    mdv_list table = create_test_rows_list(10, 10);
    mdv_op *scanner = mdv_scan_list(&table);
    mu_check(scanner);

    const uint32_t from = 2, to = 7;

    mdv_op * project = mdv_project_range(scanner, from, to);
    mu_check(project);

    mdv_op_release(scanner);
    scanner = 0;

    mdv_kvdata kvdata;

    for(size_t i = 0; i < 10; ++i)
    {
        mu_check(mdv_op_next(project, &kvdata) == MDV_OK);

        mu_check(kvdata.key.size == sizeof(size_t));
        mu_check(*(size_t*)kvdata.key.ptr == i);

        binn row;
        mu_check(binn_load(kvdata.value.ptr, &row));

        binn_iter iter = {};
        binn item = {};

        uint32_t n = from;

        binn_list_foreach(&row, item)
        {
            int value = 0;
            mu_check(binn_get_int32(&item, &value));
            mu_check((uint32_t)value == n++);
        }
    }

    mu_check(mdv_op_next(project, &kvdata) == MDV_FALSE);
    mdv_op_reset(project);
    mu_check(mdv_op_next(project, &kvdata) == MDV_OK);

    mdv_list_clear(&table);
    mdv_op_release(project);
}


MU_TEST(op_project_by_indices)
{
    mdv_list table = create_test_rows_list(10, 10);
    mdv_op *scanner = mdv_scan_list(&table);
    mu_check(scanner);

    const size_t indices[] = { 0, 2, 4, 6, 8 };

    mdv_op * project = mdv_project_by_indices(scanner, sizeof indices / sizeof *indices, indices);
    mu_check(project);

    mdv_op_release(scanner);
    scanner = 0;

    mdv_kvdata kvdata;

    for(size_t i = 0; i < 10; ++i)
    {
        mu_check(mdv_op_next(project, &kvdata) == MDV_OK);

        mu_check(kvdata.key.size == sizeof(size_t));
        mu_check(*(size_t*)kvdata.key.ptr == i);

        binn row;
        mu_check(binn_load(kvdata.value.ptr, &row));

        binn_iter iter = {};
        binn item = {};

        uint32_t n = 0;

        binn_list_foreach(&row, item)
        {
            int value = 0;
            mu_check(binn_get_int32(&item, &value));
            mu_check((uint32_t)value == n);
            n += 2;
        }
    }

    mu_check(mdv_op_next(project, &kvdata) == MDV_FALSE);
    mdv_op_reset(project);
    mu_check(mdv_op_next(project, &kvdata) == MDV_OK);

    mdv_list_clear(&table);
    mdv_op_release(project);
}
