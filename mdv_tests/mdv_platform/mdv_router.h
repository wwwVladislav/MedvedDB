#pragma once
#include "../minunit.h"
#include <mdv_router.h>


static mdv_topology * mdv_test_router_topology_create(mdv_toponode *nodes, size_t nsize,
                                                      mdv_topolink *links, size_t lsize)
{
    mdv_vector *toponodes = mdv_vector_create(nsize, sizeof(mdv_toponode), &mdv_default_allocator);
    mdv_vector_append(toponodes, nodes, nsize);

    mdv_vector *topolinks = mdv_vector_create(lsize, sizeof(mdv_topolink), &mdv_default_allocator);
    mdv_vector_append(topolinks, links, lsize);

    mdv_topology *topology = mdv_topology_create(toponodes, topolinks, &mdv_empty_vector);

    mdv_vector_release(toponodes);
    mdv_vector_release(topolinks);

    return topology;
}


MU_TEST(platform_router)
{
    /*
        Topology:
              1   4
             / \ /
            0   3
             \ / \
              2   5
    */

    mdv_toponode nodes[] =
    {
        { .id = 0, .uuid = { .a = 0 }, .addr = "0" },
        { .id = 1, .uuid = { .a = 1 }, .addr = "1" },
        { .id = 2, .uuid = { .a = 2 }, .addr = "2" },
        { .id = 3, .uuid = { .a = 3 }, .addr = "3" },
        { .id = 4, .uuid = { .a = 4 }, .addr = "4" },
        { .id = 5, .uuid = { .a = 5 }, .addr = "5" },
    };

    mdv_topolink links[] =
    {
        { .node = { 0, 1 }, .weight = 1 },
        { .node = { 0, 2 }, .weight = 1 },
        { .node = { 1, 3 }, .weight = 1 },
        { .node = { 2, 3 }, .weight = 1 },
        { .node = { 3, 4 }, .weight = 1 },
        { .node = { 3, 5 }, .weight = 1 },
    };

    // Best routes for each node
    uint32_t const routes[][4] =
    {
        { 1, 2, 0xff },     // 0 -> { 1, 2 }
        { 0, 3, 0xff },     // 1 -> { 0, 3 }
        { 0, 0xff },        // 2 -> { 0 }
        { 1, 4, 5, 0xff },  // 3 -> { 1, 4, 5 }
        { 3, 0xff },        // 4 -> { 3 }
        { 3, 0xff },        // 5 -> { 3 }
    };

    mdv_topology *topology = mdv_test_router_topology_create(nodes, sizeof(nodes) / sizeof *nodes,
                                                             links, sizeof links / sizeof *links);

    for(size_t i = 0; i < sizeof nodes / sizeof *nodes; ++i)
    {
        mdv_hashmap *mstroutes = mdv_routes_find(topology, &nodes[i].uuid);

        uint32_t j = 0;

        for(uint32_t n = routes[i][j]; n != 0xff; n = routes[i][++j])
            mu_check(mdv_hashmap_find(mstroutes, &nodes[routes[i][j]].uuid));

        mu_check(mdv_hashmap_size(mstroutes) == j);

        mdv_hashmap_release(mstroutes);
    }

    mdv_topology_release(topology);
}

