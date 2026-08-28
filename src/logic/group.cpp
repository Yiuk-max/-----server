#include "group.h"

// 展示用信息：群名:群UID（补零形式）
std::string group::get_group_info(){
    return group_name_ + ":" + str_UID;
}
