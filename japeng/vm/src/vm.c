#include "vm.h"
#include "vm_calc.h"
#include "common.h"
#include "native.h"
#include "builtins.h"
#include "dispatch.h"
#include <stdio.h>
#include <string.h>

#define DEBUG_TRACE_EXECUTION 1

#if DEBUG_TRACE_EXECUTION 
static void print_stack(VM* vm);
static void print_instruction(CallFrame* frame, uint32_t instruction, uint32_t current_ip);

const char* op_kind[OP_MAX] = {
    [OP_CONSTANT]      = "OP_CONSTANT",
    [OP_GET_LOCAL]     = "OP_GET_LOCAL",
    [OP_SET_LOCAL]     = "OP_SET_LOCAL",
    [OP_GET_GLOBAL]    = "OP_GET_GLOBAL",
    [OP_SET_GLOBAL]    = "OP_SET_GLOBAL",
    [OP_GET_FIELD]     = "OP_GET_FIELD",
    [OP_SET_FIELD]    = "OP_SET_FIELD",
    [OP_SEND]          = "OP_SEND",
    [OP_SUPER_CALL]    = "OP_SUPER_CALL",
    [OP_CALL]          = "OP_CALL",
    [OP_POP]           = "OP_POP",
    [OP_DUP]           = "OP_DUP",
    [OP_RETURN]        = "OP_RETURN",
    [OP_JUMP]          = "OP_JUMP",
    [OP_JUMP_IF_FALSE] = "OP_JUMP_IF_FALSE",
    [OP_LOOP]          = "OP_LOOP",
    // 整数算術
    [OP_ADD_INT]       = "OP_ADD_INT",
    [OP_SUB_INT]       = "OP_SUB_INT",
    [OP_MUL_INT]       = "OP_MUL_INT",
    [OP_DIV_INT]       = "OP_DIV_INT",
    [OP_REM_INT]       = "OP_REM_INT",

    // Float 算術
    [OP_ADD_FLOAT]     = "OP_ADD_FLOAT",
    [OP_SUB_FLOAT]     = "OP_SUB_FLOAT",
    [OP_MUL_FLOAT]     = "OP_MUL_FLOAT",
    [OP_DIV_FLOAT]     = "OP_DIV_FLOAT",

    // Decimal 算術
    [OP_ADD_DEC]       = "OP_ADD_DEC",
    [OP_SUB_DEC]       = "OP_SUB_DEC",
    [OP_MUL_DEC]       = "OP_MUL_DEC",
    [OP_DIV_DEC]       = "OP_DIV_DEC",

    // 文字列結合
    [OP_CONCAT]        = "OP_CONCAT",

    // 整数高速比較
    [OP_EQUAL_INT]     = "OP_EQUAL_INT",
    [OP_NEQUAL_INT]    = "OP_NEQUAL_INT",
    [OP_LESS_INT]      = "OP_LESS_INT",
    [OP_GREAT_INT]     = "OP_GREAT_INT",
    [OP_NLESS_INT]     = "OP_NLESS_INT",
    [OP_NGREAT_INT]    = "OP_NGREAT_INT",
    [OP_LESS]         = "OP_LESS",
    [OP_GREAT]        = "OP_GREAT",
    [OP_EQUAL]        = "OP_EQUAL",
    [OP_NEQUAL]       = "OP_NEQUAL",
    [OP_NGREAT]       = "OP_NGREAT",
    [OP_NLESS]        = "OP_NLESS",
    [OP_SHL]           = "OP_SHL",
    [OP_SHR]           = "OP_SHR",
    [OP_BAND]          = "OP_BAND",
    [OP_BOR]           = "OP_BOR",
    [OP_BXOR]          = "OP_BXOR",
    [OP_BNOT]          = "OP_BNOT",
    [OP_NOT]           = "OP_NOT",
    [OP_CLOSURE]      = "OP_CLOSURE",
    [OP_GET_UPVALUE]  = "OP_GET_UPVALUE",
    [OP_NEW_INSTANCE] = "OP_NEW_INSTANCE"
};
#endif

static void reset_stack(VM* vm)
{
    vm->stack_top = vm->stack;
}

void init_vm(VM* vm)
{
    reset_stack(vm);
    vm->globals = new_table(8);
    init_builtin_classes(vm);
}

void free_vm(VM* vm)
{
    table_free(vm->globals);
    (void)vm;
}

void push(VM* vm, Value value)
{
    *vm->stack_top = value;
    vm->stack_top++;
}

Value pop(VM* vm)
{
    vm->stack_top--;
    return *vm->stack_top;
}

ObjClass* get_class_for_value(VM* vm, Value val) 
{
    if (is_int(val)) {
        return vm->class_integer;
    }
    if (is_float(val)) {
        return vm->class_float;
    }
    if (is_bool(val)) {
        return vm->class_boolean;
    }

    if (is_obj(val)) {
        Obj* obj = as_obj(val);
        switch (obj->type) {
            case OBJ_BIGINTEGER:
                return vm->class_integer;
            case OBJ_DECIMAL:
                return vm->class_decimal;
            case OBJ_INSTANCE:
                return ((ObjInstance*)obj)->klass;
            case OBJ_LIST:
                return vm->class_list;
            case OBJ_STRING:
                return vm->class_string;
            default:
                break;
        }
    }
    return vm->class_object;
}

static inline bool is_falsy(Value value) {
    if (is_nil(value)) return true;
    if ((is_bool(value) && !as_bool(value))) return true;
    if (is_int(value) && as_int(value) == 0) return true;
    return false;
}

static InterpretResult run(VM* vm, int stop_frame_count)
{
    CallFrame* frame = &vm->frames[vm->frame_count - 1]; 
    for(;;) {
#if DEBUG_TRACE_EXECUTION
        print_stack(vm);
        uint32_t current_ip = (uint32_t)(frame->ip - frame->chunk->code);
        uint32_t instruction_preview = *frame->ip;
        print_instruction(frame, instruction_preview, current_ip);
#endif
       if (frame->ip >= frame->chunk->code + frame->chunk->count) {
            error_runtime(ERR_FELL_OFF_END);
            return INTERPRET_RUNTIME_ERROR;
        }
        uint32_t instruction = *frame->ip++;
        Opcode op = get_op(instruction);

        switch(op) {
            case OP_CONSTANT: {
                uint32_t index = get_operand(instruction);
                Value constant = frame->chunk->constants[index];
                push(vm, constant);
                break;
            }
            case OP_GET_LOCAL: {
                uint32_t slot = get_operand(instruction);
                push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint32_t slot = get_operand(instruction);
                frame->slots[slot] = peek(vm ,0);
                break;
            }
            case OP_GET_GLOBAL: {
                uint32_t name_idx = get_operand(instruction);
                ObjString* name = (ObjString*)as_obj(frame->chunk->constants[name_idx]);
                Value value;
                if (!table_get(vm->globals, name, &value)) {
                    error_runtime(ERR_UNDEFINED_GLOBAL, name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                uint32_t name_idx = get_operand(instruction);
                ObjString* name = (ObjString*)as_obj(frame->chunk->constants[name_idx]);
                Value val = peek(vm, 0);

                if (is_obj(val) && ((Obj*)as_obj(val))->type == OBJ_CLASS) {
                    ObjClass* klass = (ObjClass*)as_obj(val);
                    if (klass != vm->class_object && klass->superclass == NULL) {
                        klass->superclass = vm->class_object;
                        klass->superclass_type = new_type_info(sym_Object, 0);
                    }
                }
                table_set(vm->globals, name, peek(vm, 0));
                break;
            }
            case OP_GET_FIELD: {
                Value receiver = pop(vm);

                if (is_nil(receiver)) {
                    error_runtime(ERR_NULL_REFERENCE, "field");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                if (!is_obj(receiver) || ((Obj*)as_obj(receiver))->type != OBJ_INSTANCE) {
                    error_runtime(ERR_CANNOT_ACCESS_FIELD_ON_CLASS); 
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                uint32_t field_idx = get_operand(instruction);
                ObjInstance* instance = (ObjInstance*)as_obj(receiver);
                push(vm, instance->fields[field_idx]);
                break;
            }
            case OP_SET_FIELD: {
                uint32_t field_idx = get_operand(instruction);
                Value value = pop(vm);
                Value receiver = pop(vm);

                if (is_nil(receiver)) {
                    error_runtime(ERR_NULL_REFERENCE, "field");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = (ObjInstance*)as_obj(receiver);
                instance->fields[field_idx] = value;
                push(vm, value);
                break;
            }
            case OP_NEW_INSTANCE: {
                Value class_val = pop(vm);
                if (!is_obj(class_val) || ((Obj*)as_obj(class_val))->type != OBJ_CLASS) {
                    error_runtime(ERR_CANNOT_INSTANTIATE);
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClass* klass = (ObjClass*)as_obj(class_val);
                if (klass != NULL && klass != vm->class_object && klass->superclass == NULL) {
                    klass->superclass = vm->class_object;
                    klass->superclass_type = new_type_info(sym_Object, 0);
                }
                if (klass == vm->class_list) {
                    push(vm, make_obj((Obj*)new_list()));
                } else {
                    push(vm, make_obj((Obj*)new_instance(klass)));
                }
                break;
            }
            case OP_CLOSURE: {
                uint32_t fn_idx = get_operand(instruction);
                ObjFunction* fn = (ObjFunction*)as_obj(frame->chunk->constants[fn_idx]);
                ObjClosure* closure = new_closure(fn);

                for (int i = 0; i < fn->upvalue_count; i++) {
                    uint8_t index = fn->upvalues[i].index;
                    if (fn->upvalues[i].is_local) {
                        closure->captures[i] = frame->slots[index];
                    } else {
                        closure->captures[i] = frame->closure->captures[index];
                    }
                }
                push(vm, make_obj((Obj*)closure));
                break;
            }
            case OP_GET_UPVALUE: {
                uint32_t slot = get_operand(instruction);
                push(vm, frame->closure->captures[slot]);
                break;
            }
            case OP_SET_UPVALUE: {
                uint32_t slot = get_operand(instruction);
                Value value = peek(vm, 0); // または pop(vm)
                // クロージャのキャプチャ配列に書き戻し
                frame->closure->captures[slot] = value; 
                break;
            }
            case OP_JUMP: {
                uint32_t offset = get_operand(instruction);
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint32_t offset = get_operand(instruction);
                Value condition = peek(vm, 0);
                if (is_falsy(condition)) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint32_t offset = get_operand(instruction);
                frame->ip -= offset;
                break;
            }
            case OP_SEND: {
                uint8_t arg_count = get_send_args(instruction);
                uint16_t msg_index = get_send_index(instruction);

                if (!dispatch_send(vm, arg_count, msg_index)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_SUPER_CALL: {
                uint8_t arg_count = get_send_args(instruction);
                uint16_t fn_index = get_send_index(instruction);

                // 実行時探索は 0 回！コンパイル時に特定済みの関数を直接取得
                Value method_val = frame->chunk->constants[fn_index];

                // ネイティブメソッドの場合
                if (is_obj(method_val) && ((Obj*)as_obj(method_val))->type == OBJ_NATIVE) {
                    ObjNative* native = (ObjNative*)as_obj(method_val);
                    if (!native->function(vm, arg_count)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                // JapEng の関数 (ObjFunction) の場合
                ObjFunction* func = (ObjFunction*)as_obj(method_val);

                if (vm->frame_count >= FRAME_MAX) {
                    error_runtime(ERR_STACK_OVERFLOW);
                    return INTERPRET_RUNTIME_ERROR;
                }

                CallFrame* next_frame = &vm->frames[vm->frame_count++];
                next_frame->closure = NULL;
                next_frame->chunk = func->chunk;
                next_frame->ip = func->chunk->code;
                next_frame->slots = vm->stack_top - (arg_count + 1);

                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            case OP_POP:
                pop(vm);
                break;
            case OP_DUP: 
                push(vm, peek(vm, 0));
                break; 
            case OP_RETURN: {
                uint8_t ret_count = (uint8_t)get_operand(instruction);
                Value ret_values[16];

                // 1. スタックトップから戻り値を順番に取り出す (逆順に取り出して正順に並べる)
                for (int i = ret_count - 1; i >= 0; i--) {
                    ret_values[i] = pop(vm);
                }

                CallFrame* ending_frame = frame;
                vm->frame_count--;

                // 2. コールフレームのスタックを巻き戻す
                vm->stack_top = ending_frame->slots;

                // 3. 正順 (第1戻り値, 第2戻り値, ...) でスタックに積み直す
                for (int i = 0; i < ret_count; i++) {
                    push(vm, ret_values[i]);
                }

                if (vm->frame_count == stop_frame_count) {
                    return INTERPRET_OK;
                }

                frame = &vm->frames[vm->frame_count - 1];
                break;
            }
            // --- 整数専用算術 ---
            case OP_ADD_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    int32_t ia = as_int(a), ib = as_int(b), res;
                    if (!__builtin_add_overflow(ia, ib, &res)) {
                        push(vm, make_int(res));
                        break;
                    }
                    push(vm, make_obj((Obj*)bi_from_i64((int64_t)ia + (int64_t)ib)));
                    break;
                }
                push(vm, calc_add_int_slow(a, b));
                break;
            }
            case OP_SUB_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    int32_t ia = as_int(a), ib = as_int(b), res;
                    if (!__builtin_sub_overflow(ia, ib, &res)) {
                        push(vm, make_int(res));
                        break;
                    }
                    push(vm, make_obj((Obj*)bi_from_i64((int64_t)ia - (int64_t)ib)));
                    break;
                }
                push(vm, calc_sub_int_slow(a, b));
                break;
            }
            case OP_MUL_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    int32_t ia = as_int(a), ib = as_int(b), res;
                    if (!__builtin_mul_overflow(ia, ib, &res)) {
                        push(vm, make_int(res));
                        break;
                    }
                }
                push(vm, calc_mul_int_slow(a, b));
                break;
            }
            case OP_DIV_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    int32_t ia = as_int(a), ib = as_int(b);
                    if (ib == 0) {
                        error_runtime(ERR_DIVISION_BY_ZERO);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    if (ia == INT32_MIN && ib == -1) {
                        push(vm, make_obj((Obj*)bi_from_i64(-(int64_t)INT32_MIN)));
                        break;
                    }
                    push(vm, make_int(ia / ib));
                    break;
                }
                push(vm, calc_div_int_slow(a, b));
                break;
            }
            case OP_REM_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    int32_t ia = as_int(a), ib = as_int(b);
                    if (ib == 0) {
                        error_runtime(ERR_DIVISION_BY_ZERO);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    push(vm, make_int(ia % ib));
                    break;
                }
                push(vm, calc_rem_int_slow(a, b));
                break;
            }
            case OP_EQUAL_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) == as_int(b)));
                    break;
                }
                push(vm, make_bool(calc_compare_int_slow(a, b) == 0));
                break;
            }
            case OP_NEQUAL_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) != as_int(b)));
                    break;
                }
                push(vm, make_bool(calc_compare_int_slow(a, b) != 0));
                break;
            }
            case OP_LESS_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) < as_int(b)));
                    break;
                }
                push(vm, make_bool(calc_compare_int_slow(a, b) < 0));
                break;
            }
            case OP_GREAT_INT: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) > as_int(b)));
                    break;
                }
                push(vm, make_bool(calc_compare_int_slow(a, b) > 0));
                break;
            }
            case OP_NLESS_INT: { // <=
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) <= as_int(b)));
                    break;
                }
                push(vm, make_bool(calc_compare_int_slow(a, b) <= 0));
                break;
            }
            case OP_NGREAT_INT: { // >=
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_bool(as_int(a) >= as_int(b)));
                    break;
                }
                push(vm, make_bool(calc_compare_int_slow(a, b) >= 0));
                break;
            }
            // --- Float 専用 ---
            case OP_ADD_FLOAT: {
                double b = as_float(pop(vm));
                double a = as_float(pop(vm));
                push(vm, make_float(a + b));
                break;
            }
            case OP_SUB_FLOAT: {
                double b = as_float(pop(vm));
                double a = as_float(pop(vm));
                push(vm, make_float(a - b));
                break;
            }
            case OP_MUL_FLOAT: {
                double b = as_float(pop(vm));
                double a = as_float(pop(vm));
                push(vm, make_float(a * b));
                break;
            }
            case OP_DIV_FLOAT: {
                double b = as_float(pop(vm));
                double a = as_float(pop(vm));
                push(vm, make_float(a / b));
                break;
            }

            // --- Decimal 専用 ---
            case OP_ADD_DEC: {
                ObjDecimal* b = as_decimal(pop(vm));
                ObjDecimal* a = as_decimal(pop(vm));
                push(vm, make_obj((Obj*)dec_add(a, b)));
                break;
            }
            case OP_SUB_DEC: {
                ObjDecimal* b = as_decimal(pop(vm));
                ObjDecimal* a = as_decimal(pop(vm));
                push(vm, make_obj((Obj*)dec_sub(a, b)));
                break;
            }
            case OP_MUL_DEC: {
                ObjDecimal* b = as_decimal(pop(vm));
                ObjDecimal* a = as_decimal(pop(vm));
                push(vm, make_obj((Obj*)dec_mul(a, b)));
                break;
            }
            case OP_DIV_DEC: {
                ObjDecimal* b = as_decimal(pop(vm));
                ObjDecimal* a = as_decimal(pop(vm));
                push(vm, make_obj((Obj*)dec_div(a, b, 28))); // 規定精度 28 桁
                break;
            }

            // --- 文字列結合 ---
            case OP_CONCAT: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, calc_concat(a, b));
                break;
            }
            case OP_EQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(calc_compare(a, b) == 0));
                break;
            }
            case OP_NEQUAL: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(calc_compare(a, b) != 0));
                break;
            }
            case OP_LESS: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(calc_compare(a, b) < 0));
                break;
            }
            case OP_GREAT: {
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(calc_compare(a, b) > 0));
                break;
            }
            case OP_NLESS: { // <=
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(calc_compare(a, b) <= 0));
                break;
            }
            case OP_NGREAT: { // >=
                Value b = pop(vm);
                Value a = pop(vm);
                push(vm, make_bool(calc_compare(a, b) >= 0));
                break;
            }
            case OP_SHL: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) << as_int(b)));
                    break;
                }
                if (is_int(b)) {
                    push(vm, make_obj((Obj*)bi_shl((BigInteger*)as_obj(a), (size_t)as_int(b))));
                    break;
                }
                push(vm, make_int(0));
                break;
            }
            case OP_SHR: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) >> as_int(b)));
                    break;
                }
                if (is_int(b)) {
                    push(vm, make_obj((Obj*)bi_shr((BigInteger*)as_obj(a), (size_t)as_int(b))));
                    break;
                }
                push(vm, make_int(0));
                break;
            }
            case OP_BAND: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) & as_int(b)));
                    break;
                }
                push(vm, make_int(0));
                break;
            }
            case OP_BOR: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) | as_int(b)));
                    break;
                }
                push(vm, make_int(0));
                break;
            }
            case OP_BXOR: {
                Value b = pop(vm);
                Value a = pop(vm);
                if (is_int(a) && is_int(b)) {
                    push(vm, make_int(as_int(a) ^ as_int(b)));
                    break;
                }
                push(vm, make_int(0));
                break;
            }
            case OP_BNOT: {
                Value a = pop(vm);
                if (is_int(a)) {
                    push(vm, make_int(~as_int(a)));
                    break;
                }
                push(vm, make_int(0));
                break;
            }
            case OP_NOT: {
                Value a = pop(vm);
                if (is_bool(a)) {
                    push(vm, make_bool(!as_bool(a)));
                    break;
                }
                // is_falsy に従って反転フォールバック
                push(vm, make_bool(is_falsy(a)));
                break;
            }
            default: 
                error_runtime(ERR_UNKNOWN_OPCODE, op);
                return INTERPRET_RUNTIME_ERROR;
        }
    }
}

InterpretResult interpret(VM* vm, Chunk* chunk)
{
    reset_stack(vm);
    ObjInstance* top_level_self = new_instance(vm->class_object);
    push(vm, make_obj((Obj*)top_level_self));
    
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = NULL;
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->slots = vm->stack;
    return run(vm, 0);
}

// C言語から JapEng のクロージャ/関数をで実行し、戻り値を受け取る
bool vm_call_function(VM* vm, Value func_val, uint8_t arg_count, Value* args, Value* out_result)
{
    if (!is_obj(func_val)) {
        error_runtime(ERR_TYPE_OP_ADD);
        return false;
    }

    Obj* obj = as_obj(func_val);
    ObjFunction* func = NULL;
    ObjClosure* closure = NULL;

    if (obj->type == OBJ_CLOSURE) {
        closure = (ObjClosure*)obj;
        func = closure->function;
    } else if (obj->type == OBJ_FUNCTION) {
        func = (ObjFunction*)obj;
    } else {
        error_runtime(ERR_TYPE_OP_ADD);
        return false;
    }

    // ★ 引数の個数を柔軟にチェック
    if (func->arity != arg_count) {
        error_runtime(ERR_WRONG_ARG_COUNT, func->arity, arg_count);
        return false;
    }

    if (vm->frame_count >= FRAME_MAX) {
        error_runtime(ERR_STACK_OVERFLOW);
        return false;
    }

    // 1. レシーバ (クロージャ/関数) を push
    push(vm, func_val);

    // 2. 引数を順番にすべて push
    for (int i = 0; i < arg_count; i++) {
        push(vm, args[i]);
    }

    int stop_frame_count = vm->frame_count;

    CallFrame* next_frame = &vm->frames[vm->frame_count++];
    next_frame->closure = closure;
    next_frame->chunk = func->chunk;
    next_frame->ip = func->chunk->code;
    // slots[0] がレシーバ、その後ろに引数が並ぶ
    next_frame->slots = vm->stack_top - (arg_count + 1);

    InterpretResult res = run(vm, stop_frame_count);
    if (res != INTERPRET_OK) {
        return false;
    }

    if (out_result != NULL) {
        *out_result = pop(vm);
    } else {
        pop(vm); // 戻り値が不要な場合 (forEach など) は破棄
    }
    return true;
}

#if DEBUG_TRACE_EXECUTION
static void print_stack(VM* vm) {
    // fprintf(stderr, "\n          ");
    if (vm->stack == vm->stack_top) {
        fprintf(stderr, "[ empty ]\n");
        return;
    }
    for (Value* slot = vm->stack; slot < vm->stack_top; slot++) {
        for (int f = 0; f < vm->frame_count; f++) {
            if (slot == vm->frames[f].slots) {
                fprintf(stderr, "\n          ");
                fprintf(stderr, "| f%d: ", f);
            }
        }
        fprintf(stderr, "[ ");
        if (is_int(*slot)) {
            fprintf(stderr, "%d", as_int(*slot));
        } else if (is_bool(*slot)) {
            fprintf(stderr, "%s", as_bool(*slot) ? "true" : "false");
        } else if (is_nil(*slot)) {
            fprintf(stderr, "nil");
        } else if (is_float(*slot)) {
            fprintf(stderr, "%g", as_float(*slot));
        } else if (is_obj(*slot)) {
            Obj* obj = (Obj*)as_obj(*slot);
            if (obj == NULL) {
                fprintf(stderr, "XXX");
            } else {
                switch (obj->type) {
                    case OBJ_CLASS: {
                        ObjClass* klass = (ObjClass*)obj;
                        fprintf(stderr, "<class %s>", klass->name ? klass->name->chars : "Anon");
                        break;
                    }
                    case OBJ_INSTANCE: {
                        ObjInstance* inst = (ObjInstance*)obj;
                        fprintf(stderr, "<inst %s>", (inst->klass && inst->klass->name) 
                                            ? inst->klass->name->chars : "Anon");
                        break;
                    }
                    case OBJ_BIGINTEGER: {
                        BigInteger* bi = (BigInteger*)obj;
                        fprintf(stderr, "%s", bi_to_string(bi)->chars);
                        break;
                    }
                    case OBJ_DECIMAL: {
                        ObjDecimal* dec = (ObjDecimal*)obj;
                        fprintf(stderr, "%s", dec_toString(dec)->chars);
                        break;
                    }
                    case OBJ_STRING: {
                        ObjString* str = (ObjString*)obj;
                        fprintf(stderr, "\"%s\"", str->chars);
                        break;
                    }
                    case OBJ_LIST: {
                        ObjList* list = (ObjList*)obj;
                        fprintf(stderr, "<list len:%d>", list->size);
                        break;
                    }
                    case OBJ_FUNCTION:
                        fprintf(stderr, "<fn>");
                        break;
                    case OBJ_CLOSURE:
                        fprintf(stderr, "<closure>");
                        break;
                    default:
                        fprintf(stderr, "XXX");
                        break;
                }
            }
        } else {
            fprintf(stderr, "<unknown>");
        }
        fprintf(stderr, " ]");
    }
    fprintf(stderr, "\n\n");
}

static void print_instruction(CallFrame* frame, uint32_t instruction, uint32_t current_ip)
{
    Opcode op = get_op(instruction);
    uint32_t operand = get_operand(instruction);
    const char* name = (op < OP_MAX && op_kind[op]) ? op_kind[op] : "UNKNOWN";

    fprintf(stderr, "%04d  op: %-15s", current_ip, name);

    switch (op) {
        case OP_SEND: {
            uint8_t argc = get_send_args(instruction);
            uint16_t sym_idx = get_send_index(instruction);
            Value sym_val = frame->chunk->constants[sym_idx];
            const char* msg = (is_obj(sym_val) && ((Obj*)as_obj(sym_val))->type == OBJ_STRING)
                              ? ((ObjString*)as_obj(sym_val))->chars : "?";
            fprintf(stderr, "argc: %u, msg: #%s (idx: %u)\n", argc, msg, sym_idx);
            break;
        }
        case OP_SUPER_CALL: {
            uint8_t argc = get_send_args(instruction);
            uint16_t fn_idx = get_send_index(instruction);
            fprintf(stderr, "argc: %u, fn_idx: %u\n", argc, fn_idx);
            break;
        }
        case OP_CONSTANT: {
            Value val = frame->chunk->constants[operand];
            fprintf(stderr, "const[%u] = ", operand);
            if (is_int(val))        fprintf(stderr, "%d\n", as_int(val));
            else if (is_float(val)) fprintf(stderr, "%g\n", as_float(val));
            else if (is_bool(val))  fprintf(stderr, "%s\n", as_bool(val) ? "true" : "false");
            else if (is_nil(val))   fprintf(stderr, "nil\n");
            else if (is_obj(val)) {
                Obj* obj = as_obj(val);
                if (obj->type == OBJ_STRING)   fprintf(stderr, "\"%s\"\n", ((ObjString*)obj)->chars);
                else if (obj->type == OBJ_CLASS) fprintf(stderr, "<class %s>\n", ((ObjClass*)obj)->name->chars);
                else fprintf(stderr, "<obj type:%d>\n", obj->type);
            } else {
                fprintf(stderr, "<unknown>\n");
            }
            break;
        }
        case OP_GET_GLOBAL:
        case OP_SET_GLOBAL: {
            Value name_val = frame->chunk->constants[operand];
            const char* gname = (is_obj(name_val) && ((Obj*)as_obj(name_val))->type == OBJ_STRING)
                                ? ((ObjString*)as_obj(name_val))->chars : "?";
            fprintf(stderr, "global: '%s' (idx: %u)\n", gname, operand);
            break;
        }
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
            fprintf(stderr, "slot: %u\n", operand);
            break;
        case OP_GET_FIELD:
        case OP_SET_FIELD:
            fprintf(stderr, "field[%u]\n", operand);
            break;
        case OP_GET_UPVALUE:
        case OP_SET_UPVALUE:
            fprintf(stderr, "upvalue[%u]\n", operand);
            break;
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
            fprintf(stderr, "offset: +%u -> %04d\n", operand, current_ip + 1 + operand);
            break;
        case OP_LOOP:
            fprintf(stderr, "offset: -%u -> %04d\n", operand, current_ip + 1 - operand);
            break;
        case OP_POP:
        case OP_DUP:
        case OP_RETURN:
        case OP_NEW_INSTANCE:
        case OP_CONCAT:
        case OP_ADD_INT:
        case OP_SUB_INT:
        case OP_MUL_INT:
        case OP_DIV_INT:
        case OP_REM_INT:
        case OP_ADD_FLOAT:
        case OP_SUB_FLOAT:
        case OP_MUL_FLOAT:
        case OP_DIV_FLOAT:
        case OP_ADD_DEC:
        case OP_SUB_DEC:
        case OP_MUL_DEC:
        case OP_DIV_DEC:
        case OP_LESS:
        case OP_GREAT:
        case OP_EQUAL:
        case OP_NEQUAL:
        case OP_NGREAT:
        case OP_NLESS:
        case OP_EQUAL_INT:
        case OP_NEQUAL_INT:
        case OP_LESS_INT:
        case OP_GREAT_INT:
        case OP_NLESS_INT:
        case OP_NGREAT_INT:
        case OP_SHL:
        case OP_SHR:
        case OP_BAND:
        case OP_BOR:
        case OP_BXOR:
        case OP_BNOT:
        case OP_NOT:
            fprintf(stderr, "\n");
            break;
        default:
            fprintf(stderr, "(operand: %u)\n", operand);
            break;
    }
}

#endif
