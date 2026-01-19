#include "../include/tcp_client.hpp"

TCP_Client::TCP_Client() 
    : fd(-1), ready(false), transmit_length(0)
{
    struct addrinfo connhints = {0}; 
    struct addrinfo* res = nullptr;
    
    connhints.ai_family = AF_UNSPEC;
    connhints.ai_socktype = SOCK_STREAM;
    connhints.ai_protocol = IPPROTO_TCP; // TCP

    int code = getaddrinfo(HOST, PORT, &connhints, &res);
    if(code != 0) {
        std::cout << "Error " << code << " while getting address info: " << gai_strerror(code) << std::endl;
        return;
    }

    int fd = -1;
    for (struct addrinfo *addr_info = res; addr_info; addr_info = addr_info->ai_next)
    {
        fd = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
        if (fd < 0) {
          continue;
        }
     
        //Todo: Fix nonblocking bug OR remove nonblocking capability

        if (int conn = connect(fd, addr_info->ai_addr, addr_info->ai_addrlen) == 0) {
          break;
        } else if (conn == -1 && errno == EINPROGRESS) {
          std::cout << "in progress" << std::endl;
          break; //Nonblocking connection in progess (SOCKcess)
        }
        
        close(fd);
        fd = -1;
    }
    
    freeaddrinfo(res);
    
    if (fd >= 3) {
    this->fd = fd;
    //std::cout << "Socket successfully connected at fd " << fd << "!" << std::endl;
    ready = true;
    }
}

TCP_Client::~TCP_Client() {
    if (this->fd >= 3) {
        close(this->fd);
        this->fd = -1;
    }
}

int TCP_Client::set_nonblocking(int _Fd){
      if (int flags = fcntl(_Fd, F_GETFL, 0) == -1) 
      {
        return -1;
      } else 
      if (fcntl(_Fd, F_SETFL, flags | O_NONBLOCK) == -1)
      {
        return -1;
      } 
    return 0;
}

int TCP_Client::recieve() {
    if (this->fd < 0) {
        return -1;
    }

    bool done = false;
    char recv_buf[1024*1024];
    char* buf_ptr = recv_buf;
    int buf_len = 0;
    int bytes_recieved = 0;
    
    while (!done) {
    
        bytes_recieved = recv(this->fd, buf_ptr, sizeof(recv_buf), MSG_DONTWAIT);

        if (bytes_recieved < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (bytes_recieved == 0) {
            done = true;
        }

        if (bytes_recieved > 0) {
            buf_ptr += bytes_recieved;
            buf_len += bytes_recieved;
        }
    }

    recv_buf[buf_len] = '\0';

    std::string s(recv_buf, buf_len);
    this->receive_data = s;
    
    return 0; 
}

std::string TCP_Client::getRecieveData() {return this->receive_data;}

int TCP_Client::transmit() {
    if (this->fd < 0 || this->transmit_length <= 0 || this->transmit_data.empty()) {
        return -1;
    }

    int transmit_bytes = this->transmit_length;
    int bytes_transmitted = 0;

    std::cout << "Coutat från tcp " << std::endl << this->transmit_data << std::endl;
    while (transmit_bytes > 0)
    {
        bytes_transmitted = send(this->fd, this->transmit_data.c_str(), this->transmit_data.size(), MSG_NOSIGNAL);
        std::cout << "Bytes_transmitted: " << bytes_transmitted << std::endl;

        if (bytes_transmitted < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (bytes_transmitted > 0) {
            transmit_bytes -= bytes_transmitted;
        }
    }
    
    return 0;
}

void TCP_Client::setTransmitData(std::string _Data) {
    this->transmit_data = _Data;
    this->transmit_length = _Data.length();
}

int TCP_Client::getFileDescriptor() {return this->fd;}

bool TCP_Client::is_ready() {return ready;}

