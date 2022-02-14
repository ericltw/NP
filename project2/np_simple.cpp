#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <map>

using namespace std;

#define MAX_SINGLE_INPUT_LENGTH 15000
#define MAX_COMMAND_LENGTH 256
#define MAX_NUMBERED_PIPE_NUMBER 10000
// Define by myself.
#define MAX_CHILD_PROCESS_NUMBER 1000

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
};

struct PipeConfig
{
    // For input and output endpoints of a pipe.
    int endpoints[2];

    // Record the number of how many following lines the pipe data will
    // be the input.
    int countdown;

    bool isCreatedForOrdinaryPipe;
    bool hasBeenUsedAsInputPipe = false;
};

struct PipeConfigTable
{
    PipeConfig configs[MAX_NUMBERED_PIPE_NUMBER];
    int length = 0;
};

// pidTable is not used.
// struct PidTable
// {
// };

// Create the TCP connection.
int passiveTCP(uint16_t port)
{
    int socketfd, optval = 1;
    sockaddr_in serverAddress;

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("Error: socket failed");
        exit(0);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        perror("Error: setsockopt failed");
        exit(0);
    }

    if (bind(socketfd, (sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("Error: bind failed");
        exit(0);
    }

    if (listen(socketfd, 5) == -1)
    {
        perror("Error: listen failed");
        exit(0);
    }

    return socketfd;
}

void initEnvPath()
{
    setenv("PATH", "bin:.", 1);
}

// We will use \0 (NULL) to determine the end of a command.
void initInput(char *input)
{
    memset(input, '\0', sizeof(*input));
}

void printPrefixSymbol(int clientSocket)
{
    write(clientSocket, "% ", strlen("% "));
}

// After an iteration, we will execute the next single-line input,
// so we countdown all pipes.
void countdownAllPipes(PipeConfigTable &pipeConfigTable)
{
    int length = pipeConfigTable.length;

    for (int i = 0; i < length; i++)
    {
        pipeConfigTable.configs[i].countdown--;
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

    currentCommand.argc++;
    inputSegmentationTable.currentIndex++;

    while (inputSegmentationTable.currentIndex < inputSegmentationTable.length)
    {
        int currentIndex = inputSegmentationTable.currentIndex;
        char *nextSegmentation = inputSegmentationTable.segmentation[currentIndex];

        // If this is a file redirection command, get the input and output filename.
        if (nextSegmentation[0] == '>')
        {
            currentCommand.operation = fileRedirection;
            currentCommand.filePath[0] = currentCommand.argv[1];
            currentCommand.filePath[1] = inputSegmentationTable.segmentation[currentIndex + 1];

            currentCommand.argc++;
            inputSegmentationTable.currentIndex = inputSegmentationTable.currentIndex + 2;
            break;
        }
        // If this is a pipe command, determine is ordinary pipe or numbered pipe.
        else if (nextSegmentation[0] == '|')
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
        else if (nextSegmentation[0] == '!')
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

int getInputPipe(PipeConfigTable &pipeConfigTable)
{
    int length = pipeConfigTable.length;
    for (int i = 0; i < length; ++i)
    {
        int countdown = pipeConfigTable.configs[i].countdown;
        bool hasBeenUsedAsInputPipe = pipeConfigTable.configs[i].hasBeenUsedAsInputPipe;

        if (countdown == 0 && !hasBeenUsedAsInputPipe)
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

void createPipe(Command currentCommand, PipeConfigTable &pipeConfigTable)
{
    int length = pipeConfigTable.length;
    int coutdown = currentCommand.countdown;
    int isCreatedForOrdinaryPipe = currentCommand.pipeType == ordinary;

    pipe(pipeConfigTable.configs[length].endpoints);
    pipeConfigTable.configs[length].countdown = coutdown;
    pipeConfigTable.configs[length].isCreatedForOrdinaryPipe = isCreatedForOrdinaryPipe;

    pipeConfigTable.length++;
}

int getOutputPipe(Command &currentCommand, PipeConfigTable &pipeConfigTable, int clientSocket)
{
    if (currentCommand.pipeType == numbered || currentCommand.pipeType == numberedWithStdError)
    {
        int length = pipeConfigTable.length;
        int countdown = currentCommand.countdown;

        // Check if there is a pipe which will be used has been established.
        for (int i = 0; i < length; i++)
        {
            bool isCreatedForNumberedPipe = pipeConfigTable.configs[i].isCreatedForOrdinaryPipe == false;

            if (pipeConfigTable.configs[i].countdown == countdown && isCreatedForNumberedPipe)
            {
                return pipeConfigTable.configs[i].endpoints[1];
            }
        }
        createPipe(currentCommand, pipeConfigTable);
        return pipeConfigTable.configs[length].endpoints[1];
    }
    else if (currentCommand.pipeType == ordinary)
    {
        int length = pipeConfigTable.length;

        createPipe(currentCommand, pipeConfigTable);
        return pipeConfigTable.configs[length].endpoints[1];
    }
    else if (currentCommand.operation == fileRedirection)
    {
        return open(currentCommand.filePath[1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    }

    return clientSocket;
}

int getErrorPipe(Command &currentCommand, PipeConfigTable &pipeConfigTable, int clientSocket)
{
    if (currentCommand.pipeType == numberedWithStdError)
    {
        int length = pipeConfigTable.length;
        int countdown = currentCommand.countdown;

        for (int i = 0; i < length; ++i)
        {

            bool isCreatedForNumberedPipe = pipeConfigTable.configs[i].isCreatedForOrdinaryPipe == false;

            if (pipeConfigTable.configs[i].countdown == countdown && isCreatedForNumberedPipe)
            {
                return pipeConfigTable.configs[i].endpoints[1];
            }
        }

        createPipe(currentCommand, pipeConfigTable);
        return pipeConfigTable.configs[length].endpoints[1];
    }

    return clientSocket;
}

void getExecutablePath(Command &currentCommand)
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
    else
    {

        char *envPath = getenv("PATH");
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

// Get the next execution command info, which is the command start from
// current input segmentation index, and end to |, |1 or !1.
void getCurrentCommandInfo(InputSegmentationTable &inputSegmentationTable, Command &currentCommand, PipeConfigTable &pipeConfigTable, int clientSocket)
{
    setCommandInfos(inputSegmentationTable, currentCommand);
    getExecutablePath(currentCommand);
    currentCommand.inputPipe = getInputPipe(pipeConfigTable);
    currentCommand.outputPipe = getOutputPipe(currentCommand, pipeConfigTable, clientSocket);
    currentCommand.errorPipe = getErrorPipe(currentCommand, pipeConfigTable, clientSocket);
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

void execCurrentCommand(Command &currentCommand)
{
    char *executablePath = currentCommand.executablePath;

    if (!strcmp(executablePath, "setenv"))
    {
        setenv(currentCommand.argv[1], currentCommand.argv[2], 1);
    }
    else if (!strcmp(executablePath, "printenv"))
    {
        char *msg = getenv(currentCommand.argv[1]);
        char output[MAX_SINGLE_INPUT_LENGTH];
        sprintf(output, "%s\n", msg);
        if (msg)
        {
            write(currentCommand.outputPipe, output, strlen(output));
        }
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

// TODO
void closeAndReorganizePipe(PipeConfigTable &pipeConfigTable)
{
}

void execSocket(int clientSocket)
{
    // For single-line input.
    char input[MAX_SINGLE_INPUT_LENGTH];
    PipeConfigTable pipeConfigTable;

    initEnvPath();
    initInput(input);

    while (1)
    {
        InputSegmentationTable inputSegmentationTable;

        printPrefixSymbol(clientSocket);

        // Read the data from client.
        char readBuffer[MAX_SINGLE_INPUT_LENGTH];
        memset(&input, '\0', sizeof(input));
        do
        {
            memset(&readBuffer, '\0', sizeof(readBuffer));
            read(clientSocket, readBuffer, sizeof(readBuffer));
            strcat(input, readBuffer);
        } while (readBuffer[strlen(readBuffer) - 1] != '\n');

        strtok(input, "\r\n");

        // New line doesn't count as one line for numbered pipe.
        if (!strcmp(input, ""))
        {
            continue;
        }
        else if (!strcmp(input, "exit"))
        {
            return;
        }

        countdownAllPipes(pipeConfigTable);
        splitInput(input, inputSegmentationTable);

        int inputSegmentationTableLength = inputSegmentationTable.length;

        while (inputSegmentationTable.currentIndex < inputSegmentationTableLength)
        {
            Command currentCommand = currentCommand;

            initCurrentCommand(currentCommand);
            getCurrentCommandInfo(inputSegmentationTable, currentCommand, pipeConfigTable, clientSocket);

            // cout << "argc:" << currentCommand.argc << "\n";
            // cout << "argv:" << currentCommand.argv[0] << "\n";
            // cout << "operation:" << currentCommand.operation << "\n";
            // cout << "pipeType:" << currentCommand.pipeType << "\n";
            // cout << "countdown:" << currentCommand.countdown << "\n";
            // cout << "filePath:" << currentCommand.filePath << "\n";
            // cout << "--------------------------------------------------"
            //      << "\n";

            execCurrentCommand(currentCommand);
            // closeAndReorganizePipe(pipeConfigTable);
        }
    }
}

int main(int argc, char *argv[])
{
    int serverSocket, clientSocket, addressLength, port;
    sockaddr_in clientAddress;

    port = atoi(argv[1]);
    // Get server socket fd.
    serverSocket = passiveTCP(port);

    while (1)
    {
        addressLength = sizeof(clientAddress);
        clientSocket = accept(serverSocket, (sockaddr *)&clientAddress, (socklen_t *)&addressLength);

        cout << "Client Socket: " << clientSocket << endl;

        if (clientSocket == -1)
        {
            perror("Error: accept failed");
            exit(0);
        }

        execSocket(clientSocket);
        close(clientSocket);
    }
    close(serverSocket);
    return 0;
}