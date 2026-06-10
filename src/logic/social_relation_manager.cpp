
void social_relation_manager::add_friend(int friend_UID){
    friend_relations.emplace_back(friend_UID);
}
void social_relation_manager::remove_friend(int friend_UID){
    friend_relations.erase(std::remove(friend_relations.begin(), friend_relations.end(), friend_UID), friend_relations.end());
}