#include "utility.h"

#include "error.h"

void join(pthread_t thread) {
  int ret = pthread_join(thread, NULL);
  if (ret != 0) syserr(ret, "pthread_join() fail.\n");
}

void mutex_attr_destroy(pthread_mutexattr_t* attr) {
  int ret = pthread_mutexattr_destroy(attr);
  if (ret != 0) syserr(ret, "Mutexattr destroy failed.\n");
}

void key_delete(pthread_key_t key) {
  int ret = pthread_key_delete(key);
  if (ret != 0) syserr(ret, "Failed key delete.\n");
}

void cond_signal(pthread_cond_t* cond) {
  if (pthread_cond_signal(cond) != 0) {
    fatal("pthread_cond_signal() should never return an error code.\n");
  }
}

void cond_destroy(pthread_cond_t* cond) {
  int ret = pthread_cond_destroy(cond);
  if (ret != 0) syserr(ret, "pthread_cond_destroy() fail.\n");
}

void mutex_lock(pthread_mutex_t* mutex) {
  int ret = pthread_mutex_lock(mutex);
  if (ret != 0) syserr(ret, "Mutex lock failed.\n");
}

void mutex_unlock(pthread_mutex_t* mutex) {
  int ret = pthread_mutex_unlock(mutex);
  if (ret != 0) syserr(ret, "Mutex unlock failed.\n");
}

void mutex_destroy(pthread_mutex_t* mutex) {
  int ret = pthread_mutex_destroy(mutex);
  if (ret != 0) syserr(ret, "Mutex destroy failed\n");
}
