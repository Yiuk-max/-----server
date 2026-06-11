//Message_handler,Chat_handler,Group_handler
#pragma once
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class client_session;

class Message_handler {
public:
    virtual void handle_message(const json& message,client_session& session,std::string &file_data) = 0;
    virtual ~Message_handler() = default;
};
class Chat_handler : public Message_handler{
    void handle_message(const json& message,client_session& session,std::string &file_data) override;
};
class Group_handler : public Message_handler{
    void handle_message(const json& message,client_session& session,std::string &file_data) override;
};
class File_handler : public Message_handler{
    void handle_message(const json& message,client_session& session,std::string &file_data) override;
};
class Base_handler : public Message_handler{
    void handle_message(const json& message,client_session& session,std::string &file_data) override;
};