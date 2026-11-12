#include <GLFW/glfw3.h>
#include <stdint.h>
#include <stdbool.h>
#include "vm.h"
#include "vm_class.h"
#include "object.h"

static bool glfw_initialized = false;

// ポインタの分解・復元 (File クラスと同じ ptr_hi / ptr_lo 方式)
static void store_window_ptr(ObjInstance* inst, GLFWwindow* win) {
    uintptr_t ptr = (uintptr_t)win;
    inst->fields[0] = make_int((int32_t)(uint32_t)(ptr >> 32));
    inst->fields[1] = make_int((int32_t)(uint32_t)(ptr & 0xFFFFFFFFULL));
}

GLFWwindow* get_window_ptr(ObjInstance* inst) {
    if (inst == NULL) return NULL;
    uint64_t hi = (uint32_t)as_int(inst->fields[0]);
    uint64_t lo = (uint32_t)as_int(inst->fields[1]);
    return (GLFWwindow*)(uintptr_t)((hi << 32) | lo);
}

static void unpack_rgb(int32_t color, float* r, float* g, float* b) {
    *r = (float)((color >> 16) & 0xFF) / 255.0f;
    *g = (float)((color >> 8)  & 0xFF) / 255.0f;
    *b = (float)(color & 0xFF)         / 255.0f;
}

// Window.init(width, height, title, resizable, x, y)
static bool native_window_init(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t y         = as_int(pop(vm));
    int32_t x         = as_int(pop(vm));
    bool    resizable = as_bool(pop(vm));
    ObjString* title  = (ObjString*)as_obj(pop(vm));
    int32_t height    = as_int(pop(vm));
    int32_t width     = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));

    if (!glfw_initialized) {
        if (!glfwInit()) {
            push(vm, make_nil());
            return false;
        }
        glfw_initialized = true;
    }

    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(width, height, string_chars(title), NULL, NULL);
    if (!win) {
        push(vm, make_nil());
        return false;
    }

    if (x >= 0 && y >= 0) {
        glfwSetWindowPos(win, x, y);
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1); // VSync ON (60FPS)

    store_window_ptr(self, win);
    push(vm, make_obj((Obj*)self));
    return true;
}

static bool native_window_should_close(VM* vm, uint8_t argc) {
    (void)argc;
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win = get_window_ptr(self);
    push(vm, make_bool(win ? (glfwWindowShouldClose(win) != 0) : true));
    return true;
}

static bool native_window_poll_events(VM* vm, uint8_t argc) {
    (void)argc;
    pop(vm);
    glfwPollEvents();
    glFlush();
    push(vm, make_nil());
    return true;
}

static bool native_window_swap_buffers(VM* vm, uint8_t argc) {
    (void)argc;
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win = get_window_ptr(self);
    if (win) glfwSwapBuffers(win);
    push(vm, make_nil());
    return true;
}

static bool native_window_clear(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t color     = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);

    if (win) {
        // glfwMakeContextCurrent(win);

        // Retina 対応: フレームバッファサイズで Viewport を張り、論理サイズで 2D 正射影
        int fb_w, fb_h, win_w, win_h;
        glfwGetFramebufferSize(win, &fb_w, &fb_h);
        glfwGetWindowSize(win, &win_w, &win_h);
        glViewport(0, 0, fb_w, fb_h);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (double)win_w, (double)win_h, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        float r, g, b;
        unpack_rgb(color, &r, &g, &b);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    push(vm, make_nil());
    return true;
}

static bool native_window_close(VM* vm, uint8_t argc) {
    (void)argc;
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);
    if (win) {
        glfwDestroyWindow(win);
        store_window_ptr(self, NULL);
    }
    push(vm, make_nil());
    return true;
}

static bool native_window_width(VM* vm, uint8_t argc) {
    (void)argc;
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);
    int w = 0, h = 0;
    if (win) glfwGetWindowSize(win, &w, &h);
    push(vm, make_int(w));
    return true;
}

static bool native_window_height(VM* vm, uint8_t argc) {
    (void)argc;
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);
    int w = 0, h = 0;
    if (win) glfwGetWindowSize(win, &w, &h);
    push(vm, make_int(h));
    return true;
}

static bool native_window_set_size(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t h = as_int(pop(vm));
    int32_t w = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win = get_window_ptr(self);
    if (win) glfwSetWindowSize(win, w, h);
    push(vm, make_nil());
    return true;
}

static bool native_window_set_position(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t y = as_int(pop(vm));
    int32_t x = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win = get_window_ptr(self);
    if (win) glfwSetWindowPos(win, x, y);
    push(vm, make_nil());
    return true;
}

static bool native_window_set_title(VM* vm, uint8_t argc) {
    (void)argc;
    ObjString* title  = (ObjString*)as_obj(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);
    if (win) glfwSetWindowTitle(win, string_chars(title));
    push(vm, make_nil());
    return true;
}

static bool native_window_fill_rect(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t color     = as_int(pop(vm));
    double  h         = as_float(pop(vm));
    double  w         = as_float(pop(vm));
    double  y         = as_float(pop(vm));
    double  x         = as_float(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);

    if (win) {
        float r, g, b;
        unpack_rgb(color, &r, &g, &b);
        glColor3f(r, g, b);
        glBegin(GL_QUADS);
            glVertex2d(x,     y);
            glVertex2d(x + w, y);
            glVertex2d(x + w, y + h);
            glVertex2d(x,     y + h);
        glEnd();
    }

    push(vm, make_nil());
    return true;
}

static bool native_window_draw_rect(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t color     = as_int(pop(vm));
    int32_t h         = as_int(pop(vm));
    int32_t w         = as_int(pop(vm));
    int32_t y         = as_int(pop(vm));
    int32_t x         = as_int(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);

    if (win) {
        float r, g, b;
        unpack_rgb(color, &r, &g, &b);
        glColor3f(r, g, b);
        glBegin(GL_QUADS);
            glVertex2d((double)x,     (double)y);
            glVertex2d((double)(x+w), (double)y);
            glVertex2d((double)(x+w), (double)(y+h));
            glVertex2d((double)x,     (double)(y+h));
        glEnd();
    }

    push(vm, make_nil());
    return true;
}

static bool native_window_draw_line(VM* vm, uint8_t argc) {
    (void)argc;
    int32_t color     = as_int(pop(vm));
    double  y2        = as_float(pop(vm));
    double  x2        = as_float(pop(vm));
    double  y1        = as_float(pop(vm));
    double  x1        = as_float(pop(vm));
    ObjInstance* self = (ObjInstance*)as_obj(pop(vm));
    GLFWwindow* win   = get_window_ptr(self);

    if (win) {
        float r, g, b;
        unpack_rgb(color, &r, &g, &b);
        glColor3f(r, g, b);
        glBegin(GL_LINES);
            glVertex2d(x1, y1);
            glVertex2d(x2, y2);
        glEnd();
    }

    push(vm, make_nil());
    return true;
}

void register_window_natives(ObjClass* klass) {
    vm_define_native(klass, "init",        native_window_init);
    vm_define_native(klass, "shouldClose", native_window_should_close);
    vm_define_native(klass, "pollEvents",  native_window_poll_events);
    vm_define_native(klass, "swapBuffers", native_window_swap_buffers);
    vm_define_native(klass, "clear",       native_window_clear);
    vm_define_native(klass, "close",       native_window_close);
    vm_define_native(klass, "width",       native_window_width);
    vm_define_native(klass, "height",      native_window_height);
    vm_define_native(klass, "setSize",     native_window_set_size);
    vm_define_native(klass, "setPosition", native_window_set_position);
    vm_define_native(klass, "setTitle",    native_window_set_title);
    vm_define_native(klass, "fillRect",    native_window_fill_rect);
    vm_define_native(klass, "drawRect", native_window_draw_rect);
    vm_define_native(klass, "drawLine",    native_window_draw_line);
}
