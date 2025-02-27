#if !defined(BGC__BGC_HPP)
#define BGC__BGC_HPP

#include <bgc.h>

#define bgcxx_new(T)     new (bgc_malloc_ext(BGC_GLOBAL_GC, sizeof(T), ([](void *this_){ std::destroy_at<T>((T *)(this_)); }))) T

#endif // BGC__BGC_HPP
