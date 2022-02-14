#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>

using boost::asio::ip::tcp;
using namespace std;

const int NUMBER_OF_SERVERS = 5;
boost::asio::io_service io_service;
string socks_host;
string socks_port;

struct server_info
{
    // Record if query string of corresponding server contans all the
    // config fields (host, port, input file).
    bool has_data = false;

    // Used for adding HTML content to the corresponding table.
    string element_id;

    // Get from the query string.
    string host;
    string port;
    string input_file_path;

    // For readig the data from file.
    ifstream input_file_stream;
};

void print_content_type()
{
    cout << "Content-type: text/html" << endl
         << endl;
}

void parse_query_string(server_info server_infos[])
{
    string query_string;
    vector<string> parameters;

    query_string = string(getenv("QUERY_STRING"));
    boost::split(parameters, query_string, boost::is_any_of("&"));

    // Set the host, port, input_file of server info.
    for (int i = 0; i < (int)parameters.size(); i++)
    {
        // If the last element of the parameter is =, it means that
        // the value is NULL.
        if (parameters[i].back() == '=')
        {
            continue;
        }

        int indexOfEqual = parameters[i].find("=");

        // If is not Socks4 related parameters.
        if (parameters[i][0] != 's')
        {
            int serverIndex = stoi(parameters[i].substr(1, 1));
            switch (parameters[i][0])
            {
            case 'h':
                server_infos[serverIndex].host = parameters[i].substr(indexOfEqual + 1);
                break;
            case 'p':
                server_infos[serverIndex].port = parameters[i].substr(indexOfEqual + 1);
                break;
            case 'f':
                server_infos[serverIndex].input_file_path = "./test_case/" + parameters[i].substr(indexOfEqual + 1);
                break;
            }
        }
        else
        {
            switch (parameters[i][1])
            {
            case 'h':
                socks_host = parameters[i].substr(indexOfEqual + 1);
                break;
            case 'p':
                socks_port = parameters[i].substr(indexOfEqual + 1);
                break;
            }
        }
    }

    // Set the has_data, element_id, and input_file_stream of server info.
    for (int i = 0; i < NUMBER_OF_SERVERS; i++)
    {
        if (server_infos[i].host != "" && server_infos[i].port != "" && server_infos[i].input_file_path != "")
        {
            server_infos[i].has_data = true;
            server_infos[i].element_id = 's' + to_string(i + 1);
            server_infos[i].input_file_stream.open(server_infos[i].input_file_path);
        }
    }
}

void print_thead(server_info server_infos[])
{
    cout << "<thead>" << endl;
    cout << "<tr>" << endl;
    for (int i = 0; i < NUMBER_OF_SERVERS; i++)
    {
        if (server_infos[i].has_data)
        {
            cout << "<th scope=\"col\">" + server_infos[i].host + ":" + server_infos[i].port + "</th>" << endl;
        }
    }
    cout << "</tr>" << endl;
    cout << "</thead>" << endl;
}

void print_tbody(server_info server_infos[])
{
    cout << "<tbody>" << endl;
    cout << "<tr>" << endl;
    for (int i = 0; i < NUMBER_OF_SERVERS; i++)
    {
        if (server_infos[i].has_data)
        {
            cout << "<td><pre id=" + server_infos[i].element_id + " class=\"mb-0\"></pre></td>" << endl;
        }
    }

    cout << "</tbody>" << endl;
    cout << "</tr>" << endl;
}

void print_html(server_info server_infos[])
{
    cout << "<!DOCTYPE html>" << endl;
    cout << "<html lang=\"en\">" << endl;
    cout << "<head>" << endl;
    cout << "<meta charset=\"UTF-8\" />" << endl;
    cout << "<title>NP Project 3 Sample Console</title>" << endl;
    cout << "<link" << endl;
    cout << "rel=\"stylesheet\"" << endl;
    cout << "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"" << endl;
    cout << "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"" << endl;
    cout << "crossorigin=\"anonymous\"" << endl;
    cout << "/>" << endl;
    cout << "<link" << endl;
    cout << "rel=\"stylesheet\"" << endl;
    cout << "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"" << endl;
    cout << "/>" << endl;
    cout << "<link" << endl;
    cout << "rel=\"icon\"" << endl;
    cout << "type=\"image/png\"" << endl;
    cout << "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"" << endl;
    cout << "/>" << endl;
    cout << "<style>" << endl;
    cout << "* {" << endl;
    cout << "font-family: 'Source Code Pro', monospace;" << endl;
    cout << "font-size: 1rem !important;" << endl;
    cout << "}" << endl;
    cout << "body {" << endl;
    cout << "background-color: #212529;" << endl;
    cout << "}" << endl;
    cout << "pre {" << endl;
    cout << "color: #cccccc;" << endl;
    cout << "}" << endl;
    cout << "b {" << endl;
    cout << "color: #01b468;" << endl;
    cout << "}" << endl;
    cout << "</style>" << endl;
    cout << "</head>" << endl;

    cout << "<body>" << endl;
    cout << "<table class=\"table table-dark table-bordered\">" << endl;
    print_thead(server_infos);
    print_tbody(server_infos);
    cout << "</table>" << endl;
    cout << "</body>" << endl;

    cout << "</html>" << endl;
}

class client
{
public:
    client()
        : socket_(io_service),
          resolver_(io_service)
    {
        memset(data_, '\0', max_length);
    }

    void start(server_info &server_info)
    {
        do_resolve(server_info);
    }

private:
    void replace_symbols(string &data)
    {
        boost::replace_all(data, " ", "&nbsp;");
        boost::replace_all(data, "\"", "&quot;");
        boost::replace_all(data, "\'", "&apos;");
        boost::replace_all(data, "\r", "");
        boost::replace_all(data, "\n", "&NewLine;");
        boost::replace_all(data, "<", "&lt;");
        boost::replace_all(data, ">", "&gt;");
    }

    // Print the response from server (project2) to user.
    void output_response(server_info &server_info)
    {
        string response = string(data_);

        replace_symbols(response);
        cout << "<script>document.getElementById('" << server_info.element_id << "').innerHTML += '" << response << "';</script>" << endl;
    }

    // send the command to server and print the command to user.
    void output_command(server_info &server_info)
    {
        string command;

        getline(server_info.input_file_stream, command);

        if (command == "exit")
        {
            server_info.has_data = false;
            server_info.input_file_stream.close();
        }

        // Process the command output.
        boost::replace_all(command, "\r", "");
        command.append("\n");

        // Send the command to server (project2).
        do_write(server_info, command.c_str(), command.size());

        //
        replace_symbols(command);
        cout << "<script>document.getElementById('" << server_info.element_id << "').innerHTML += '<b>" << command << "</b>';</script>" << endl;
    }

    void do_write(server_info &server_info, const char *buffer, size_t length)
    {
        boost::asio::async_write(
            socket_, boost::asio::buffer(buffer, length),
            [&, this, buffer](boost::system::error_code error_code, size_t write_length)
            {
                if (!error_code)
                {
                }
            });
    }

    void do_read(server_info &server_info)
    {
        socket_.async_read_some(
            boost::asio::buffer(data_, max_length),
            [this, &server_info](boost::system::error_code error_code, size_t length)
            {
                if (!error_code)
                {
                    output_response(server_info);
                    // The % is from the server (project2)
                    if (strstr(data_, "% "))
                    {
                        output_command(server_info);
                    }
                    memset(data_, 0, max_length);
                    do_read(server_info);
                }
            });
    }

    void do_write_request(server_info &server_info)
    {
        char request[max_length] = {0};
        request[0] = 4;
        request[1] = 1;
        request[2] = (atoi(server_info.port.c_str()) >> 8) & 0xFF;
        request[3] = atoi(server_info.port.c_str()) & 0xFF;
        request[4] = 0;
        request[5] = 0;
        request[6] = 0;
        request[7] = 1;
        request[8] = 0;
        memcpy(request + 9, server_info.host.c_str(), server_info.host.size());
        request[9 + server_info.host.size()] = 0;
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(request, 10 + server_info.host.size()),
            [&, this](boost::system::error_code ec, size_t write_length)
            {
                if (!ec)
                {
                    do_read_reply(server_info);
                }
                else
                {
                    cerr << "Write => " << ec << endl;
                }
            });
    }

    void do_read_reply(server_info &server_info)
    {
        socket_.async_read_some(
            boost::asio::buffer(data_, 8),
            [this, &server_info](boost::system::error_code ec, size_t length)
            {
                if (!ec)
                {
                    if (data_[1] == 90)
                    {
                        do_read(server_info);
                    }
                }
            });
    }

    void do_connect(server_info &server_info, tcp::resolver::iterator &iterator)
    {
        tcp::endpoint endpoint = iterator->endpoint();
        // Connect to the server (project2).
        socket_.async_connect(
            endpoint, [this, &server_info](boost::system::error_code error_code)
            {
                if (!error_code)
                {
                    do_write_request(server_info);
                }
                else
                {
                    cerr << "Connect => " << error_code << endl
                         << endl;
                }
            });
    }

    void do_resolve(server_info &server_info)
    {
        tcp::resolver::query query(socks_host, socks_port);
        // For DNS
        resolver_.async_resolve(
            query, [this, &server_info](boost::system::error_code error_code, tcp::resolver::iterator iterator)
            {
                if (!error_code)
                {
                    do_connect(server_info, iterator);
                }
                else
                {
                    cerr << "Resolve => " << error_code << endl;
                }
            });
    }

    tcp::socket socket_;
    tcp::resolver resolver_;
    enum
    {
        max_length = 1024
    };
    char data_[max_length];
};

int main(int argc, char *argv[])
{
    server_info server_infos[NUMBER_OF_SERVERS];
    client clients[NUMBER_OF_SERVERS];

    print_content_type();
    parse_query_string(server_infos);
    print_html(server_infos);

    for (int i = 0; i < NUMBER_OF_SERVERS; i++)
    {
        if (server_infos[i].has_data)
        {
            clients[i].start(server_infos[i]);
        }
    }

    io_service.run();

    return 0;
}