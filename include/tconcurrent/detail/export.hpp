#ifndef TCONCURRENT_DETAIL_EXPORT_HPP
#define TCONCURRENT_DETAIL_EXPORT_HPP

#if defined(_WIN32) && defined(tconcurrent_EXPORTS)
#define TCONCURRENT_EXPORT __declspec(dllexport)
#else
#define TCONCURRENT_EXPORT
#endif

#endif
