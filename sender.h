#pragma once
#include "total.h"
class sender
{
private:
    std::vector<uint8_t> buffer;
public:
    sender();
    virtual bool send_msg() =0;
    virtual ~sender() = 0;
};
//Text_sender
//File_sender

