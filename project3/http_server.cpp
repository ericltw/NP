#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <sys/wait.h>

using boost::asio::ip::tcp;
using namespace std;

class session
    : public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket)
        : socket_(std::move(socket))
    {
    }

    void start()
    {
        do_read();
    }

private:
    static void child_handler(int signo)
    {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0)
        {
        }
    }

    void get_exec_file_path(char exec_file_path[])
    {
        char request_uri[max_length];

        // Get request uri from environment variable.
        strcpy(request_uri, getenv("REQUEST_URI"));

        strcpy(exec_file_path, ".");
        strcat(exec_file_path, strtok(request_uri, "?"));
    }

    void do_write(std::size_t length)
    {
        auto self(shared_from_this());
        boost::asio::async_write(
            socket_, boost::asio::buffer(data_, length),
            [this, self](boost::system::error_code ec, std::size_t /*length*/)
            {
                if (!ec)
                {
                    // Avoid zombie process.
                    signal(SIGCHLD, child_handler);

                    pid_t pid;
                    pid = fork();

                    // Child process
                    if (pid == 0)
                    {
                        char exec_file_path[max_length];
                        int socket = socket_.native_handle();

                        dup2(socket, STDIN_FILENO);
                        dup2(socket, STDOUT_FILENO);
                        dup2(socket, STDERR_FILENO);
                        socket_.close();

                        get_exec_file_path(exec_file_path);

                        if (execlp(exec_file_path, exec_file_path, NULL) < 0)
                        {
                            exit(0);
                        }
                    }
                    else
                    {
                        socket_.close();
                    }

                    do_read();
                }
            });
    }

    void set_env_query_string(char query_string[])
    {
        if (query_string == NULL)
        {
            setenv("QUERY_STRING", "", 1);
        }
        else
        {
            setenv("QUERY_STRING", query_string + 1, 1);
        }
    }

    void split_data_and_set_env_variable()
    {
        char request_method[max_length];
        char request_uri[max_length];
        char server_protocol[max_length];
        // Just for splittig the data_, it won't be used later.
        char hostString[max_length];
        char http_host[max_length];

        // Split the first 5 words.
        sscanf(data_, "%s %s %s %s %s", request_method, request_uri, server_protocol, hostString, http_host);

        char *query_string = strchr(request_uri, '?');

        // Set all environment variables.
        setenv("REQUEST_METHOD", request_method, 1);
        setenv("REQUEST_URI", request_uri, 1);
        set_env_query_string(query_string);
        setenv("SERVER_PROTOCOL", server_protocol, 1);
        setenv("HTTP_HOST", http_host, 1);
        setenv("SERVER_ADDR", socket_.local_endpoint().address().to_string().c_str(), 1);
        setenv("SERVER_PORT", to_string(socket_.local_endpoint().port()).c_str(), 1);
        setenv("REMOTE_ADDR", socket_.remote_endpoint().address().to_string().c_str(), 1);
        setenv("REMOTE_PORT", to_string(socket_.remote_endpoint().port()).c_str(), 1);
    }

    void set_data()
    {
        strcpy(data_, "HTTP/1.1 200 OK\n");
    }

    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(
            boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if (!ec)
                {
                    split_data_and_set_env_variable();

                    set_data();

                    do_write(strlen(data_));
                }
            });
    }

    tcp::socket socket_;

    enum
    {
        max_length = 1024
    };
    char data_[max_length];
};

class server
{
public:
    server(boost::asio::io_context &io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    {
        do_accept();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    std::make_shared<session>(std::move(socket))->start();
                }

                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char *argv[])
{
    try
    {
        if (argc != 2)
        {
            std::cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        server s(io_context, std::atoi(argv[1]));

        io_context.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}