/**
 * @file mdv_datum.h
 * @author Vladislav Volkov (wwwvladislav@gmail.com)
 * @brief Field data storage for given type.
 * @version 0.1
 * @date 2020-02-20
 *
 * @copyright Copyright (c) 2020, Vladislav Volkov
 *
 */
#pragma once
#include "mdv_field.h"
#include <mdv_alloc.h>


/// Field data storage
typedef struct mdv_datum mdv_datum;


#define mdv_datum_constructor(T, name)  \
    mdv_datum * mdv_datum_##name##_create(T const *v, uint32_t count, mdv_allocator const *allocator)

#define mdv_datum_destructor(name)  \
    void mdv_datum_##name##_free(mdv_datum *datum)


mdv_datum_constructor(bool,         bool);
mdv_datum_constructor(char,         char);
mdv_datum_constructor(uint8_t,      byte);
mdv_datum_constructor(int8_t,       int8);
mdv_datum_constructor(uint8_t,      uint8);
mdv_datum_constructor(int16_t,      int16);
mdv_datum_constructor(uint16_t,     uint16);
mdv_datum_constructor(int32_t,      int32);
mdv_datum_constructor(uint32_t,     uint32);
mdv_datum_constructor(int64_t,      int64);
mdv_datum_constructor(uint64_t,     uint64);
mdv_datum_constructor(float,        float);
mdv_datum_constructor(double,       double);


mdv_datum_destructor(bool);
mdv_datum_destructor(char);
mdv_datum_destructor(byte);
mdv_datum_destructor(int8);
mdv_datum_destructor(uint8);
mdv_datum_destructor(int16);
mdv_datum_destructor(uint16);
mdv_datum_destructor(int32);
mdv_datum_destructor(uint32);
mdv_datum_destructor(int64);
mdv_datum_destructor(uint64);
mdv_datum_destructor(float);
mdv_datum_destructor(double);

