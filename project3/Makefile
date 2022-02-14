CC = g++
CFLAGS = -g -Wall -lpthread
TARGET = ./

all:
	$(CC) http_server.cpp -o $(TARGET)http_server $(CFLAGS)
	$(CC) console.cgi.cpp -o $(TARGET)console.cgi $(CFLAGS)

part1:
	$(CC) http_server.cpp -o $(TARGET)http_server $(CFLAGS)
	$(CC) console.cgi.cpp -o $(TARGET)console.cgi $(CFLAGS)

clean:
	rm $(TARGET)http_server
	rm $(TARGET)console.cgi
