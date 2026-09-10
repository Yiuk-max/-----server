// session_manager 在线表语义测试（不依赖 MySQL）：
//   1. replace_online 返回旧会话并完成原子替换
//   2. remove_online_if_same 只按“UID + 对象身份”删除，旧连接迟到断线不会清掉新登录
//   3. 未登录 uid / 空指针调用安全
#include "client_session.h"
#include "session_manager.h"

#include <cassert>
#include <iostream>

// main.cpp 定义；测试链接时不包含 main.cpp，这里补上全局运行标志。
bool running = true;

int main() {
    auto& manager = session_manager::get_instance();

    auto first = std::make_shared<client_session>();
    auto second = std::make_shared<client_session>();

    assert(manager.replace_online(1001, first) == nullptr);
    assert(manager.find_session(1001) == first);

    // 同 UID 再次登录：返回旧会话并换成新会话
    auto replaced = manager.replace_online(1001, second);
    assert(replaced == first);
    assert(manager.find_session(1001) == second);

    // 旧连接迟到断线：身份不匹配，不能删除新会话
    manager.remove_online_if_same(1001, first.get());
    assert(manager.find_session(1001) == second);
    std::cout << "[ok] stale disconnect keeps new login" << std::endl;

    // 新会话自己断线：身份匹配才删除
    manager.remove_online_if_same(1001, second.get());
    assert(manager.find_session(1001) == nullptr);
    std::cout << "[ok] matching disconnect removes login" << std::endl;

    // 未登录 / 空指针路径安全
    manager.remove_online_if_same(-1, nullptr);
    manager.remove_online_if_same(9999, first.get());
    assert(manager.find_session(9999) == nullptr);
    std::cout << "[ok] invalid identity is a no-op" << std::endl;

    std::cout << "all session manager tests passed" << std::endl;
    return 0;
}
