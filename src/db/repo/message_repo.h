#pragma once

#include <memory>
#include <string>

#include "message_repository.h"

class message_repo : public I_message_repo {
public:
    int store_message(int sender_UID, int receiver_UID, const std::string& message,
                      const std::string& type, int reply_to_message_id = 0,
                      std::string* out_timestamp = nullptr) override;

    bool delete_message(int message_id) override;

    bool delete_expired_messages() override;

    bool get_message(int message_id, message& out) override;

    std::vector<message> get_offline_messages(int receiver_UID, const std::string& since_time) override;

    // 游标分页查聊天历史（message.id 作游标）
    bool get_history_page(int self_uid, int peer_uid, const std::string& type,
                          int before_id, int limit,
                          std::vector<message>& out, bool& has_more) override;

};