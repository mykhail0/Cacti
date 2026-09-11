#ifndef UTILITY_H
#define UTILITY_H

#include <pthread.h>

// Wrappers for pthread functions with panicking error handling.

extern void join(pthread_t thread);

extern void mutex_attr_destroy(pthread_mutexattr_t* attr);

extern void key_delete(pthread_key_t key);

extern void cond_signal(pthread_cond_t* cond);

extern void cond_destroy(pthread_cond_t* cond);

extern void mutex_lock(pthread_mutex_t* mutex);

extern void mutex_unlock(pthread_mutex_t* mutex);

extern void mutex_destroy(pthread_mutex_t* mutex);

#endif  // UTILITY_H
