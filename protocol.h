// $Id: protocol.h,v 1.13 2021-12-20 12:58:11-08 - - $

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
using namespace std;

#include "socket.h"

enum class cxi_command : uint8_t {
   ERROR = 0, EXIT, GET, HELP, LS, PUT, RM, FILEOUT, LSOUT, ACK, NAK,
};

constexpr size_t FILENAME_SIZE = 59;
constexpr size_t HEADER_SIZE = 64;

struct cxi_header {
   uint32_t nbytes {};
   cxi_command command {cxi_command::ERROR};
   char filename[FILENAME_SIZE] {};
};

static_assert (sizeof (cxi_header) == HEADER_SIZE);

void send_packet (base_socket& socket,
                  const void* buffer, size_t bufsize);

void recv_packet (base_socket& socket, void* buffer, size_t bufsize);

ostream& operator<< (ostream& out, const cxi_header& header);

in_port_t get_cxi_server_port (const string& port_arg);

#endif

