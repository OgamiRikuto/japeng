#include <GLFW/glfw3.h>
#include <stdint.h>
#include <stdbool.h>
#include "vm.h"
#include "vm_class.h"
#include "object.h"

extern GLFWwindow* get_window_ptr(ObjInstance* inst);

static uint8_t prev_key_state[GLFW_KEY_LAST + 1] = {0};

static bool native_input_is_key_down(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t key       = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    ObjInstance* win_inst = (ObjInstance*)as_obj(self->fields[0]);
    GLFWwindow* win = get_window_ptr(win_inst);

    bool down = false;
    if (win && key >= 0 && key <= GLFW_KEY_LAST) {
        down = (glfwGetKey(win, key) == GLFW_PRESS);
    }
    push(vm, make_bool(down));
    return true;
}

static bool native_input_is_key_pressed(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t key       = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    ObjInstance* win_inst = (ObjInstance*)as_obj(self->fields[0]);
    GLFWwindow* win = get_window_ptr(win_inst);

    bool pressed = false;
    if (win && key >= 0 && key <= GLFW_KEY_LAST) {
        bool curr = (glfwGetKey(win, key) == GLFW_PRESS);
        pressed = (curr && !prev_key_state[key]);
        prev_key_state[key] = curr ? 1 : 0;
    }
    push(vm, make_bool(pressed));
    return true;
}

void register_input_natives(ObjClass* klass) {
    vm_define_native(klass, "isKeyDown",    native_input_is_key_down);
    vm_define_native(klass, "isKeyPressed", native_input_is_key_pressed);
}

static void set_static_int(ObjClass* klass, const char* name, int32_t val) {
    Symbol* sym = intern_cstr(name);
    for (uint16_t i = 0; i < klass->static_field_count; i++) {
        if (klass->static_field_names[i] == sym) {
            klass->static_fields[i] = make_int(val);
            return;
        }
    }
}

void register_key_natives(ObjClass* klass) {
    set_static_int(klass, "space",  GLFW_KEY_SPACE);
    set_static_int(klass, "enter",  GLFW_KEY_ENTER);
    set_static_int(klass, "escape", GLFW_KEY_ESCAPE);
    set_static_int(klass, "tab",    GLFW_KEY_TAB);
    set_static_int(klass, "right",  GLFW_KEY_RIGHT);
    set_static_int(klass, "left",   GLFW_KEY_LEFT);
    set_static_int(klass, "down",   GLFW_KEY_DOWN);
    set_static_int(klass, "up",     GLFW_KEY_UP);
    set_static_int(klass, "a", GLFW_KEY_A);
    set_static_int(klass, "c", GLFW_KEY_C);
    set_static_int(klass, "d", GLFW_KEY_D);
    set_static_int(klass, "p", GLFW_KEY_P);
    set_static_int(klass, "q", GLFW_KEY_Q);
    set_static_int(klass, "r", GLFW_KEY_R);
    set_static_int(klass, "s", GLFW_KEY_S);
    set_static_int(klass, "w", GLFW_KEY_W);
    set_static_int(klass, "x", GLFW_KEY_X);
    set_static_int(klass, "z", GLFW_KEY_Z);
}
