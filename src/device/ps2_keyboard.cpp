// ps2_keyboard.cpp - ps2 keyboard driver
#include "includes.h"
#include "arch/x86/irq.h"
#include "arch/x86/pic.h"
#include "core/scheduler.h"
#include "device/ps2_controller.h"
#include "device/ps2_keyboard.h"
#include "device/vga.h"
#include "lib/vector.h"
#include "lib/refcount.h"

// for now, we'll just assume that port1 is always connected to the keyboard.
// Because virtualbox's emulated keyboard apparently does not send ident bytes in response to an 0xF2.

bool shift_stat, alt_stat, ctrl_stat;
char current_state;

ps2_keypress* keystroke_buffer[256];
int keystroke_buffer_offset = 0;

process *keyboard_input_process;

char shift_char(char in) {
    switch(in) {
        case '1':
            return '!';
        case '2':
            return '@';
        case '3':
            return '#';
        case '4':
            return '$';
        case '5':
            return '%';
        case '6':
            return '^';
        case '7':
            return '&';
        case '8':
            return '*';
        case '9':
            return '(';
        case '0':
            return ')';
        case '`':
            return '~';
        case '\'':
            return '"';
        case '-':
            return '_';
        case '=':
            return '+';
        case ';':
            return ':';
        case ',':
            return '<';
        case '.':
            return '>';
        case '/':
            return '?';
        case ' ':
            return ' ';
        default:
            return in & ~(1<<5);
    }
}

ps2_keypress* convert_scancode(bool f0, bool e0, unsigned char code_end) {
    unsigned char keycode = 0;
    ps2_keypress* kp = new ps2_keypress;
    if(e0) {
        if(code_end > 0x7D) {
            keycode = 0xFF;
#ifdef DEBUG
            kprintf("Unknown extd-scancode received from keyboard. code_end=0x%x\n", code_end);
#endif
        } else {
            keycode = extd_scancodes[code_end];
        }
    } else {
        if(code_end > 0x83) {
            keycode = 0xFF;
#ifdef DEBUG
            kprintf("Unknown base-scancode received from keyboard. code_end=0x%x\n", code_end);
#endif
        } else {
            keycode = base_scancodes[code_end];
        }
    }
    if(keycode == KEY_Lctrl || keycode == KEY_Rctrl) {
        ctrl_stat = !f0;
    }
    if(keycode == KEY_Lshift || keycode == KEY_Rshift) {
        shift_stat = !f0;
    }
    if(keycode == KEY_Lalt || keycode == KEY_Ralt) {
        alt_stat = !f0;
    }
    if(keycode >=0x20 && keycode <=0x7E)
        kp->is_ascii = true;
    else
        kp->is_ascii = false;
    kp->key = keycode;
    if(shift_stat)
        kp->character = shift_char(keycode);
    else
        kp->character = keycode;
    kp->shift = shift_stat;
    kp->ctrl = ctrl_stat;
    kp->alt = alt_stat;
    kp->released = f0;
    return kp;
}

ps2_keypress* ps2_keyboard_get_keystroke() { // blocks for a keystroke
    ps2_keypress* data;
    set_message_listen_status( "keypress", true );
    while(true) {
        message* msg = wait_for_message();
        if( strcmp(const_cast<char*>(msg->type), const_cast<char*>("keypress")) ) {
            data = (ps2_keypress*)(msg->data);
            msg->data = NULL;
            delete msg;
            return data;
        }
        delete msg;
    }
    
}

void ps2_keyboard_input_process() {
    bool e0 = false;
    bool f0 = false;
    
    while(true) {
        unsigned char data = ps2_receive_byte(false);
        if(data == 0xE0) {
            e0 = true;
        } else if(data == 0xF0) {
            f0 = true;
        } else {
            ps2_keypress* key = convert_scancode(f0, e0, data);
            message msg( "keypress", key, sizeof(ps2_keypress) );
            send_message( msg );
            e0 = false;
            f0 = false;
        }
    }
}


void ps2_keyboard_initialize() {
    //uint16_t port1_ident = ps2_get_ident_bytes(false);
    //if(port1_ident != 0xFFFF) {
#ifdef DEBUG
        terminal_writestring("Initializing port 1 keyboard.\n");
#endif
        ps2_send_byte(0xF0, false); // 0xF0 - Set scancode set
        ps2_wait_for_input();
        ps2_send_byte(0x02, false); // sub-command for the above
        ps2_wait_for_input();
        ps2_send_byte(0xF4, false); // 0xF4 - Enable scanning
        ps2_wait_for_input();
    //}
    keyboard_input_process = new process( (size_t)&ps2_keyboard_input_process, false, 0, "ps2kb_in" ,NULL, 0 );
    spawn_process( keyboard_input_process, true );
    register_channel( "keypress", CHANNEL_MODE_MULTICAST, keyboard_input_process );
}

char* ps2_keyboard_readline(unsigned int *len) {
    vector<char> buffer;
    while(true) {
        unique_ptr<ps2_keypress> kp;
        kp = ps2_keyboard_get_keystroke();
        if(!kp->released) {
            if(kp->key == KEY_Enter) {
                terminal_putchar('\n');
                break;
            } else if(kp->key == KEY_Bksp && buffer.length() > 0) {
                buffer.remove_end();
                terminal_backspace();
            } else if(kp->is_ascii) {
                buffer.add_end(kp->character);
                terminal_putchar(kp->character);
            }
        }
    }
    char *ret = (char*)kmalloc(buffer.length());
    if(ret) {
        for(unsigned int i=0;i<buffer.length();i++) {
            ret[i] = buffer[i];
        }
        *len = buffer.length();
        return ret;
    } else {
        *len = -1;
        return NULL;
    }
}