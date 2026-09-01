#pragma once
#include <string>

// ============================================================
// 群聊数据模型（纯数据 holder，无 DB / SQL 依赖）
// 定位：群聊对象只是"群"这一实体的数据载体，由 db/repo 层
//       create_group / load_group 创建并返回（含 owner、名称、group_UID）。
//       群成员的增删/权限/广播等业务全部下沉到 repo 层与 NoticeService，
//       本类不再持有成员列表、不再管理活跃生命周期。
// 分层约定：
//   - 本类【不含任何 SQL / 不持有仓库】。数据库读写由 repo 层完成。
//   - 结构体字段与 sql/create_table.sql 中 `Group` 表【严格对齐】：
//         group_UID / name / owner_UID / create_time
// ============================================================
class group{
    private:
        int manager_UID_;           // owner_UID：创建者/群主
        std::string group_name_;    // name：群名称
        int UID;                    // group_UID：群唯一标识（系统自增分配）
        std::string str_UID;        // 补零字符串形式，便于展示
        int member_count_;            // 群成员数量（仅展示用，非实时统计）

    public:
        group() : manager_UID_(-1), UID(-1), member_count_(0) {}
        // 由 repo 层创建：从数据库读取/写入的群数据填充本对象
        group(int manager, std::string name, int UID_)
            : manager_UID_(manager), group_name_(name), UID(UID_), member_count_(0) {
            // UID 由数据库 AUTO_INCREMENT 分配，这里只做展示用补零，不再依赖内存分配器
            str_UID = std::to_string(UID_);
        }

        int getUID() const { return UID; }
        int get_manager_UID() const { return manager_UID_; }
        std::string get_group_name() const { return group_name_; }
        int get_member_count() const { return member_count_; }
        void set_member_count(int n) { member_count_ = n; }   // 由 repo 在 load_group/create_group 时填入
        void add_member_count() { member_count_ ++; }
        std::string get_group_info();   // 展示用：群名(人数):群UID
};
