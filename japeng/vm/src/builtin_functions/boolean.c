#include "common.h"
#include "vm.h"
#include "builtins.h"

void setup_boolean(VM* vm)
{
    vm->class_boolean = vm_new_class(sym_Boolean, vm->class_object);
    table_set(vm->globals, sym_Boolean, make_obj((Obj*)vm->class_boolean));
}
