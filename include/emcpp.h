#pragma once

#include <vector>
#include <string>


#ifdef _WIN32
  #define EMCPP_EXPORT __declspec(dllexport)
#else
  #define EMCPP_EXPORT
#endif

EMCPP_EXPORT void emcpp();
EMCPP_EXPORT void emcpp_print_vector(const std::vector<std::string> &strings);
