//Message_handler,Chat_handler,Group_handler
#pragma once
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "client_session.h"
using json = nlohmann::json;
class client_session;
class Message_handler{
    virtual void handle_message(const json& message,client_session& session) = 0;
    virtual ~Message_handler() = default;
};
class Chat_handler : public Message_handler{
    void handle_message(const json& message,client_session& session) override;
};
class Group_handler : public Message_handler{
    void handle_message(const json& message,client_session& session) override;
};