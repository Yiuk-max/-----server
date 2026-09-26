#include "message_handler.h"

#include "client_session.h"
#include "file_service.h"

void File_handler::handle_message(const chat_proto::Envelope& message, client_session& session, std::string& file_data) {
    const std::string type = message.type();

    auto svc = session.file_service();
    if (!svc) {
        session.package_message("File service unavailable.\n", "system");
        return;
    }

    const int uid = session.logged_in_uid();
    auto send = [&session](const chat_proto::Envelope& env, const std::string& fd) {
        session.send_file_packet(env, fd);
    };

    if (type == "file_upload_init") {
        svc->upload_init(uid, message, send);
    } else if (type == "file_chunk") {
        svc->upload_chunk(uid, message, file_data, send);
    } else if (type == "file_upload_done") {
        svc->upload_done(uid, message, send);
    } else if (type == "file_upload_resume") {
        svc->upload_resume(uid, message, send);
    } else if (type == "file_download_init") {
        svc->download_init(uid, message, send);
    } else if (type == "file_download_chunk") {
        svc->download_chunk(uid, message, send);
    } else if (type == "file_transfer_pause") {
        svc->pause(uid, message, send);
    } else if (type == "file_transfer_resume") {
        svc->resume(uid, message, send);
    } else if (type == "file_transfer_cancel") {
        svc->cancel(uid, message, send);
    } else if (type == "file_transfer_status") {
        svc->status(uid, message, send);
    } else if (type == "file_list_request") {
        svc->list(uid, message, send);
    } else if (type == "file_delete") {
        svc->remove_file(uid, message, send);
    }
}
