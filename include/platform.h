#ifndef CUPPLATFORM_H
#define CUPPLATFORM_H

#ifndef thread_local
  #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define thread_local _Thread_local
  #elif defined(__GNUC__) || defined(__clang__)
    #define thread_local __thread
  #elif defined(__MSC_VER)
    #define thread_local __declspec(thread)
  #else
    #error "Thread-local storage not supported by this compiler."
  #endif
#endif



#endif