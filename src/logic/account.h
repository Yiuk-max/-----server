#pragma once
#include <iostream>
#include <string>

namespace account_info
{
    struct account_settings
    {
        bool recv_msg_notification = true;
        bool recv_group_msg_notification = true;
        bool recv_file_notification = true;
        std::string theme = "white";
        std::string language = "Chinese";
    };
    struct account_base_info
    {
        std::string name;
        std::string password = "123456";
        int UID;
        bool online_status = false;
        std::string email;
        std::string phone_number;
    };
}

class account
{
private:
    account_info::account_settings settings;
    account_info::account_base_info base_info;

public:
    void load_account_info();
    account(){
        load_account_info();
    };
    std::string getName();
    bool passwd_check(std::string password);
    int getUID();
};
