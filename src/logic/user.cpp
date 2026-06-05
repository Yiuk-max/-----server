#include "user.h"
int user::getClientFd(){
    return client_fd_;
}
std::string user::getName(){
    return name;
}
bool user::passwd_check(){
    // Implement your password checking logic here
    // For example, you can compare the password with a stored value
    std::string stored_password = "123456"; // Replace with actual password retrieval logic
    return password == stored_password;
}