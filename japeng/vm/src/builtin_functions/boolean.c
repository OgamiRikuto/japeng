#include "common.h"
#include "vm.h"
#include "builtins.h"

static bool native_boolean_toString(VM* vm, uint8_t arg_count)
{
    Value receiver = pop(vm);
    const char* buf = as_bool(receiver) ? "true" : "false";
    push(vm, make_obj((Obj*)gc_new_string(&vm->heap, buf, strlen(buf))));
    return true;
}

void setup_boolean(VM* vm)
{
    vm->class_boolean = vm_new_class(sym_Boolean, vm->class_object);
    table_set(vm->globals, sym_Boolean, make_obj((Obj*)vm->class_boolean));
    vm_define_native(vm->class_boolean, "toString", native_boolean_toString);
}
