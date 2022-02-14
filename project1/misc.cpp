#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>

using namespace std;

// Execute executable.

int main() {
    printf("entering main process---\n");

    // execl("/bin/cat", "cat", "./shell.cpp", NULL);
    // execl("./bin/ls", "ls", NULL);
    // execl("./bin/number", "number", "./test.html", NULL);
    // TODO: Check how to connect number, removetag to pipe.
    // execl("./bin/number", "number", NULL);
    execl("./bin/removetag", "removetag", "test.html", NULL);
    // execl("./bin/removetag0", "removetag0", NULL);
    printf("exiting main process ----\n");
    
    return 0;
}


// Implement Fork, wait.
/*
int main() {
    pid_t pid;

    pid = fork();

    // child process.
    if (pid == 0) {
        cout << "child process";
    // parent process
    } else if(pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        cout << "parent process" << pid;
    }

    return 0;
}
*/

// Implement pipe.
// Implement removetag test.html | number.
/*
int main()
{
    // execl("./bin/removetag", "removetag", "test.html", NULL);
    pid_t pid;
    int pipe1[2];

    // Create pipe.
    if (pipe(pipe1) < 0)
    {
        cout << "can't create pipes";
    }

    pid = fork();
    if (pid < 0)
    {
        cout << "can't fork";
    }
    // parent process.
    else if (pid > 0)
    {
        int status;

        cout << 2;
        close(pipe1[1]);
        dup2(pipe1[0], 0);
        waitpid(pid, &status, 0);
        execl("./bin/number", "number", NULL);
        return 0;
    }
    // child process.
    else
    {
        cout << 1;
        close(pipe1[0]);
        dup2(pipe1[1], 1);
        execl("./bin/removetag", "removetag", "test.html", NULL);
        return 0;
    }
    return 0;
}
*/

// File Redirection
/*
int main()
{
    freopen("hello.txt", "w", stdout);
    execl("./bin/ls", "ls", "-l", NULL);
    fclose(stdout);
    return 0;
}
*/

// Define 2D char array, and interate it.
/*
int main()
{
    char str[10][10] = {"AA", "BB", "CC"};

    strcpy(str[0], "LLL");
    cout << str[0];
    cout << str[1];

    for (int i = 0; i < 2; i++)
    {
        cout << str[i];
    }
}
*/
