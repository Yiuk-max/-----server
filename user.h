#pragma once
#include <iostream>
#include <string>
class user{
    private:
        int client_fd_;
        std::string name;
        std::string password="123456";
    public:
        user() = default;
        user(int client_fd, const std::string& name, const std::string& password) : client_fd_(client_fd), name(name), password(password) {}
        int getClientFd();
        std::string getName();
        bool passwd_check(); 
};