#pragma once

#include <memory>
#include <string>

#include "message_repository.h"

class message_repo : public I_message_repo {
public:
    int store_message(int sender_UID, int receiver_UID, const std::string& message, bool is_group) override;

    bool delete_message(int message_id) override;

    bool delete_expired_messages() override;

    bool get_message(int message_id, message& out) override;

    std::vector<message> get_offline_messages(int receiver_UID, const std::string& since_time) override;

    std::vector<message> get_chat_history(int user1_UID, int user2_UID, int limit) override;

};