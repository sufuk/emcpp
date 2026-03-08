cmake_minimum_required(VERSION 3.15)
project(emcpp CXX)




add_library(emcpp src/emcpp.cpp)
target_include_directories(emcpp PUBLIC include)



set_target_properties(emcpp PROPERTIES PUBLIC_HEADER "include/emcpp.h")
install(TARGETS emcpp)
