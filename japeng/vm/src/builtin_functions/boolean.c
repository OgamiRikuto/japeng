#include "common.h"
#include "vm.h"
#include "builtins.h"

void setup_boolean(VM* vm, TypeInfo* o_info)
{
    TypeInfo* b_info = new_type_info(sym_Boolean, 0);
    vm->class_boolean = new_class(sym_Boolean, b_info, vm->class_object, o_info, 0);
}
