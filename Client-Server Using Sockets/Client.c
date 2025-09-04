// This is the client code for my client-servers connection project via sockets. BAsically this client C file is simulating a CLI for the user where 
//the user will be prompted to enter some commands based on the needs of it. It supports upload, download, remove, display functions and it only interacts with the 
// Server S1, since user is unaware of the fact that S1 is storing only .c files and sending all other files to servers S2, S3 and S4 based on whether the file is a 
// .pdf file, .txt file or .zip file. The user can also request a tar file for the particular type of file it wants
// This is done by name - Ayeshkant Mallick
//SID - 110190414


//intially I am declaring all the necessary header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <libgen.h>

// I am predefining and hardcoding the port numbers and buffer size
#define PORT 4560
#define BUFFER_SIZE 4096

//Here the main function starts
int main() {
    char command[1024], filename[256], dest_path[256];

    // the CLI loop for the client
    while (1) {
        printf("s25client$ "); // Prompt for user input
        fgets(command, sizeof(command), stdin); // Reading command from user
        command[strcspn(command, "\n")] = 0;

   // Checking for upload command
if (strncmp(command, "uploadf", 7) == 0) {
    char files_in[10][512], dest_path[512];
    int in_count = 0;

    // Here I am breaking the input into tokens
    char *token = strtok(command, " "); // "uploadf"
    while ((token = strtok(NULL, " ")) && in_count < 10) {
        strncpy(files_in[in_count], token, sizeof(files_in[0]) - 1);
        files_in[in_count][sizeof(files_in[0]) - 1] = '\0';
        in_count++;
    }

    if (in_count < 2) {
        printf("Need at least 1 source file and 1 destination path\n");
        continue;
    }

    strcpy(dest_path, files_in[in_count - 1]);// making sure that the last token is destination path
    int file_count = in_count - 1;

    if (file_count > 3) {
        printf("You can upload at most 3 files.\n");
        continue;
    }


   // checking if the entered detn path starts with ~/S1 or ~S1 or not
    if (!(strncmp(dest_path, "~/S1", 4) == 0 || strncmp(dest_path, "~S1", 3) == 0)) {
        printf("Invalid destination path. Use ~/S1 or ~S1\n");
        continue;
    }

    // here I am adding one extra validation which will only include files that can be opened
    char files[3][512];
    int num_files = 0;
    for (int i = 0; i < file_count; i++) {
        FILE *tf = fopen(files_in[i], "rb");
        if (!tf) {
            fprintf(stderr, "Skip (cannot open): %s\n", files_in[i]);
            continue;
        }
        fclose(tf);
        strncpy(files[num_files], files_in[i], sizeof(files[0]) - 1);
        files[num_files][sizeof(files[0]) - 1] = '\0';
        num_files++;
    }

    if (num_files == 0) {
        printf("No valid files to upload.\n");
        continue;
    }

    // Here I will be creating and connecting the socket
    // I wont be using inet_pton() since I am already hardcoding the IP address here rather than dynamically getting from the user
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); //I am manually feeding the IP address
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sock);
        continue;
    }

    // here I am sending UPLOAD + actual count
    char cmd[10] = "UPLOAD";
    send(sock, cmd, sizeof(cmd), 0);
    send(sock, &num_files, sizeof(int), 0);

    // Sending each file
    for (int i = 0; i < num_files; i++) {
        FILE *fp = fopen(files[i], "rb");
        fseek(fp, 0, SEEK_END);
        int file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *file_data = malloc(file_size);
        fread(file_data, 1, file_size, fp);
        fclose(fp);

        char *path_copy = strdup(files[i]);
        char *filename = basename(path_copy);

        send(sock, filename, 256, 0);
        send(sock, dest_path, 256, 0);
        send(sock, &file_size, sizeof(int), 0);
        send(sock, file_data, file_size, 0);

        free(file_data);
        free(path_copy);
    }

    printf("Uploaded %d file(s) to server path '%s'.\n", num_files, dest_path);
    close(sock);
}




 // Checking for the download command
else if (strncmp(command, "downlf", 6) == 0) {
    char files[10][512];
    int num_files = 0;

    //so basically I am separating the user entered path into tokens 
    char *token = strtok(command, " "); // "downlf"
    token = strtok(NULL, " "); // first file path

    while (token && num_files < 10) {
        strncpy(files[num_files], token, sizeof(files[num_files]) - 1);
        files[num_files][sizeof(files[num_files]) - 1] = '\0';
        num_files++;
        token = strtok(NULL, " ");
    }

    if (num_files == 0) {
        printf("Invalid syntax. Use: downlf file1 [file2 ...]\n");
        continue;
    }

    // max 2 files per download
    if (num_files < 1 || num_files > 2) {
    printf("You can download 1 or 2 files only.\n");
    continue;
    }

    // socket for download
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sock);
        continue;
    }

    char cmd[10] = "DOWNLOAD";
    send(sock, cmd, sizeof(cmd), 0);
    send(sock, &num_files, sizeof(int), 0);

    for (int i = 0; i < num_files; i++) {
        send(sock, files[i], sizeof(files[i]), 0);

        int file_size = 0;
        recv(sock, &file_size, sizeof(int), 0);
        if (file_size < 0) {
            printf("File not found: %s\n", files[i]);
            continue;
        }

        // getting the memory dynamically
        char *file_data = malloc(file_size);
        int received = 0;
        while (received < file_size) {
            int r = recv(sock, file_data + received, file_size - received, 0);
            if (r <= 0) break;
            received += r;
        }

        char *path_copy = strdup(files[i]);
        char *base_name = basename(path_copy);
        FILE *fp = fopen(base_name, "wb");
        fwrite(file_data, 1, file_size, fp);
        fclose(fp);

         printf("Downloaded: %s (%d bytes)\n", base_name, file_size); // printing teh appropriate message for downloaded files

        // closing the descriptors
        free(file_data);
        free(path_copy);

    }

    close(sock);
}

        // now I will be checking for remove command
else if (strncmp(command, "removef", 7) == 0) {
    char files[10][512];
    int num_files = 0;

    char *token = strtok(command, " "); // "removef"
    token = strtok(NULL, " "); // first file path

    // again I am treating the path entered by user as separate tokens
    while (token && num_files < 10) {
        strncpy(files[num_files], token, sizeof(files[num_files]) - 1);
        files[num_files][sizeof(files[num_files]) - 1] = '\0';
        num_files++;
        token = strtok(NULL, " ");
    }

    if (num_files == 0) {
        printf("Invalid syntax. Use: removef file1 [file2 ...]\n");
        continue;
    }

    if (num_files < 1 || num_files > 2) {
    printf("You can remove 1 or 2 files only.\n");
    continue;
    }


    // creating and integrating another socket for the remove command
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sock);
        continue;
    }

    char cmd[10] = "REMOVE";
    send(sock, cmd, sizeof(cmd), 0);
    send(sock, &num_files, sizeof(int), 0);

    // the for loop for implementing the remove command
    for (int i = 0; i < num_files; i++) {
        send(sock, files[i], sizeof(files[i]), 0);
        int status_code = 0;
        recv(sock, &status_code, sizeof(int), 0);
        if (status_code == 0) printf("Removed: %s\n", files[i]);
        else if (status_code == 1) printf("Not found: %s\n", files[i]);
        else if (status_code == 2) printf("Permission denied: %s\n", files[i]);
        else printf("Error removing: %s\n", files[i]);
    }

    close(sock);
}



        // now checking for the download tar command
        else if (strncmp(command, "downltar", 8) == 0) {
            char filetype[10];
            // parsing the command to get the file type
            if (sscanf(command, "downltar %9s", filetype) != 1) {
                printf("Invalid syntax. Use: downltar .filetype (.c/.pdf/.txt)\n");
                continue;
            }

            // here I am validating the file type because no tar files for .zip files
            if (strcmp(filetype, ".c") != 0 && strcmp(filetype, ".pdf") != 0 && strcmp(filetype, ".txt") != 0) {
                printf("Unsupported file type. Only .c, .pdf, and .txt are supported for tar download.\n");
                continue;
            }

            // creating and integrating a socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // as I mentioned, I am hardcoding the IP address

            // Connecting to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                close(sock);
                continue;
            }

            // sending the TARFETCH command and the file type to the server
            char cmd[10] = "TARFETCH";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, filetype, sizeof(filetype), 0);

            // now to receive the size of the tar file from the server
            int file_size = 0;
            int bytes_received = recv(sock, &file_size, sizeof(int), 0);
            if (bytes_received <= 0) {
                printf("Error receiving tar file size.\n");
                close(sock);
                continue;
            }

            // also I need to check if the tar file exists
            if (file_size < 0) {
                printf("Tar file not found or could not be created.\n");
                close(sock);
                continue;
            }

            printf("Receiving tar file of size %d bytes...\n", file_size);

            // getting memory dynamically to receive the tar file data
            char *file_data = malloc(file_size);
            if (!file_data) {
                perror("Memory allocation error");
                close(sock);
                continue;
            }

            // here I have tried to receive the tar file data in chunks
            int received = 0;
            while (received < file_size) {
                int r = recv(sock, file_data + received, file_size - received, 0);
                if (r <= 0) {
                    printf("Connection error while receiving tar file.\n");
                    break;
                }
                received += r;
            }

            // Checking if the entire tar file was received
            if (received < file_size) {
                printf("Warning: Only received %d of %d bytes\n", received, file_size);
                free(file_data);
                close(sock);
                continue;
            }

            // Determining the local tar file name based on the file type
            char tar_name[64];
            if (strcmp(filetype, ".c") == 0)
                strcpy(tar_name, "cfiles.tar");
            else if (strcmp(filetype, ".pdf") == 0)
                strcpy(tar_name, "pdf.tar");
            else
                strcpy(tar_name, "text.tar");

            // now I have to save the received tar file data to a local file
            FILE *fp = fopen(tar_name, "wb");
            if (!fp) {
                perror("Failed to create tar file locally");
                free(file_data);
                close(sock);
                continue;
            }

            // finally writng the received tar data to the file
            size_t written = fwrite(file_data, 1, file_size, fp);
            if (written != file_size) {
                printf("Warning: Only wrote %zu of %d bytes to file\n", written, file_size);
            }

            fclose(fp);
            printf("Downloaded '%s' to current directory (%d bytes).\n", tar_name, file_size);

            // and cleaning up resources
            free(file_data);
            close(sock);
        }

        // now to check for display filenames command
        else if (strncmp(command, "dispfnames", 10) == 0) {
            // Extracting the directory path from the command
            char dir_path[512];
            if (sscanf(command, "dispfnames %511[^\n]", dir_path) != 1) {
                printf("Invalid syntax. Use: dispfnames pathname\n");
                continue;
            }
            
            // Creating another socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 

            // Connecting to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                close(sock);
                continue;
            }

            // Sending the LISTFILES command and the directory path to the server
            char cmd[10] = "LISTFILES";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, dir_path, sizeof(dir_path), 0);

            // Receiving the count of files in the directory
            int file_count = 0;
            recv(sock, &file_count, sizeof(int), 0);
            
            // finally checking for errors in directory access
            if (file_count < 0) {
                printf("Error: Directory not found or access denied.\n");
                close(sock);
                continue;
            }
            
            // Handling the case where no files are found
            if (file_count == 0) {
                printf("No files found in directory '%s'\n", dir_path);
                close(sock);
                continue;
            }
            
            printf("Files in '%s':\n", dir_path);
            
            // I also have to receive and display each filename
            for (int i = 0; i < file_count; i++) {
                char filename[256] = {0};
                recv(sock, filename, sizeof(filename), 0);
                printf("%s\n", filename);
            }
            
            close(sock);
        } 
        // finally for checking for exit command
        else if (strcmp(command, "exit") == 0) {
            break; // Exiting the loop and to terminate the program
        } else {
            printf("Unknown command.\n"); // Handling some unknown commands
        }
    }

    return 0;
}
