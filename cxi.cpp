// $Id: cxi.cpp,v 1.6 2021-11-08 00:01:44-08 - - $
// CLIENT

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "logstream.h"
#include "protocol.h"
#include "socket.h"

#include <fstream>

# define BUFFER_SIZE 0x1000


logstream outlog (cout);
struct cxi_exit: public exception {};




ifstream read_file_into_buffer(const char* fn, char* buffer) {
   ifstream ifs{ fn, istream::binary };
   if (!ifs) {
      return ifs;
   }

   ifs.seekg(0, ifs.end);
   int len = ifs.tellg();
   ifs.seekg(0, ifs.beg);

   ifs.read(buffer, len);
   return ifs;
}

ofstream write_file_from_buffer(const char* fn, const char* buffer,
      const uint32_t bytes) {
   ofstream ofs{ fn, ostream::out | ostream::binary
         | ostream::trunc };
   if (ofs) {
      ofs.write(buffer, bytes);
   }
   return ofs;
}




unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"put",  cxi_command::PUT},
   {"get",  cxi_command::GET},
   {"rm",   cxi_command::RM},
   {"ls",   cxi_command::LS},
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cxi_help() {
   cout << help;
}


void cxi_put(client_socket& server, string fn) {
   // fn is ready to go into the header

   cxi_header hdr;
   strncpy(hdr.filename, fn.c_str(), FILENAME_SIZE);
   hdr.command = cxi_command::PUT;

   char payload[BUFFER_SIZE];
   ifstream ifs = read_file_into_buffer(hdr.filename, payload);

   if (!ifs) {
      throw socket_sys_error("Err: cxi_put: ifstream fail");
   }

   ifs.seekg(0, ifs.end);
   int len = ifs.tellg();
   ifs.seekg(0, ifs.beg);
   hdr.nbytes = htonl(len);

   // send packets
   send_packet(server, &hdr, sizeof hdr);
   send_packet(server, payload, len);

   // recieve packet
   recv_packet(server, &hdr, sizeof hdr);
   if (hdr.command == cxi_command::NAK) {
      cout << "PUT: FAILURE: NAK: err:" << 
            strerror(ntohl(hdr.nbytes)) << endl;
   } else if (hdr.command == cxi_command::ACK) {
      cout << "PUT: SUCCESS: ACK" << endl;
   } else {
      cout << "PUT: UNCERTAIN: recieved neither NAK nor ACK" << endl;
   }
}

void cxi_get(client_socket& server, string fn) {
   // fn is ready to go into the header

   // NOTE TO GRADER: COMMAND PARSING HAPPENS IN MAIN()
   //       THATS HOW WE GOT "string fn" AS AN ARG

   cxi_header hdr;
   strncpy(hdr.filename, fn.c_str(), FILENAME_SIZE);
   char fn_cstr_cpy[FILENAME_SIZE] {};
   strncpy(fn_cstr_cpy, fn.c_str(), FILENAME_SIZE);
   hdr.command = cxi_command::GET;
   hdr.nbytes = 0;
   send_packet(server, &hdr, sizeof hdr);

   recv_packet(server, &hdr, sizeof hdr);
   if (hdr.command == cxi_command::NAK) {
      cout << "GET: FAILURE: NAK: err:" << 
            strerror(ntohl(hdr.nbytes)) << endl;
   } else if (hdr.command == cxi_command::FILEOUT) {
      int bytes = ntohl(hdr.nbytes);
      char payload[BUFFER_SIZE];
      recv_packet(server, &payload, bytes);
      ofstream ofs = write_file_from_buffer(fn_cstr_cpy, payload, 
            bytes);
      if (!ofs) {
         throw socket_sys_error("Err: cxi_get: ofstream fail");
      }
      cout << "GET: SUCCESS: FILEOUT" << endl;
   } else {
      cout << "GET: UNCERTAIN: recieved neither NAK nor ACK" << endl;
   }
}

void cxi_rm(client_socket& server, string fn) {
   // fn is ready to go into the header

   cxi_header hdr;
   strncpy(hdr.filename, fn.c_str(), FILENAME_SIZE);
   hdr.command = cxi_command::RM;
   hdr.nbytes = 0;
   send_packet(server, &hdr, sizeof hdr);
   
   recv_packet(server, &hdr, sizeof hdr);
   if (hdr.command == cxi_command::NAK) {
      cout << "RM: FAILURE: NAK: err:" << 
            strerror(ntohl(hdr.nbytes)) << endl;
   } else if (hdr.command == cxi_command::ACK) {
      cout << "RM: SUCCESS: ACK" << endl;
   } else {
      cout << "RM: UNCERTAIN: recieved neither NAK nor ACK" << endl;
   }
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   DEBUGF ('h', "sending header " << header << endl);
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   DEBUGF ('h', "received header " << header << endl);
   if (header.command != cxi_command::LSOUT) {
      outlog << "sent LS, server did not return LSOUT" << endl;
      outlog << "server returned " << header << endl;
   }else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      DEBUGF ('h', "received " << host_nbytes << " bytes");
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
   }
}


void usage() {
   cerr << "Usage: " << outlog.execname() << " host port" << endl;
   throw cxi_exit();
}

pair<string,in_port_t> scan_options (int argc, char** argv) {
   for (;;) {
      int opt = getopt (argc, argv, "@:");
      if (opt == EOF) break;
      switch (opt) {
         case '@': debugflags::setflags (optarg);
                   break;
      }
   }
   if (argc - optind != 2) usage();
   string host = argv[optind];
   in_port_t port = get_cxi_server_port (argv[optind + 1]);
   return {host, port};
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << to_string (hostinfo()) << endl;
   try {
      auto host_port = scan_options (argc, argv);
      string host = host_port.first;
      in_port_t port = host_port.second;
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();

         // get com and fn (DESTROYS line) (com means command)
         string com = line;
         string fn = "";
         int spos = line.find(" ");
         if (spos != static_cast<int>(string::npos)) {  // 
               // found a whitespace
            com = line.substr(0, spos);
            line.erase(0, spos + 1);
            fn = line;
         }

         // check fn length
         if (fn.length() > 58) {  // too long
            cout << "Err: fn:" << fn << ", is >58 chars long" << endl;
            continue;
         }

         const auto& itor = command_map.find (com);
         cxi_command cmd = itor == command_map.end()
                         ? cxi_command::ERROR : itor->second;
         switch (cmd) {
            case cxi_command::EXIT:
               throw cxi_exit();
               break;
            case cxi_command::HELP:
               cxi_help();
               break;
            case cxi_command::PUT:
               cxi_put(server, fn);
               break;
            case cxi_command::GET:
               cxi_get(server, fn);
               break;
            case cxi_command::RM:
               cxi_rm(server, fn);
               break;
            case cxi_command::LS:
               cxi_ls (server);
               break;
            default:
               outlog << com << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   return 0;
}

