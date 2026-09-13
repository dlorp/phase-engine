/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Minimal newlib syscall stubs for the integer FFT target harness (Unicorn
 * Cortex-M0 emulation). Semihosting only for _write and _exit; the rest
 * are dead stubs the harness never reaches.
 */

#include <stdint.h>
#include <sys/stat.h>

extern void semihost_write(const void *buf, int n);
extern void semihost_exit(uint32_t reason);

int _write(int fd, const void *buf, int n) {
    (void)fd;
    semihost_write(buf, n);
    return n;
}

void _exit(int code) {
    semihost_exit((uint32_t)code);
    for (;;) {
    }
}

void _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
}

int _getpid(void) {
    return 1;
}

int _close(int fd) {
    (void)fd;
    return -1;
}

int _lseek(int fd, int off, int whence) {
    (void)fd;
    (void)off;
    (void)whence;
    return -1;
}

int _read(int fd, void *buf, int n) {
    (void)fd;
    (void)buf;
    (void)n;
    return -1;
}

int _fstat(int fd, struct stat *st) {
    (void)fd;
    (void)st;
    return -1;
}

int _isatty(int fd) {
    (void)fd;
    return 1;
}

void *_sbrk(int incr) {
    /* Bump allocator from end of .bss to top of the 16KB RAM window.
     * printf needs a small heap for stdio buffers. */
    extern char end[];
    static char *heap_end = 0;
    if (heap_end == 0) {
        heap_end = end;
    }
    char *prev = heap_end;
    char *next = prev + incr;
    if ((uintptr_t)next > 0x20004000u) {
        return (void *)-1;
    }
    heap_end = next;
    return prev;
}
