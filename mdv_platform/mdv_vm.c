#include "mdv_vm.h"
#include "mdv_log.h"
#include <string.h>


mdv_errno mdv_vm_stack_push(mdv_stack_base *stack, mdv_vm_datum const *data)
{
    return mdv_stack_push(*stack, (char const *)data->data, data->size)
            && mdv_stack_push(*stack, (char const *)&data->size, sizeof(data->size))
            ? MDV_OK
            : MDV_STACK_OVERFLOW;
}


mdv_errno mdv_vm_stack_pop(mdv_stack_base *stack, mdv_vm_datum *data)
{
    uint16_t const *size = mdv_stack_pop(*stack, sizeof(uint16_t));
    if (!size)
        return MDV_FAILED;
    data->size = *size;
    data->data = mdv_stack_pop(*stack, *size);
    return data->data ? MDV_OK : MDV_FAILED;
}


static mdv_errno mdv_vm_stack_top(mdv_stack_base *stack, mdv_vm_datum *data)
{
    if(stack->size < sizeof(data->size))
        return MDV_FAILED;

    char const *top = stack->data + stack->size;
    uint16_t const size = *(uint16_t const *)(top - sizeof(data->size));

    if(size + sizeof(data->size) > stack->size)
        return MDV_FAILED;

    data->size = size;
    data->data = top - sizeof(data->size) - size;

    return MDV_OK;
}


mdv_errno mdv_vm_run(mdv_stack_base *stack, mdv_vm_fn const *fns, size_t fns_count, uint8_t const *program)
{
    mdv_errno err = MDV_OK;

    register uint8_t const *ip = program;

    do
    {
        switch(*ip++)
        {
            case MDV_VM_NOP:
            {
                break;
            }

            case MDV_VM_PUSH:
            {
                mdv_vm_datum datum =
                {
                    .size = *(uint16_t*)ip
                };

                ip += sizeof(uint16_t);

                datum.data = ip;

                err = mdv_vm_stack_push(stack, &datum);

                if (err == MDV_OK)
                    ip += datum.size;
                else
                    ip = 0;

                break;
            }

            case MDV_VM_END:
            {
                ip = 0;
                break;
            }

            case MDV_VM_CALL:
            {
                uint16_t const id = *(uint16_t*)ip;
                ip += sizeof id;

                if (id >= fns_count)
                {
                    ip = 0;
                    err = MDV_NO_IMPL;
                    break;
                }

                err = fns[id](stack);

                if (err != MDV_OK)
                    ip = 0;

                break;
            }

            default:
            {
                MDV_LOGE("Unknown instruction type");
                ip = 0;
                err = MDV_FAILED;
                break;
            }
        }
    }
    while(ip);

    return err;
}


mdv_errno mdv_vm_result_as_bool(mdv_stack_base *stack, bool *res)
{
    if (mdv_stack_size(*stack) < sizeof(uint16_t) + sizeof(uint8_t))
        return MDV_FAILED;

    char const *top = stack->data + stack->size;

    uint16_t const len = *(uint16_t const*)(top - sizeof(uint16_t));

    if(len != sizeof(uint8_t))
        return MDV_INVALID_TYPE;

    uint8_t const val = *(uint8_t*)(top - sizeof(uint16_t) - sizeof(uint8_t));

    *res = val != MDV_VM_FALSE;

    return MDV_OK;
}


static mdv_errno mdv_vm_stack_pop_pair(mdv_stack_base *stack, mdv_vm_datum ops[2])
{
    mdv_errno err = mdv_vm_stack_pop(stack, &ops[1]);

    if(err != MDV_OK)
        return err;

    err = mdv_vm_stack_pop(stack, &ops[0]);

    if(err != MDV_OK)
        return err;

    return MDV_OK;
}


mdv_errno mdv_vmop_equal(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    uint8_t const res = op[0].size == op[1].size
                    && memcmp(op[0].data, op[1].data, op[0].size) == 0
                    ? MDV_VM_TRUE
                    : MDV_VM_FALSE;

    mdv_vm_datum const data =
    {
        .size = sizeof res,
        .data = &res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_not_equal(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    uint8_t const res = op[0].size != op[1].size
                    || memcmp(op[0].data, op[1].data, op[0].size) != 0
                    ? MDV_VM_TRUE
                    : MDV_VM_FALSE;

    mdv_vm_datum const data =
    {
        .size = sizeof res,
        .data = &res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_greater(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    int const cmp_res = memcmp(op[0].data,
                               op[1].data,
                               op[0].size < op[1].size
                                ? op[0].size
                                : op[1].size);

    uint8_t const res = cmp_res > 0 || (cmp_res == 0 && op[0].size > op[1].size)
                            ? MDV_VM_TRUE
                            : MDV_VM_FALSE;

    mdv_vm_datum const data =
    {
        .size = sizeof res,
        .data = &res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_greater_or_equal(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    int const cmp_res = memcmp(op[0].data,
                               op[1].data,
                               op[0].size < op[1].size
                                ? op[0].size
                                : op[1].size);

    uint8_t const res = cmp_res > 0 || (cmp_res == 0 && op[0].size >= op[1].size)
                            ? MDV_VM_TRUE
                            : MDV_VM_FALSE;

    mdv_vm_datum const data =
    {
        .size = sizeof res,
        .data = &res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_less(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    int const cmp_res = memcmp(op[0].data,
                               op[1].data,
                               op[0].size < op[1].size
                                ? op[0].size
                                : op[1].size);

    uint8_t const res = cmp_res < 0 || (cmp_res == 0 && op[0].size < op[1].size)
                            ? MDV_VM_TRUE
                            : MDV_VM_FALSE;

    mdv_vm_datum const data =
    {
        .size = sizeof res,
        .data = &res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_less_or_equal(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    int const cmp_res = memcmp(op[0].data,
                               op[1].data,
                               op[0].size < op[1].size
                                ? op[0].size
                                : op[1].size);

    uint8_t const res = cmp_res < 0 || (cmp_res == 0 && op[0].size <= op[1].size)
                            ? MDV_VM_TRUE
                            : MDV_VM_FALSE;

    mdv_vm_datum const data =
    {
        .size = sizeof res,
        .data = &res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_and(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    if(op[0].size != op[1].size)
        return MDV_INVALID_ARG;

    uint8_t res[op[0].size];

    size_t const size = op[0].size;
    uint8_t const *lhs = op[0].data;
    uint8_t const *rhs = op[1].data;

    for(size_t i = 0; i < size; ++i)
        res[i] = lhs[i] & rhs[i];

    mdv_vm_datum const data =
    {
        .size = size,
        .data = res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_or(mdv_stack_base *stack)
{
    mdv_vm_datum op[2];

    mdv_errno err = mdv_vm_stack_pop_pair(stack, op);

    if(err != MDV_OK)
        return err;

    if(op[0].size != op[1].size)
        return MDV_INVALID_ARG;

    uint8_t res[op[0].size];

    size_t const size = op[0].size;
    uint8_t const *lhs = op[0].data;
    uint8_t const *rhs = op[1].data;

    for(size_t i = 0; i < size; ++i)
        res[i] = lhs[i] | rhs[i];

    mdv_vm_datum const data =
    {
        .size = size,
        .data = res
    };

    return mdv_vm_stack_push(stack, &data);
}


mdv_errno mdv_vmop_not(mdv_stack_base *stack)
{
    mdv_vm_datum op;

    mdv_errno err = mdv_vm_stack_top(stack, &op);

    if(err != MDV_OK)
        return err;

    uint8_t *data = (uint8_t *)op.data;

    for(size_t i = 0; i < op.size; ++i)
        data[i] = ~data[i];

    return MDV_OK;
}
