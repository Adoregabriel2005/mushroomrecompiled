#pragma once
#include <windows.h>
#include <stdint.h>

#define INPUT_UP       0x0001
#define INPUT_DOWN     0x0002
#define INPUT_LEFT     0x0004
#define INPUT_RIGHT    0x0008
#define INPUT_B        0x0010
#define INPUT_A        0x0020
#define INPUT_SELECT   0x0040
#define INPUT_START    0x0080
#define INPUT_TURBO_B  0x0100
#define INPUT_TURBO_A  0x0200
#define INPUT_MENU     0x0400

typedef struct {
    int id;
    int mapped;
    char guid[33];
    char key[512];
    wchar_t name[180];
} InputDevice;

typedef struct {
    int connected;
    int pad;
    int menu;
} InputState;

int input_init(void);
void input_close(void);
int input_devices(InputDevice *out, int capacity);
void input_select(const char *guid);
const char *input_preferred(void);
void input_update(void);
void input_configure(const int b[11]);
void input_set_deadzone(int v);
int input_deadzone(void);
InputState input_read(void);
const wchar_t *input_name(void);
int input_button_count(void);
int input_first_pressed(void);
void input_default_label(int action, wchar_t *out, int capacity);

