// $Id: cxid.cpp,v 1.10 2021-11-16 16:11:40-08 - - $
// SERVER

#include <iostream>
#include <string>
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




void reply_put(accepted_socket& client_sock, cxi_header& header) {
   // ASSUMING FILE FITS IN ONE BUFFER

   uint32_t bytes = ntohl(header.nbytes);

   char payload[BUFFER_SIZE];
   recv_packet(client_sock, &payload, bytes);

   ofstream ofs = write_file_from_buffer(header.filename, payload, 
         bytes);

   // send back
   if (!ofs) {
      header.command = cxi_command::NAK;
      header.nbytes = htonl(errno);
   } else {
      header.command = cxi_command::ACK;
      header.nbytes = htonl(0);
   }
   memset(header.filename, 0, FILENAME_SIZE);
   send_packet(client_sock, &header, sizeof header);
}

void reply_get(accepted_socket& client_sock, cxi_header& header) {
   // ASSUMING FILE FITS IN ONE BUFFER

   char payload[BUFFER_SIZE];
   ifstream ifs = read_file_into_buffer(header.filename, payload);

   // send ACK header
   if (!ifs) {
      header.command = cxi_command::NAK;
      header.nbytes = htonl(errno);
   } else {
      ifs.seekg(0, ifs.end);
      int len = ifs.tellg();
      ifs.seekg(0, ifs.beg);

      header.command = cxi_command::FILEOUT;
      header.nbytes = htonl(len);
   }
   memset(header.filename, 0, FILENAME_SIZE);
   send_packet(client_sock, &header, sizeof header);

   // send payload
   if (!ifs) {
      // don't send a payload
   } else {
      ifs.seekg(0, ifs.end);
      int len = ifs.tellg(); 
      ifs.seekg(0, ifs.beg);

      send_packet(client_sock, payload, len);
   }
}

void reply_rm(accepted_socket& client_sock, cxi_header& header) {
   if (unlink(header.filename) != 0) {  // fail
      header.command = cxi_command::NAK;
      header.nbytes = htonl(errno);
   } else {  // success
      header.command = cxi_command::ACK;
      header.nbytes = htonl(0);
   }
   memset(header.filename, 0, FILENAME_SIZE);
   send_packet(client_sock, &header, sizeof header);
}

void reply_ls (accepted_socket& client_sock, cxi_header& header) {
   static const char ls_cmd[] = "ls -l 2>&1";
   FILE* ls_pipe = popen (ls_cmd, "r");
   if (ls_pipe == nullptr) { 
      outlog << ls_cmd << ": " << strerror (errno) << endl;
      header.command = cxi_command::NAK;
      header.nbytes = htonl (errno);
      send_packet (client_sock, &header, sizeof header);
      return;
   }

   string ls_output;
   char buffer[BUFFER_SIZE];
   for (;;) {
      char* rc = fgets (buffer, sizeof buffer, ls_pipe);
      if (rc == nullptr) break;
      ls_output.append (buffer);
   }
   pclose (ls_pipe);
   
   header.command = cxi_command::LSOUT;
   header.nbytes = htonl (ls_output.size());
   memset (header.filename, 0, FILENAME_SIZE);
   DEBUGF ('h', "sending header " << header);
   send_packet (client_sock, &header, sizeof header);
   send_packet (client_sock, ls_output.c_str(), ls_output.size());
   DEBUGF ('h', "sent " << ls_output.size() << " bytes");
}


void run_server (accepted_socket& client_sock) {
   outlog.execname (outlog.execname() + "*");
   outlog << "connected to " << to_string (client_sock) << endl;
   try {
      for (;;) {
         cxi_header header; 
         recv_packet (client_sock, &header, sizeof header);
         DEBUGF ('h', "received header " << header);
         switch (header.command) {
            case cxi_command::PUT:
               outlog << "recieved PUT" << endl;
               reply_put (client_sock, header);
               break;
            case cxi_command::RM:
               outlog << "recieved RM" << endl;
               reply_rm(client_sock, header);
               break;
            case cxi_command::GET:
               outlog << "recieved GET" << endl;
               reply_get(client_sock, header);
               break;
            case cxi_command::LS:
               outlog << "recieved LS" << endl;
               reply_ls(client_sock, header);
               break;
            default:
               outlog << "invalid client header:" << header << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   throw cxi_exit();
}

void fork_cxiserver (server_socket& server, accepted_socket& accept) {
   pid_t pid = fork();
   if (pid == 0) { // child
      server.close();
      run_server (accept);
      throw cxi_exit();
   }else {
      accept.close();
      if (pid < 0) {
         outlog << "fork failed: " << strerror (errno) << endl;
      }else {
         outlog << "forked cxiserver pid " << pid << endl;
      }
   }
}


void reap_zombies() {
   for (;;) {
      int status;
      pid_t child = waitpid (-1, &status, WNOHANG);
      if (child <= 0) break;
      if (status != 0) {
         outlog << "child " << child
                << " exit " << (status >> 8 & 0xFF)
                << " signal " << (status & 0x7F)
                << " core " << (status >> 7 & 1) << endl;
      }
   }
}

void signal_handler (int signal) {
   DEBUGF ('s', "signal_handler: caught " << strsignal (signal));
   reap_zombies();
}

void signal_action (int signal, void (*handler) (int)) {
   struct sigaction action;
   action.sa_handler = handler;
   sigfillset (&action.sa_mask);
   action.sa_flags = 0;
   int rc = sigaction (signal, &action, nullptr);
   if (rc < 0) outlog << "sigaction " << strsignal (signal)
                      << " failed: " << strerror (errno) << endl;
}



void usage() {
   cerr << "Usage: " << outlog.execname() << " port" << endl;
   throw cxi_exit();
}

in_port_t scan_options (int argc, char** argv) {
   for (;;) {
      int opt = getopt (argc, argv, "@:");
      if (opt == EOF) break;
      switch (opt) {
         case '@': debugflags::setflags (optarg);
                   break;
      }
   }
   if (argc - optind != 1) usage();
   return get_cxi_server_port (argv[optind]);
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   signal_action (SIGCHLD, signal_handler);
   try {
      in_port_t port = scan_options (argc, argv);
      server_socket listener (port);
      for (;;) {
         outlog << to_string (hostinfo()) << " accepting port "
             << to_string (port) << endl;
         accepted_socket client_sock;
         for (;;) {
            try {
               listener.accept (client_sock);
               break;
            }catch (socket_sys_error& error) {
               switch (error.sys_errno) {
                  case EINTR:
                     outlog << "listener.accept caught "
                         << strerror (EINTR) << endl;
                     break;
                  default:
                     throw;
               }
            }
         }
         outlog << "accepted " << to_string (client_sock) << endl;
         try {
            fork_cxiserver (listener, client_sock);
            reap_zombies();
         }catch (socket_error& error) {
            outlog << error.what() << endl;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   return 0;
}

