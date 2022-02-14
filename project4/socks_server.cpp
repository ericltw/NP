#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <sys/wait.h>
#include <string>
#include <iostream>
#include <fstream>

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_service io_service;

struct request_info
{
    tcp::endpoint endpoint;
};

class session
    : public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket)
        // TODO: Check the reason of out_socket_.
        : in_socket_(std::move(socket)), out_socket_(io_service)
    {
        // Init the data.
        memset(request_data_, '\0', max_length);
        memset(in_data_, '\0', max_length);
        memset(out_data_, '\0', max_length);
    }

    void start()
    {
        read_socks4_request();
    }

private:
    // Get endpoint of destination.
    void resolve()
    {
        string port, ip;

        // Get port number from request data.
        port = to_string(ntohs(*((uint16_t *)&request_data_[2])));
        // Get IP address from request data.
        if (request_data_[4] == 0 && request_data_[5] == 0 && request_data_[6] == 0)
        {
            ip = string(&request_data_[9]);
        }
        else
        {
            ip = to_string((uint8_t)request_data_[4]) + "." + to_string((uint8_t)request_data_[5]) + "." + to_string((uint8_t)request_data_[6]) + "." + to_string((uint8_t)request_data_[7]);
        }

        tcp::resolver resolver(io_service);
        tcp::resolver::query query(ip, port);
        tcp::resolver::iterator iterator = resolver.resolve(query);
        request_info_.endpoint = iterator->endpoint();
    }

    // The firewall to check if the request is valid.
    bool isRequestValid()
    {
        int i = 0;
        string temp_string, ip_segment[4], permit_string, command, permit_ip;
        stringstream address;

        ifstream config_file_stream("./socks.conf");

        if (config_file_stream.is_open() == false)
        {
            return false;
        }

        address << request_info_.endpoint.address().to_string();
        // Store the IP address segment to ip_segment.
        while (getline(address, temp_string, '.'))
        {
            ip_segment[i++] = temp_string;
        }

        // According the form of socks.conf (permit c 140.113.*.*).
        while (config_file_stream >> permit_string >> command >> permit_ip)
        {
            bool is_permit = true;

            // If the command is connect and the request is bind, continue.
            if (command == "c" && request_data_[1] != 1)
            {
                continue;
                // If the command is bind and the request is connect, continue.
            }
            else if (command == "b" && request_data_[1] != 2)
            {
                continue;
            }
            else
            {
                address.clear();
                address << permit_ip;

                // Check the four number of ip address.
                for (i = 0; i < 4; ++i)
                {
                    getline(address, temp_string, '.');
                    if (temp_string == "*")
                    {
                        continue;
                    }
                    else if (ip_segment[i] != temp_string)
                    {
                        is_permit = false;
                    }
                }
            }
            if (is_permit)
            {
                config_file_stream.close();
                return true;
            }
        }
        config_file_stream.close();
        return false;
    }

    void print_request_info(string reply)
    {
        cout << "<S_IP>: " << in_socket_.remote_endpoint().address().to_string() << endl;
        cout << "<S_PORT>: " << to_string(in_socket_.remote_endpoint().port()) << endl;
        cout << "<D_IP>: " << request_info_.endpoint.address().to_string() << endl;
        cout << "<D_PORT>: " << to_string(request_info_.endpoint.port()) << endl;
        if (request_data_[1] == 1)
        {
            cout << "<Command>: CONNECT" << endl;
        }
        else
        {
            cout << "<Command>: BIND" << endl;
        }
        cout << "<Reply>: " << reply << endl;
    }

    void write_socks4_reply(int result_code, tcp::endpoint endpoint)
    {
        unsigned short port = endpoint.port();
        unsigned int address = endpoint.address().to_v4().to_ulong();

        char reply[8];
        reply[0] = 0;
        reply[1] = result_code;
        reply[2] = port >> 8 & 0xFF;
        reply[3] = port & 0xFF;
        reply[4] = address >> 24 & 0xFF;
        reply[5] = address >> 16 & 0xFF;
        reply[6] = address >> 8 & 0xFF;
        reply[7] = address & 0xFF;

        boost::asio::write(in_socket_, boost::asio::buffer(reply, 8));
    }

    void close_sockets()
    {
        in_socket_.close();
        out_socket_.close();
    }

    void connect()
    {
        out_socket_.connect(request_info_.endpoint);
    }

    // TODO: Check if the following code works.
    void bind()
    {
        auto self(shared_from_this());
        tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), 0));
        write_socks4_reply(90, acceptor.local_endpoint());
        acceptor.accept(out_socket_);
        write_socks4_reply(90, acceptor.local_endpoint());
    }

    void out_socket_write(size_t length)
    {
        auto self(shared_from_this());
        boost::asio::async_write(
            out_socket_,
            boost::asio::buffer(in_data_, length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    memset(in_data_, 0, max_length);
                    in_socket_read();
                }
                else
                {
                    close_sockets();
                }
            });
    }

    void in_socket_read()
    {
        auto self(shared_from_this());
        in_socket_.async_read_some(
            boost::asio::buffer(in_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    out_socket_write(length);
                }
                else
                {
                    close_sockets();
                }
            });
    }

    void in_socket_write(size_t length)
    {
        auto self(shared_from_this());
        boost::asio::async_write(
            in_socket_,
            boost::asio::buffer(out_data_, length),
            [this, self](boost::system::error_code ec, std::size_t /*length*/)
            {
                if (!ec)
                {
                    memset(out_data_, 0, max_length);
                    out_socket_read();
                }
                else
                {
                    close_sockets();
                }
            });
    }

    void out_socket_read()
    {
        auto self(shared_from_this());
        out_socket_.async_read_some(
            boost::asio::buffer(out_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    in_socket_write(length);
                }
                else
                {
                    close_sockets();
                }
            });
    }

    void read_socks4_request()
    {
        auto self(shared_from_this());
        in_socket_.async_read_some(
            boost::asio::buffer(request_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    // According to the project PPT, the version number should be 4.
                    if (request_data_[0] != 4)
                    {
                        return;
                    }

                    // Get endpoint of destination.
                    resolve();

                    if (!isRequestValid())
                    {
                        print_request_info("Reject");
                        write_socks4_reply(91, in_socket_.local_endpoint());
                        close_sockets();
                        return;
                    }

                    // Handle Connect operation.
                    if (request_data_[1] == 1)
                    {
                        connect();
                        if (out_socket_.native_handle() < 0)
                        {
                            print_request_info("Reject");
                            write_socks4_reply(91, in_socket_.local_endpoint());
                            close_sockets();
                            return;
                        }
                        else
                        {
                            print_request_info("Accept");
                            write_socks4_reply(90, in_socket_.local_endpoint());
                        }
                    }
                    // TODO: Handle bind operation.
                    else if (request_data_[1] == 2)
                    {
                        bind();
                        if (out_socket_.native_handle() < 0)
                        {
                            print_request_info("Reject");
                            write_socks4_reply(91, in_socket_.local_endpoint());
                            close_sockets();
                            return;
                        }
                        else
                        {
                            print_request_info("Accept");
                        }
                    }

                    in_socket_read();
                    out_socket_read();
                }
            });
    }

    // The socket between client and proxy.
    tcp::socket in_socket_;
    // The socket between proxy and server.
    tcp::socket out_socket_;
    enum
    {
        max_length = 1024
    };
    request_info request_info_;
    // The initial request data from the client.
    char request_data_[max_length];
    // The data from the client after establishing the connection.
    char in_data_[max_length];
    // The data from the server after establishing the connection.
    char out_data_[max_length];
};

void child_handler(int signo)
{
    int status;

    while (waitpid(-1, &status, WNOHANG) > 0)
    {
    }
}

class server
{
public:
    server(short port)
        : acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
    {
        do_accept();
    }

private:
    // Inform io_service that the process is about to fork, or has just forked.
    // This allows the io_service, and the services it contains, to perform any
    // necessary housekeeping to ensure correct operation following a fork.
    void notify_fork(boost::asio::io_service::fork_event fork_event)
    {
        io_service.notify_fork(fork_event);
    }

    void do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {

                    notify_fork(boost::asio::io_service::fork_prepare);
                    // To avoid zombie process.
                    signal(SIGCHLD, child_handler);
                    pid_t pid = fork();

                    // If fork error, fork again.
                    while (pid < 0)
                    {
                        int status;

                        waitpid(-1, &status, 0);
                        pid = fork();
                    }

                    // Parent process.
                    if (pid > 0)
                    {
                        notify_fork(boost::asio::io_service::fork_parent);
                        socket.close();
                        // Continue to accept request.
                        do_accept();
                        // Child process.
                    }
                    else
                    {
                        notify_fork(boost::asio::io_service::fork_child);
                        acceptor_.close();
                        std::make_shared<session>(std::move(socket))->start();
                    }
                }
            });
    }

    tcp::acceptor acceptor_;
};

// Initial code is from echo server of project3.
int main(int argc, char *argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        server s(std::atoi(argv[1]));

        io_service.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}