cmake_minimum_required(VERSION 3.10)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)
add_executable(chat-server 
    src/main.cpp 
    src/server.cpp         
) 