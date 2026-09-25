#pragma once

#include <memory>
#include <string>

#include "community_repository.h"

class community_repo : public I_community_repo {
public:
    // 社区
    int  create_community(int owner_uid, const std::string& name,
                          const std::string& description) override;
    bool delete_community(int community_id, int requester_uid) override;
    bool modify_community(int community_id, int requester_uid,
                          const std::string& name,
                          const std::string& description,
                          const std::string& avatar) override;

    // 查询
    bool is_community_member(int community_id, int user_uid) override;
    std::vector<int> get_user_communities(int user_uid) override;
    bool get_user_communities_info(int user_uid, std::vector<community_info>& out) override;
    bool get_community_info(int community_id, community_info& out) override;
    bool get_community_channels(int community_id, std::vector<channel_info>& out) override;
    std::vector<int> get_user_channels(int user_uid) override;
    bool get_community_members(int community_id, std::vector<community_member_info>& out) override;

    // 频道
    int  create_channel(int community_id, int requester_uid,
                        const std::string& name, const std::string& category) override;
    bool delete_channel(int community_id, int channel_id, int requester_uid) override;
    bool modify_channel(int community_id, int channel_id, int requester_uid,
                        const std::string& name, const std::string& category) override;
    bool get_channel_community(int channel_id, int& out_community_id) override;

    // 成员
    bool add_member(int community_id, int requester_uid, int target_uid) override;
    bool remove_member(int community_id, int requester_uid, int target_uid) override;
    bool leave(int community_id, int user_uid) override;
    bool send_join_request(int community_id, int requester_uid,
                           const std::string& message) override;
    bool handle_join_request(int community_id, int requester_uid,
                             int target_uid, bool accept) override;
    bool modify_member_role(int community_id, int requester_uid,
                            int target_uid, bool promote) override;
    bool show_requests(int community_id, int requester_uid,
                       std::vector<std::tuple<int, std::string>>& out) override;

    std::vector<int> get_channel_members(int channel_id) override;
};
