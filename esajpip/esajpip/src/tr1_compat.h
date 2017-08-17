#ifndef _TR1_COMPAT_H_
#define _TR1_COMPAT_H_

#ifndef _USE_BOOST
#if (__cplusplus >= 201103L) || defined(_NO_TR1)

#include <memory>

#define SHARED_PTR std::shared_ptr
#else
#include <tr1/memory>
#define SHARED_PTR std::tr1::shared_ptr
#endif
#else
#include <boost/tr1/memory.hpp>
#endif

#endif
