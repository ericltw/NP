#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define MAX_SINGLE_INPUT_LENGTH 15000
#define MAX_COMMAND_LENGTH 256
#define MAX_NUMBERED_PIPE_NUMBER 10000
#define MAX_USER_PIPE_NUMBER 10000
#define MAX_USER_NAME 20
#define MAX_USER_PORT 20
#define MAX_NUM_OF_USERS 30
// Define by myself.
#define MAX_CHILD_PROCESS_NUMBER 1000
#define ENV_NUMBER 100
#define ENV_LENGTH 500

// Store the segmentation of single-line input.
struct InputSegmentationTable
{
    char *segmentation[MAX_SINGLE_INPUT_LENGTH];
    int length = 0;
    int currentIndex = 0;
};

enum Operation
{
    // For built-in command and /bin executables.
    general,

    fileRedirection,

};

enum PipeType
{
    // For general, and fireDirection operation.
    none,

    ordinary,

    numbered,

    numberedWithStdError,

    // For using user pipe as input.
    userIn,

    // For using user pipe as output.
    userOut,
};

// Store the current command information.
struct Command
{
    int argc;
    char *argv[MAX_COMMAND_LENGTH];
    Operation operation = general;
    PipeType pipeType = none;
    int inputPipe;
    int outputPipe;
    int errorPipe;
    int countdown = 0;
    // For build-in, bin/ command (operation), executablePath store the
    // path of executable.
    char executablePath[MAX_COMMAND_LENGTH];
    // For file redirection command (operation), filePath store the input
    // and output file path.
    char *filePath[2];
    // For user pipe communication.
    int toUser = -1;
    int fromUser = -1;
};

struct PipeConfig
{
    // For input and output endpoints of a pipe.
    int endpoints[2];

    // Record the number of how many following lines the pipe data will
    // be the input.
    int countdown;

    int socket;

    bool isCreatedForOrdinaryPipe;
    bool hasBeenUsedAsInputPipe = false;
};

struct PipeConfigTable
{
    PipeConfig configs[MAX_NUMBERED_PIPE_NUMBER];
    int length = 0;
};

struct EnvTable
{
    char key[ENV_NUMBER][ENV_LENGTH];
    char value[ENV_NUMBER][ENV_LENGTH];
    int length;
};

struct User
{
    char name[MAX_USER_NAME];
    char port[MAX_USER_PORT];
    int socket;
    bool isLogin = false;
    // int address;
    EnvTable env;
};

struct UserTable
{
    User users[MAX_NUM_OF_USERS + 1];
    int length;
};

// Used to store pipe for user communication.
struct UserPipeConfig
{
    // The user index of the user who seding message.
    int from;

    // The user index of the user who receiving message.
    int to;

    // For input and output endpoints of a pipe.
    int endpoints[2];

    bool hasBeenUsedAsInputPipe = false;
};

struct UserPipeConfigTable
{
    UserPipeConfig configs[MAX_USER_PIPE_NUMBER];
    int length = 0;
};

void initEnvPath()
{
    setenv("PATH", "bin:.", 1);
}

// Create the TCP connection.
int passiveTCP(uint16_t port)
{
    int sockfd, optval = 1;
    sockaddr_in serverAddress;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("Error: socket failed");
        exit(0);
    }
    bzero((char *)&serverAddress, sizeof(serverAddress));

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        perror("Error: setsockopt failed");
        exit(0);
    }

    if (bind(sockfd, (sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("Error: bind failed");
        exit(0);
    }

    if (listen(sockfd, 5) == -1)
    {
        perror("Error: listen failed");
        exit(0);
    }

    return sockfd;
}

void printMessage(int socket, char message[])
{
    write(socket, message, strlen(message));
}

void printWelcomeMessage(int socket)
{
    char message[] = "****************************************\n** Welcome to the information server. **\n****************************************\n";
    printMessage(socket, message);
}

void broadcastMessage(char message[], UserTable &userTable)
{
    for (int i = 1; i <= MAX_NUM_OF_USERS; ++i)
    {
        if (userTable.users[i].isLogin)
        {
            printMessage(userTable.users[i].socket, message);
        }
    }
}

void broadcastLoginMessage(User user, UserTable &userTable)
{
    char message[MAX_COMMAND_LENGTH];
    sprintf(message, "*** User '%s' entered from %s. ***\n", user.name, user.port);
    broadcastMessage(message, userTable);
}

void initUserInfoAndBroadcast(int newClientSocket, sockaddr_in clientAddress, UserTable &userTable)
{
    // Iterate to find a seat for new client in user table.
    for (int i = 1; i <= MAX_NUM_OF_USERS; i++)
    {
        if (userTable.users[i].isLogin == false)
        {
            // Init the name.
            strcpy(userTable.users[i].name, "(no name)");
            // Init the port.
            char port[10];
            sprintf(port, "%d", ntohs(clientAddress.sin_port));
            strcpy(userTable.users[i].port, inet_ntoa(clientAddress.sin_addr));
            strcat(userTable.users[i].port, ":");
            strcat(userTable.users[i].port, port);
            // Init socket.
            userTable.users[i].socket = newClientSocket;
            // Init isLogin.
            userTable.users[i].isLogin = true;
            // Init the env table for the user.
            userTable.users[i].env.length = 0;
            strcpy(userTable.users[i].env.key[userTable.users[i].env.length], "PATH");
            strcpy(userTable.users[i].env.value[userTable.users[i].env.length], "bin:.");
            userTable.users[i].env.length++;

            broadcastLoginMessage(userTable.users[i], userTable);
            break;
        }
    }
}

void printPrefixSymbol(int socket)
{
    char message[MAX_COMMAND_LENGTH];
    strcpy(message, "% ");
    printMessage(socket, message);
}

// We will use \0 (NULL) to determine the end of a command.
void initInput(char *input)
{
    memset(input, '\0', sizeof(*input));
}

int getUserIndex(int sockfd, UserTable &userTable)
{
    for (int i = 1; i <= MAX_NUM_OF_USERS; ++i)
    {
        if (sockfd == userTable.users[i].socket && userTable.users[i].isLogin)
        {
            return i;
        }
    }
    return -1;
}

void broadcastLogoutMessage(User user, UserTable &userTable)
{
    char message[1000];
    sprintf(message, "*** User '%s' left. ***\n", user.name);
    broadcastMessage(message, userTable);
}

void logoutAndBroadcastMessage(fd_set &afds, int sockfd, UserTable &userTable)
{
    FD_CLR(sockfd, &afds);
    int index = getUserIndex(sockfd, userTable);
    if (index != -1)
    {
        userTable.users[index].isLogin = false;
        close(userTable.users[index].socket);
        userTable.users[index].socket = -1;
        broadcastLogoutMessage(userTable.users[index], userTable);
    }
}

// Split the single-line input, and store the result to the input
// segmentation table.
void splitInput(char input[], InputSegmentationTable &inputSegmentationTable)
{
    char delim[] = " ";
    char *pch = strtok(input, delim);

    while (pch != NULL)
    {
        int length = inputSegmentationTable.length;

        inputSegmentationTable.segmentation[length] = pch;
        pch = strtok(NULL, delim);

        inputSegmentationTable.length++;
    }
}

void removeUserInfo() {}

// After an iteration, we will execute the next single-line input,
// so we countdown all pipes.
void countdownAllPipes(PipeConfigTable &pipeConfigTable, int socket)
{
    int length = pipeConfigTable.length;

    for (int i = 0; i < length; i++)
    {
        if (pipeConfigTable.configs[i].socket == socket)
        {
            pipeConfigTable.configs[i].countdown--;
        }
    }
}

void initCurrentCommand(Command &currentCommand)
{
    currentCommand.argc = 0;
    memset(&currentCommand.argv[0], '\0', sizeof(currentCommand.argv[0]));
    memset(&currentCommand.argv[1], '\0', sizeof(currentCommand.argv[1]));
    memset(&currentCommand.argv[2], '\0', sizeof(currentCommand.argv[2]));
    memset(&currentCommand.argv[3], '\0', sizeof(currentCommand.argv[3]));
    memset(&currentCommand.argv[4], '\0', sizeof(currentCommand.argv[4]));
    currentCommand.operation = general;
    currentCommand.pipeType = none;
    currentCommand.countdown = 0;
    strcpy(currentCommand.executablePath, "");
    currentCommand.fromUser = -1;
    currentCommand.toUser = -1;
}

// Including argc, argv, operation, pipeType, countDown, filePath.
void setCommandInfos(InputSegmentationTable &inputSegmentationTable, Command &currentCommand)
{
    // Init argc.
    currentCommand.argc = 0;

    // Set the first argv.
    int currentArgc = currentCommand.argc;
    int currenrIndex = inputSegmentationTable.currentIndex;
    currentCommand.argv[currentArgc] = inputSegmentationTable.segmentation[currenrIndex];

    // If is the chat related command (tell, yell, name), we need to ignore < and >.
    bool isChatCommand = (!strcmp(currentCommand.argv[0], "tell") || !strcmp(currentCommand.argv[0], "yell") || !strcmp(currentCommand.argv[0], "name"));

    while (inputSegmentationTable.currentIndex < inputSegmentationTable.length)
    {
        int currentIndex = inputSegmentationTable.currentIndex;
        char *nextSegmentation = inputSegmentationTable.segmentation[currentIndex];

        if (nextSegmentation[0] == '>' && !isChatCommand)
        {
            // Output user pipe.
            if (nextSegmentation[1] != '\0')
            {
                currentCommand.toUser = atoi(&nextSegmentation[1]);
                currentCommand.pipeType = userOut;
                inputSegmentationTable.currentIndex++;
            }
            else
            // If this is a file redirection command, get the input and output filename.
            {
                currentCommand.operation = fileRedirection;
                currentCommand.filePath[0] = currentCommand.argv[1];
                currentCommand.filePath[1] = inputSegmentationTable.segmentation[currentIndex + 1];

                currentCommand.argc++;
                inputSegmentationTable.currentIndex = inputSegmentationTable.currentIndex + 2;
                break;
            }
        }
        // Input User pipe.
        else if (nextSegmentation[0] == '<' && !isChatCommand)
        {
            currentCommand.fromUser = atoi(&nextSegmentation[1]);
            currentCommand.pipeType = userIn;
            inputSegmentationTable.currentIndex++;
        }
        // If this is a pipe command, determine is ordinary pipe or numbered pipe.
        else if (nextSegmentation[0] == '|' && !isChatCommand)
        {
            // Ordinary pipe.
            if (nextSegmentation[1] == '\0')
            {
                currentCommand.pipeType = ordinary;
                inputSegmentationTable.currentIndex++;
                break;
            }
            else
            {
                currentCommand.pipeType = numbered;
                currentCommand.countdown = atoi(&nextSegmentation[1]);
                inputSegmentationTable.currentIndex++;
                break;
            }
        }
        else if (nextSegmentation[0] == '!' && !isChatCommand)
        {
            currentCommand.pipeType = numberedWithStdError;
            currentCommand.countdown = atoi(&nextSegmentation[1]);
            inputSegmentationTable.currentIndex++;
            break;
        }
        else
        {
            currentCommand.argv[currentCommand.argc] = inputSegmentationTable.segmentation[currentIndex];
            currentCommand.argc++;
            inputSegmentationTable.currentIndex++;
        }
    }
    currentCommand.argv[currentCommand.argc] = NULL;
}

int getUserPipeIndex(UserPipeConfigTable userPipeConfigTable, int fromUser, int toUser)
{
    for (int i = 0; i < userPipeConfigTable.length; ++i)
    {
        if (userPipeConfigTable.configs[i].from == fromUser && userPipeConfigTable.configs[i].to == toUser && !userPipeConfigTable.configs[i].hasBeenUsedAsInputPipe)
        {
            return i;
        }
    }
    return -1;
}

void getExecutablePath(Command &currentCommand, UserTable &userTable, int currentSocket)
{
    char *argv = currentCommand.argv[0];

    if (!strcmp(argv, "printenv"))
    {
        strcpy(currentCommand.executablePath, "printenv");
        return;
    }
    else if (!strcmp(argv, "setenv"))
    {
        strcpy(currentCommand.executablePath, "setenv");
        return;
    }
    else if (!strcmp(argv, "who"))
    {
        strcpy(currentCommand.executablePath, "who");
        return;
    }
    else if (!strcmp(argv, "tell"))
    {
        strcpy(currentCommand.executablePath, "tell");
        return;
    }
    else if (!strcmp(argv, "yell"))
    {
        strcpy(currentCommand.executablePath, "yell");
        return;
    }
    else if (!strcmp(argv, "name"))
    {
        strcpy(currentCommand.executablePath, "name");
        return;
    }
    else
    {

        int index = getUserIndex(currentSocket, userTable);
        char *envPath = userTable.users[index].env.value[0];
        // To operate the environment path locally.
        char localEnvPath[MAX_COMMAND_LENGTH];
        strcpy(localEnvPath, envPath);
        char delim[] = ":";
        char *pch = strtok(localEnvPath, delim);

        while (pch != NULL)
        {
            strcpy(currentCommand.executablePath, pch);
            FILE *fp = fopen(strcat(strcat(currentCommand.executablePath, "/"), currentCommand.argv[0]), "r");
            if (fp)
            {
                fclose(fp);
                return;
            }
            pch = strtok(NULL, delim);
        }
    }
    strcpy(currentCommand.executablePath, "");
}

int getInputPipe(Command currentCommand, PipeConfigTable &pipeConfigTable, UserPipeConfigTable &userPipeConfigTable, int currentSocket, UserTable &userTable, char rawInput[])
{
    // The user want to get data from user pipe (as input).
    if (currentCommand.fromUser != -1)
    {
        char message[2000];
        int fromUser = currentCommand.fromUser, toUser = getUserIndex(currentSocket, userTable);

        // The user is not exit.
        if (userTable.users[fromUser].isLogin == false)
        {
            sprintf(message, "*** Error: user #%d does not exist yet. ***\n", fromUser);
            write(currentSocket, message, strlen(message));
            int fd = open("/dev/null", O_RDONLY);
            return fd;
        }
        else
        {
            int userPipeIndex = getUserPipeIndex(userPipeConfigTable, fromUser, toUser);
            // The use pipe is not exit.
            if (userPipeIndex == -1)
            {
                sprintf(message, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", fromUser, toUser);
                write(currentSocket, message, strlen(message));
                int fd = open("/dev/null", O_RDONLY);
                return fd;
            }
            else
            {
                sprintf(message, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", userTable.users[toUser].name, toUser, userTable.users[fromUser].name, fromUser, rawInput);
                broadcastMessage(message, userTable);
                close(userPipeConfigTable.configs[userPipeIndex].endpoints[1]);
                // Set the variable to make the closing pipe decision later.
                userPipeConfigTable.configs[userPipeIndex].hasBeenUsedAsInputPipe = true;

                return userPipeConfigTable.configs[userPipeIndex].endpoints[0];
            }
        }
    }

    // For other situation.
    int length = pipeConfigTable.length;
    for (int i = 0; i < length; ++i)
    {
        int countdown = pipeConfigTable.configs[i].countdown;
        bool hasBeenUsedAsInputPipe = pipeConfigTable.configs[i].hasBeenUsedAsInputPipe;
        int socket = pipeConfigTable.configs[i].socket;

        if (countdown == 0 && !hasBeenUsedAsInputPipe && socket == currentSocket)
        {
            close(pipeConfigTable.configs[i].endpoints[1]);
            pipeConfigTable.configs[i].endpoints[1] = -1;
            // Set the variable to make the closing pipe decision later.
            pipeConfigTable.configs[i].hasBeenUsedAsInputPipe = true;

            return pipeConfigTable.configs[i].endpoints[0];
        }
    }
    return STDIN_FILENO;
}

void createPipe(Command currentCommand, PipeConfigTable &pipeConfigTable, int currentSocket)
{
    int length = pipeConfigTable.length;
    int coutdown = currentCommand.countdown;
    int isCreatedForOrdinaryPipe = currentCommand.pipeType == ordinary;

    pipe(pipeConfigTable.configs[length].endpoints);
    pipeConfigTable.configs[length].countdown = coutdown;
    pipeConfigTable.configs[length].isCreatedForOrdinaryPipe = isCreatedForOrdinaryPipe;
    pipeConfigTable.configs[length].socket = currentSocket;

    pipeConfigTable.length++;
}

void createUserPipe(UserPipeConfigTable &userPipeConfigTable, int fromUser, int toUser)
{
    int length = userPipeConfigTable.length;

    userPipeConfigTable.configs[length].from = fromUser;
    userPipeConfigTable.configs[length].to = toUser;
    pipe(userPipeConfigTable.configs[length].endpoints);
    userPipeConfigTable.length++;
}

int getOutputPipe(Command &currentCommand, PipeConfigTable &pipeConfigTable, UserPipeConfigTable &userPipeConfigTable, int currentSocket, UserTable &userTable, char rawInput[])
{
    if (currentCommand.pipeType == numbered || currentCommand.pipeType == numberedWithStdError)
    {
        int length = pipeConfigTable.length;
        int countdown = currentCommand.countdown;

        // Check if there is a pipe which will be used has been established.
        for (int i = 0; i < length; i++)
        {
            bool isCreatedForNumberedPipe = pipeConfigTable.configs[i].isCreatedForOrdinaryPipe == false;
            int socket = pipeConfigTable.configs[i].socket;

            if (pipeConfigTable.configs[i].countdown == countdown && isCreatedForNumberedPipe && socket == currentSocket)
            {
                return pipeConfigTable.configs[i].endpoints[1];
            }
        }
        createPipe(currentCommand, pipeConfigTable, currentSocket);
        return pipeConfigTable.configs[length].endpoints[1];
    }
    else if (currentCommand.pipeType == ordinary)
    {
        int length = pipeConfigTable.length;

        createPipe(currentCommand, pipeConfigTable, currentSocket);
        return pipeConfigTable.configs[length].endpoints[1];
    }
    else if (currentCommand.operation == fileRedirection)
    {
        int fd = open(currentCommand.filePath[1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        return fd;
        // return open(currentCommand.filePath[1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    }
    // The user want to output data to user pipe.
    else if (currentCommand.toUser != -1)
    {
        char message[2000];
        int fromUser = getUserIndex(currentSocket, userTable), toUser = currentCommand.toUser;

        // The user is not exit.
        if (toUser > MAX_NUM_OF_USERS || userTable.users[toUser].isLogin == false)
        {
            sprintf(message, "*** Error: user #%d does not exist yet. ***\n", toUser);
            write(currentSocket, message, strlen(message));
            int fd = open("/dev/null", O_WRONLY);
            return fd;
        }
        else
        {
            int userPipeIndex = getUserPipeIndex(userPipeConfigTable, fromUser, toUser);

            // The user pipe is not exit.
            if (userPipeIndex != -1)
            {
                sprintf(message, "*** Error: the pipe #%d->#%d already exists. ***\n", fromUser, toUser);
                write(currentSocket, message, strlen(message));
                int fd = open("/dev/null", O_WRONLY);
                return fd;
            }
            else
            // The user pipe is exit.
            {
                int length = userPipeConfigTable.length;

                sprintf(message, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", userTable.users[fromUser].name, fromUser, rawInput, userTable.users[toUser].name, toUser);
                broadcastMessage(message, userTable);
                createUserPipe(userPipeConfigTable, fromUser, toUser);
                return userPipeConfigTable.configs[length].endpoints[1];
            }
        }
    }

    return currentSocket;
}

int getErrorPipe(Command &currentCommand, PipeConfigTable &pipeConfigTable, int currentSocket)
{
    if (currentCommand.pipeType == numberedWithStdError)
    {
        int length = pipeConfigTable.length;
        int countdown = currentCommand.countdown;

        for (int i = 0; i < length; ++i)
        {

            bool isCreatedForNumberedPipe = pipeConfigTable.configs[i].isCreatedForOrdinaryPipe == false;
            int socket = pipeConfigTable.configs[i].socket;

            if (pipeConfigTable.configs[i].countdown == countdown && isCreatedForNumberedPipe && socket == currentSocket)
            {
                return pipeConfigTable.configs[i].endpoints[1];
            }
        }

        createPipe(currentCommand, pipeConfigTable, currentSocket);
        return pipeConfigTable.configs[length].endpoints[1];
    }

    return currentSocket;
}

// Get the next execution command info, which is the command start from
// current input segmentation index, and end to |, |1 or !1.
void getCurrentCommandInfo(InputSegmentationTable &inputSegmentationTable, Command &currentCommand, PipeConfigTable &pipeConfigTable, UserPipeConfigTable &userPipeConfigTable, int currentSocket, UserTable userTable, char rawInput[])
{
    setCommandInfos(inputSegmentationTable, currentCommand);
    getExecutablePath(currentCommand, userTable, currentSocket);
    currentCommand.inputPipe = getInputPipe(currentCommand, pipeConfigTable, userPipeConfigTable, currentSocket, userTable, rawInput);
    currentCommand.outputPipe = getOutputPipe(currentCommand, pipeConfigTable, userPipeConfigTable, currentSocket, userTable, rawInput);
    currentCommand.errorPipe = getErrorPipe(currentCommand, pipeConfigTable, currentSocket);
}

// Use to avoid zombie process (if child process exit before parent
// process wait).
void childHandler(int signo)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
    {
    }
}

void setEnv(Command currentCommand, UserTable &userTable, int currentSocket)
{
    int index = getUserIndex(currentSocket, userTable);

    for (int i = 0; i < userTable.users[index].env.length; ++i)
    {
        if (!strcmp(currentCommand.argv[1], userTable.users[index].env.key[i]))
        {
            strcpy(userTable.users[index].env.value[i], currentCommand.argv[2]);
            return;
        }
    }

    strcpy(userTable.users[index].env.key[userTable.length], currentCommand.argv[1]);
    strcpy(userTable.users[index].env.value[userTable.length], currentCommand.argv[2]);
    userTable.users[index].env.length++;
}

void printEnv(Command currentCommand, UserTable &userTable, int currentSocket)
{
    int index = getUserIndex(currentSocket, userTable);

    for (int i = 0; i < userTable.users[index].env.length; ++i)
    {
        if (!strcmp(currentCommand.argv[1], userTable.users[index].env.key[i]))
        {
            write(currentSocket, userTable.users[index].env.value[i], strlen(userTable.users[index].env.value[i]));
            write(currentSocket, "\n", strlen("\n"));
            return;
        }
    }
}

void who(UserTable &userTable, int currentSocket)
{
    char message[1000];
    strcpy(message, "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
    write(currentSocket, message, strlen(message));

    for (int i = 1; i <= MAX_NUM_OF_USERS; i++)
    {
        if (userTable.users[i].isLogin)
        {
            if (userTable.users[i].socket == currentSocket)
            {
                sprintf(message, "%d\t%s\t%s\t%s\n", i, userTable.users[i].name, userTable.users[i].port, "<-me");
            }
            else
            {
                sprintf(message, "%d\t%s\t%s\n", i, userTable.users[i].name, userTable.users[i].port);
            }
            write(currentSocket, message, strlen(message));
        }
    }
}

// Used by tell(), and yell() to get the full message from command.argv.
void setRawMessage(Command &currentCommand, char rawMessage[], int startMessageIndex)
{
    char *buffer;
    char space[15] = " ";

    for (int i = startMessageIndex; i < currentCommand.argc; i++)
    {
        buffer = (char *)malloc(sizeof(currentCommand.argv[i] + 20));
        strcat(buffer, currentCommand.argv[i]);
        strcat(buffer, space);
        strcat(rawMessage, buffer);
    }
}

void tell(Command &currentCommand, UserTable &userTable, int currentSocket)
{
    char rawMessage[1000];
    char outputMessage[2000];
    int userId = atoi(currentCommand.argv[1]);
    int index = getUserIndex(currentSocket, userTable);

    if (userTable.users[userId].isLogin)
    {
        setRawMessage(currentCommand, rawMessage, 2);
        sprintf(outputMessage, "*** %s told you ***: %s\n", userTable.users[index].name, rawMessage);
        write(userTable.users[userId].socket, outputMessage, strlen(outputMessage));
    }
    else
    {
        sprintf(outputMessage, "*** Error: user #%d does not exist yet. ***\n", userId);
        write(currentSocket, outputMessage, strlen(outputMessage));
    }
}

void yell(Command &currentCommand, UserTable &userTable, int currentSocket)
{
    char rawMessage[1000];
    char outputMessage[2000];
    int index = getUserIndex(currentSocket, userTable);

    if (index != -1)
    {
        setRawMessage(currentCommand, rawMessage, 1);
        sprintf(outputMessage, "*** %s yelled ***: %s\n", userTable.users[index].name, rawMessage);
        broadcastMessage(outputMessage, userTable);
    }
}

void name(UserTable &userTable, int currentSocket, const char newName[])
{
    char message[1000];
    int index = getUserIndex(currentSocket, userTable);

    if (index != -1)
    {
        // Check if the user name is exit.
        for (int i = 1; i <= MAX_NUM_OF_USERS; ++i)
        {
            if (!strcmp(userTable.users[i].name, newName))
            {
                sprintf(message, "*** User '%s' already exists. ***\n", userTable.users[i].name);
                write(currentSocket, message, strlen(message));
                return;
            }
        }
        strcpy(userTable.users[index].name, newName);

        // broadcast the change name event.
        sprintf(message, "*** User from %s is named '%s'. ***\n", userTable.users[index].port, userTable.users[index].name);
        broadcastMessage(message, userTable);
    }
}

void execCurrentCommand(Command &currentCommand, UserTable &userTable, int currentSocket)
{
    char *executablePath = currentCommand.executablePath;

    if (!strcmp(executablePath, "setenv"))
    {
        setEnv(currentCommand, userTable, currentSocket);
    }
    else if (!strcmp(executablePath, "printenv"))
    {
        printEnv(currentCommand, userTable, currentSocket);
    }
    else if (!strcmp(executablePath, "who"))
    {
        who(userTable, currentSocket);
    }
    else if (!strcmp(executablePath, "tell"))
    {
        tell(currentCommand, userTable, currentSocket);
    }
    else if (!strcmp(executablePath, "yell"))
    {
        yell(currentCommand, userTable, currentSocket);
    }
    else if (!strcmp(executablePath, "name"))
    {
        name(userTable, currentSocket, currentCommand.argv[1]);
    }
    else
    {
        signal(SIGCHLD, childHandler);
        pid_t pid;

        pid = fork();

        // Child process.
        if (pid == 0)
        {
            if (currentCommand.inputPipe != STDIN_FILENO)
            {
                dup2(currentCommand.inputPipe, STDIN_FILENO);
            }
            if (currentCommand.outputPipe != STDOUT_FILENO)
            {
                dup2(currentCommand.outputPipe, STDOUT_FILENO);
            }
            if (currentCommand.errorPipe != STDERR_FILENO)
            {
                dup2(currentCommand.errorPipe, STDERR_FILENO);
            }

            if (currentCommand.inputPipe != STDIN_FILENO)
            {
                close(currentCommand.inputPipe);
            }
            if (currentCommand.outputPipe != STDOUT_FILENO)
            {
                close(currentCommand.outputPipe);
            }
            if (currentCommand.errorPipe != STDERR_FILENO)
            {
                close(currentCommand.errorPipe);
            }

            if (!strcmp(currentCommand.executablePath, ""))
            {
                cerr << "Unknown command: [" << currentCommand.argv[0] << "]." << endl;
            }
            else
            {
                execvp(currentCommand.executablePath, currentCommand.argv);
            }

            exit(0);
        }
        // Parent process.
        else
        {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

// TODO: Not the perfect method now, the perfect method is closing the pipe and
// reorganizing the user pipe table.
void closeUserPipe(UserPipeConfigTable &userPipeConfigTable, UserTable &userTable, int currentSocket)
{
    int index = getUserIndex(currentSocket, userTable);

    for (int i = 0; i < MAX_USER_PIPE_NUMBER; i++)
    {
        if (userPipeConfigTable.configs[i].to == index) {
            userPipeConfigTable.configs[i].hasBeenUsedAsInputPipe = true;
        }
    }
}

// TODO
void closePipe() {}

int main(int argc, char *argv[])
{
    int port, serverSocket, nfds;
    fd_set rfds, afds;
    UserTable userTable;
    PipeConfigTable pipeConfigTable;
    UserPipeConfigTable userPipeConfigTable;

    initEnvPath();

    port = atoi(argv[1]);
    serverSocket = passiveTCP(port);
    // Get the size of fd_set.
    nfds = getdtablesize();

    // Initial all the afds value.
    FD_ZERO(&afds);
    // Set first value of afds to serverSocket.
    FD_SET(serverSocket, &afds);

    while (1)
    {
        // Check ON/OFF. (L5-7.4)
        memcpy(&rfds, &afds, sizeof(rfds));
        if (select(nfds, &rfds, NULL, NULL, NULL) < 0)
        {
            perror("Error: select error");
            continue;
        }

        // Accept new client and do related things.
        // printPrefixSymbol()
        if (FD_ISSET(serverSocket, &rfds))
        {
            int newClientSocket, addressLength;
            sockaddr_in clientAddress;

            // Accept new client.
            addressLength = sizeof(clientAddress);
            newClientSocket = accept(serverSocket, (sockaddr *)&clientAddress, (socklen_t *)&addressLength);
            cout << "sockfd: " << newClientSocket << endl;
            if (newClientSocket < 0)
            {
                perror("Error: accept error");
                exit(1);
            }
            FD_SET(newClientSocket, &afds);

            printWelcomeMessage(newClientSocket);

            initUserInfoAndBroadcast(newClientSocket, clientAddress, userTable);

            printPrefixSymbol(newClientSocket);
        }

        // process each child socket
        for (int currentSocket = 0; currentSocket < nfds; ++currentSocket)
        {
            // Check if slave socket and on.
            if (serverSocket != currentSocket && FD_ISSET(currentSocket, &rfds))
            {

                InputSegmentationTable inputSegmentationTable;
                // For single-line input.
                char input[MAX_SINGLE_INPUT_LENGTH];
                // Will be used later.
                char rawInput[MAX_SINGLE_INPUT_LENGTH];
                char readBuffer[MAX_SINGLE_INPUT_LENGTH];

                initInput(input);
                do
                {
                    memset(&readBuffer, '\0', sizeof(readBuffer));
                    read(currentSocket, readBuffer, sizeof(readBuffer));
                    strcat(input, readBuffer);
                    cout << input << endl;
                } while (readBuffer[strlen(readBuffer) - 1] != '\n');

                strtok(input, "\r\n");
                strcpy(rawInput, input);

                // New line doesn't count as one line for numbered pipe.
                if (!strcmp(input, ""))
                {
                    continue;
                }
                else if (!strcmp(input, "exit"))
                {
                    closePipe();
                    closeUserPipe(userPipeConfigTable, userTable, currentSocket);
                    logoutAndBroadcastMessage(afds, currentSocket, userTable);
                    continue;
                }

                countdownAllPipes(pipeConfigTable, currentSocket);
                splitInput(input, inputSegmentationTable);

                int inputSegmentationTableLength = inputSegmentationTable.length;

                while (inputSegmentationTable.currentIndex < inputSegmentationTableLength)
                {
                    Command currentCommand = currentCommand;

                    initCurrentCommand(currentCommand);
                    getCurrentCommandInfo(inputSegmentationTable, currentCommand, pipeConfigTable, userPipeConfigTable, currentSocket, userTable, rawInput);

                    // cout << "argc:" << currentCommand.argc << "\n";
                    // cout << "argv:" << currentCommand.argv[0] << "\n";
                    // cout << "operation:" << currentCommand.operation << "\n";
                    // cout << "pipeType:" << currentCommand.pipeType << "\n";
                    // cout << "countdown:" << currentCommand.countdown << "\n";
                    // cout << "filePath:" << currentCommand.filePath << "\n";
                    // cout << "--------------------------------------------------"
                    //      << "\n";

                    execCurrentCommand(currentCommand, userTable, currentSocket);
                    // closePipe(pipeConfigTable);
                }
                printPrefixSymbol(currentSocket);
            }
        }
    }
}