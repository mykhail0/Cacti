#ifndef ERROR_H
#define ERROR_H

// Print system call error message and terminate.
extern void syserr(int bl, const char* fmt, ...);

// Print error message and terminate.
extern void fatal(const char* fmt, ...);

#endif  // ERROR_H
