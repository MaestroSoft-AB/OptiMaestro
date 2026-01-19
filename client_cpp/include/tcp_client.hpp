#ifndef _TCP_CLIENT_HPP_
#define _TCP_CLIENT_HPP_

#define _POSIX_C_SOURCE 200809L

#define HOST "stockholm2.onvo.se"
#define PORT "81"

#include <string>
#include <iostream>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

class TCP_Client {
    private:
        int fd;
        std::string transmit_data;
        int transmit_length;
        std::string receive_data;
        bool ready;

        int set_nonblocking(int _Fd); //Not implemented

    public:
        TCP_Client();
        ~TCP_Client();

        bool is_ready();
        int recieve();
        std::string getRecieveData();
        int transmit();
        void setTransmitData(std::string _Data);
        int getFileDescriptor();

};

#endif