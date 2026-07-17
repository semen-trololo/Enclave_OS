#include "user_libc.h"

// Прямой syscall write для диагностики (в обход printf)
static void write_str(const char* s) {
    int len = 0;
    while(s[len]) len++;
    write(STDOUT_FILENO, s, len);
}

static void write_int(int val) {
    char buf[16];
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else {
        if (val < 0) { write_str("-"); val = -val; }
        char tmp[16]; int j = 0;
        while(val > 0) { tmp[j++] = '0' + (val % 10); val /= 10; }
        while(j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    write_str(buf);
}

int main(int argc, char** argv) {
    write_str("=== DIRECT ARGV TEST ===\n");
    write_str("argc = ");
    write_int(argc);
    write_str("\n");
    
    for (int i = 0; i < argc; i++) {
        write_str("argv[");
        write_int(i);
        write_str("] = ");
        if (argv[i]) write_str(argv[i]);
        else write_str("(null)");
        write_str("\n");
    }
    write_str("========================\n");
    return 0;
}