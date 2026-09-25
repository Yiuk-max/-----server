#include "message_handler.h"

#include "client_session.h"

void File_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string& file_data) {
    const std::string type = message.type();
    if (type == "upload_file") {
        // 文件二进制内容由传输层切帧后通过 file_data 参数传入。
        session.upload_file(message.meta(), file_data);
        return;
    } else if (type == "download_file") {
        session.download_file(message.file_name());
        return;
    }
}
