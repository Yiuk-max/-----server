#pragma once

#include <memory>
#include <string>

#include "group_repository.h"

class group_repo : public I_group_repo {
public:
    std::shared_ptr<group> create_group(int owner_uid, const std::string& group_name) override;

    bool delete_group(int group_uid, int requester_uid) override;

    bool modify_group_name(int group_uid, int requester_uid, const std::string& new_group_name) override;

    std::shared_ptr<group> load_group(int group_uid) override;

    std::vector<int> get_group_members(int group_uid) override;
    std::string show_group_members(int group_uid) override;

    std::vector<int> get_user_groups(int user_uid) override;
    std::string show_user_groups(int user_uid) override;

    bool member_add_group(int group_uid, int requester_uid, int new_member_uid) override;

    bool send_join_group(int group_uid, int requester_uid) override;

    bool handle_join_request(int group_uid, int requester_uid, int target_uid, bool accept) override;

    bool modify_member_role(int group_uid, int requester_uid, int target_uid, bool promote) override;

};