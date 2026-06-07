#include "message_handler.h"
void Chat_handler::handle_message(const json& message,client_session& session){
    std::string type = message["cmd"];
    
}