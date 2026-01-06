#pragma once

#include "common.hpp"

#include <memory>
#include <string>

enum class ReadResult { Success, Error, Timeout };

class IConn {
public:
  virtual ~IConn() = default;
  virtual bool Write(const ChatMessage &msg) = 0;
  virtual ReadResult Read(ChatMessage &msg) = 0;
};

std::unique_ptr<IConn> create_connection(int id, bool is_host);
std::string connection_type_code();
