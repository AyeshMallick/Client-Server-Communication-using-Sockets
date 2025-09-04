// So this is the server S1 who is the main middleman between client and all the servers. It gives an illusion to the client that this is the only server client is coordinating with 
// but S1 in background sends non .c files servers S2, S3 and S4.


// including necessary header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <dirent.h>    // this is needed for DIR, struct dirent, opendir(), readdir(), closedir() 
#include <sys/types.h> // this is needed for additional type definitions 

#define PORT 4560 // port number for client-S1
#define BUFFER_SIZE 4096
#define S2_PORT 4561 // port number for S1-S2
#define S3_PORT 4562 // port number for S1-S3
#define S4_PORT 4563  //port number for S1-S4 
#define MAX_FILES 1000  // Maximum number of files to process 


//I am using this strip function to ensure files go to the exact destination path entered by user
static const char* strip_s1_prefix(const char *p) {
    if (strncmp(p, "~/S1/", 5) == 0) return p + 5;
    if (strcmp(p,  "~/S1") == 0)     return "";
    if (strncmp(p,  "~S1/", 4) == 0) return p + 4;
    if (strcmp(p,   "~S1") == 0)     return "";
    return p;
}



// here I am predefining function prototypes
void send_c_tar(int client_sock);
int stream_tar_from_server(int client_sock, const char *filetype, int server_port);

// this is a function to create directories recursively as per the path mentioned by user
void create_directories(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    int len = strlen(tmp);


    for (int i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777); //  directory with full permissions
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0777); // Creating the final directory
}

// Function to save data locally to a specified path
void save_locally(const char *filename, const char *data, int size, const char *dest_path) {
    const char *home = getenv("HOME");
    char full_path[1024];

    // including the destination path so that it does not save in home or wrong folder
    if (strncmp(dest_path, "~S1/", 4) == 0) {
    snprintf(full_path, sizeof(full_path), "%s/S1/%s", home, dest_path + 4);
} else if (strncmp(dest_path, "~/S1/", 5) == 0) {
    snprintf(full_path, sizeof(full_path), "%s/S1/%s", home, dest_path + 5);
} else if (strncmp(dest_path, "~/", 2) == 0) {
    snprintf(full_path, sizeof(full_path), "%s/%s", home, dest_path + 2);
} else {
    snprintf(full_path, sizeof(full_path), "%s/S1/%s", home, dest_path);
}


    create_directories(full_path); // Creating necessary directories

    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", full_path, filename);

    // here I am opening file for writing
    FILE *fp = fopen(file_path, "wb");
    if (fp) {
        fwrite(data, 1, size, fp); // Writing data to file
        fclose(fp);
        printf("Stored .c file at %s\n", file_path);
    } else {
        perror("Error writing .c file"); // Error handling for file write
    }
}

// Function to forward file data to a specified server
void forward_to_server(const char *filename, const char *data, int size, const char *dest_path, int server_port, const char *server_name) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket to server failed");
        return;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // connceting to required server for file transfer
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Step 1: Sending the "UPLOAD" command
    char cmd[10] = "UPLOAD";
    send(sock, cmd, sizeof(cmd), 0);  // here I am basicaly notifying server it's an upload

    // Step 2: Sending the filename, path, size, and data
    send(sock, filename, 256, 0);
    send(sock, dest_path, 256, 0);
    send(sock, &size, sizeof(int), 0);
    send(sock, data, size, 0);

    printf("Forwarded file to %s\n", server_name);
    close(sock); 
}

// now a function to resolve file path, handling ~ expansion
void resolve_path(const char *input_path, char *resolved_path, size_t resolved_size) {
    const char *home = getenv("HOME");
    
    // Handling ~/ at the beginning
    if (strncmp(input_path, "~/", 2) == 0) {
        snprintf(resolved_path, resolved_size, "%s/%s", home, input_path + 2);
    }
    // Handling ~S1/ at the beginning (special case for this application)
    else if (strncmp(input_path, "~S1/", 4) == 0) {
        snprintf(resolved_path, resolved_size, "%s/S1/%s", home, input_path + 4);
    }
    // Handling the absolute path
    else if (input_path[0] == '/') {
        strncpy(resolved_path, input_path, resolved_size);
        resolved_path[resolved_size - 1] = '\0';
    }
    // Handling the relative path
    else {
        snprintf(resolved_path, resolved_size, "%s/S1/%s", home, input_path);
    }
}

// now I am extracting the path components from an S1 path to be used for S2/S3/S4
void extract_path_components(const char *s1_path, char *server_path, size_t max_len) {
    const char *home = getenv("HOME");
    char s1_prefix[1024];
    snprintf(s1_prefix, sizeof(s1_prefix), "%s/S1/", home);
    
    // If the path starts with the home/S1 directory
    if (strncmp(s1_path, s1_prefix, strlen(s1_prefix)) == 0) {
        // then I will extract the part after HOME/S1/
        strncpy(server_path, s1_path + strlen(s1_prefix), max_len);
        server_path[max_len - 1] = '\0';
    } 
    // If it's a path like ~S1/something
    else if (strncmp(s1_path, "~S1/", 4) == 0) {
        strncpy(server_path, s1_path + 4, max_len);
        server_path[max_len - 1] = '\0';
    }
    // Otherwise I am just copying the relative path
    else {
        strncpy(server_path, s1_path, max_len);
        server_path[max_len - 1] = '\0';
    }
}

// Now this is a function to get file from another server (S2/S3/S4)
static int get_file_from_server(int client_sock, const char *path, int server_port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        perror("Socket creation failed");
        return 0;
    }
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    //  connecting to the server
    if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        close(server_sock);
        perror("Connection to server failed");
        return 0;
    }
    
    // Sending the "DOWNLOAD" command to the server
    char cmd[10] = "DOWNLOAD";
    send(server_sock, cmd, sizeof(cmd), 0);
    
    // Extracting the path components after S1 prefix
    char server_path[512];
    resolve_path(path, server_path, sizeof(server_path));
    
    char relative_path[512];
    extract_path_components(server_path, relative_path, sizeof(relative_path));
    
    // Sending the path to the server
    send(server_sock, relative_path, sizeof(relative_path), 0);
    
    // Getting the file size from the server
    int file_size = 0;
    recv(server_sock, &file_size, sizeof(int), 0);
    
    if (file_size <= 0) {
        // Forwarding the error to client
        send(client_sock, &file_size, sizeof(int), 0);
        close(server_sock);
        return 0;
    }
    
    // now sending file size to client
    send(client_sock, &file_size, sizeof(int), 0);
    
    // transfering data from server to client
    char buffer[BUFFER_SIZE];
    int bytes_read = 0;
    int total_read = 0;
    
    while (total_read < file_size) {
        bytes_read = recv(server_sock, buffer, 
                          (file_size - total_read < BUFFER_SIZE) ? (file_size - total_read) : BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            perror("Error receiving data from server");
            break;
        }
        send(client_sock, buffer, bytes_read, 0);
        total_read += bytes_read;
    }
    
    close(server_sock);
    return 1;
}

// now the function to handle file download requests
int handle_download(int client_sock, const char *path) {
    char *ext = strrchr(path, '.');
    char resolved_path[1024];
    
    // Checking the file extension
    if (!ext || (strcmp(ext, ".c") != 0 && 
                strcmp(ext, ".pdf") != 0 && 
                strcmp(ext, ".txt") != 0 && 
                strcmp(ext, ".zip") != 0)) {
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        return 0;
 }
    
    // For .c files, I am resolving path and checking locally
    if (strcmp(ext, ".c") == 0) {
        resolve_path(path, resolved_path, sizeof(resolved_path));
        
        printf("Looking for .c file at: %s\n", resolved_path);
        
        FILE *fp = fopen(resolved_path, "rb");
        if (!fp) {
            perror("File open error");
            int error_code = -1;
            send(client_sock, &error_code, sizeof(int), 0);
            return 0;
        }
        
       
        fseek(fp, 0, SEEK_END);
        int file_size = ftell(fp);
        rewind(fp);
        
        // Sending the file size
        send(client_sock, &file_size, sizeof(int), 0);
        
        // Reading and sending file data
        char *buffer = malloc(file_size);
        fread(buffer, 1, file_size, fp);
        send(client_sock, buffer, file_size, 0);
        
        free(buffer);
        fclose(fp);
        printf("Sent .c file to client: %s\n", resolved_path);
        return 1;
    }
    // For .pdf files, I have to get from S2
    else if (strcmp(ext, ".pdf") == 0) {
        printf("Retrieving .pdf file from S2: %s\n", path);
        return get_file_from_server(client_sock, path, S2_PORT);
    }
    // For .txt files, I have to get from S3
    else if (strcmp(ext, ".txt") == 0) {
        printf("Retrieving .txt file from S3: %s\n", path);
        return get_file_from_server(client_sock, path, S3_PORT);
    }
    // For .zip files, I have to get from S4
    else if (strcmp(ext, ".zip") == 0) {
        printf("Retrieving .zip file from S4: %s\n", path);
        return get_file_from_server(client_sock, path, S4_PORT);
    }
    
    return 0;
}


// now the function to remove file from S1, S2, or S3 (local or remote)
int handle_remove(int client_sock, const char *path) {
    char *ext = strrchr(path, '.');
    char resolved_path[1024];
    int status_code = 0;  // 0 for Success, 1 for file not found and 2 for permission denied

   
    if (!ext || (strcmp(ext, ".c") != 0 && 
                strcmp(ext, ".pdf") != 0 && 
                strcmp(ext, ".txt") != 0 && 
                strcmp(ext, ".zip") != 0)) {
        status_code = 1;  // File not found/supported
        send(client_sock, &status_code, sizeof(int), 0);
        return 0;
    }

    // For .c files, resolve path and remove locally
    if (strcmp(ext, ".c") == 0) {
        resolve_path(path, resolved_path, sizeof(resolved_path));
        
        printf("Attempting to remove .c file: %s\n", resolved_path);
        
        // I am trying to access the file first
        if (access(resolved_path, F_OK) != 0) {
            status_code = 1;  // File not found
            send(client_sock, &status_code, sizeof(int), 0);
            return 0;
        }

        // then I am trying to remove the file
        if (remove(resolved_path) != 0) {
            status_code = 2; 
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Error removing .c file");
            return 0;
        }


        status_code = 0;
        send(client_sock, &status_code, sizeof(int), 0);
        printf("Successfully removed .c file: %s\n", resolved_path);
        return 1;
    }

    // For .pdf files, forwarding the remove request to S2
    else if (strcmp(ext, ".pdf") == 0) {
        // Creating socket to S2
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            status_code = 2; 
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Socket creation to S2 failed");
            return 0;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(S2_PORT);
        serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

 if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            status_code = 2; 
            send(client_sock, &status_code, sizeof(int), 0);
            close(server_sock);
            perror("Connection to S2 failed");
            return 0;
        }

        // Sending the "REMOVE" command to S2
        char cmd[10] = "REMOVE";
        send(server_sock, cmd, sizeof(cmd), 0);

        // Extracting the path components after S1 prefix
        char server_path[512];
        resolve_path(path, server_path, sizeof(server_path));

        char relative_path[512];
        extract_path_components(server_path, relative_path, sizeof(relative_path));

        send(server_sock, relative_path, sizeof(relative_path), 0);

        // Getting status code from S2
        recv(server_sock, &status_code, sizeof(int), 0);
        close(server_sock);

        // Forwarding tje status code to client
        send(client_sock, &status_code, sizeof(int), 0);
        printf("PDF file removal request forwarded to S2. Status: %d\n", status_code);
        return 1;
    }

    // For .txt files, forwarding the remove request to S3
    else if (strcmp(ext, ".txt") == 0) {
        // Creating a socket to S3
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            status_code = 2;  
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Socket creation to S3 failed");
            return 0;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(S3_PORT);
        serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            status_code = 2; 
            send(client_sock, &status_code, sizeof(int), 0);
            close(server_sock);
            perror("Connection to S3 failed");
            return 0;
        }

        // Sending the "REMOVE" command to S3
        char cmd[10] = "REMOVE";
        send(server_sock, cmd, sizeof(cmd), 0);

        // Extracting path components after S1 prefix
        char server_path[512];
        resolve_path(path, server_path, sizeof(server_path));

        char relative_path[512];
        extract_path_components(server_path, relative_path, sizeof(relative_path));

        // Sending the path to the server
        send(server_sock, relative_path, sizeof(relative_path), 0);

        // Getting status code from S3
        recv(server_sock, &status_code, sizeof(int), 0);
        close(server_sock);

        // Forwarding status code to client
        send(client_sock, &status_code, sizeof(int), 0);
        printf("TXT file removal request forwarded to S3. Status: %d\n", status_code);
        return 1;
    }

    // For .zip files, forwarding the remove request to S4
    else if (strcmp(ext, ".zip") == 0) {
        // Creating the socket to S4
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            status_code = 2; 
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Socket creation to S4 failed");
            return 0;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(S4_PORT);
        serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            status_code = 2; 
            send(client_sock, &status_code, sizeof(int), 0);
            close(server_sock);
            perror("Connection to S4 failed");
            return 0;
        }

        // Sending the "REMOVE" command to S4
        char cmd[10] = "REMOVE";
        send(server_sock, cmd, sizeof(cmd), 0);

        // Extracting the path components after S1 prefix
        char server_path[512];
        resolve_path(path, server_path, sizeof(server_path));

        char relative_path[512];
        extract_path_components(server_path, relative_path, sizeof(relative_path));

        // Sending the path to the server
        send(server_sock, relative_path, sizeof(relative_path), 0);

        // here I am getting the status code from S4
        recv(server_sock , &status_code, sizeof(int), 0);
        close(server_sock);

        // Forwarding the status code to client
        send(client_sock, &status_code, sizeof(int), 0);
        printf("ZIP file removal request forwarded to S4. Status: %d\n", status_code);
        return 1;
    }

    return 0; // Returning 0 if no valid file type was found
}


// this is the handle_tarfetch to stream directly to client
void handle_tarfetch(int client_sock, const char *filetype) {
    if (strcmp(filetype, ".c") == 0) {
        printf("Creating .c tar file for client\n");
        send_c_tar(client_sock);
    } 
    else if (strcmp(filetype, ".pdf") == 0) {
        printf("Forwarding PDF tar request to S2\n");
        if (stream_tar_from_server(client_sock, filetype, S2_PORT) != 0) {
            int error = -1;
            send(client_sock, &error, sizeof(int), 0);
        }
    }
    else if (strcmp(filetype, ".txt") == 0) {
        printf("Forwarding TXT tar request to S3\n");
        if (stream_tar_from_server(client_sock, filetype, S3_PORT) != 0) {
            int error = -1;
            send(client_sock, &error, sizeof(int), 0);
        }
    }
    else {
        // Invalid file type
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
    }
}

// now here is a function to create a tar file for .c files (local to S1) and send to client
void send_c_tar(int client_sock) {
    const char *home = getenv("HOME");
    char command[1024];
    char tar_name[] = "cfiles.tar";  // the name that client will receive
    
    // Creating the tar file in /tmp directory to avoid permission issues
    char tmp_tar_path[1024];
    snprintf(tmp_tar_path, sizeof(tmp_tar_path), "/tmp/cfiles_%d.tar", getpid());
    
    // Creating the tar file
    snprintf(command, sizeof(command),
         "find %s/S1 -type f -name '*.c' -print0 | tar --null -T - -cf %s",
         home, tmp_tar_path); // .tar file saves only c files for s1

    int result = system(command);
    
    if (result != 0) {
        printf("Error creating .c tar file\n");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    
    // Opening the tar file
    FILE *fp = fopen(tmp_tar_path, "rb");
    if (!fp) {
        perror("Error opening tar file");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        remove(tmp_tar_path); 
        return;
    }
    
    // here I am getting the file size using ftell()
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    rewind(fp);
    
    // Sending the file size to client
    send(client_sock, &file_size, sizeof(int), 0);
    
    // Sending the file data
    char buffer[BUFFER_SIZE];
    while (!feof(fp)) {
        size_t read = fread(buffer, 1, BUFFER_SIZE, fp);
        if (read > 0) {
            send(client_sock, buffer, read, 0);
        }
    }
    
    fclose(fp);
    remove(tmp_tar_path); 
    printf("Sent .c tar file to client (%d bytes)\n", file_size);
}

// Modified request_tar_from_server to stream directly to client
int stream_tar_from_server(int client_sock, const char *filetype, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    // connection with the server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        return -1;
    }

    // sending the TARFETCH command
    char cmd[10] = "TARFETCH";
    send(sock, cmd, sizeof(cmd), 0);
    send(sock, filetype, strlen(filetype)+1, 0);

    // Receiving the file size
    int file_size;
    if (recv(sock, &file_size, sizeof(int), 0) <= 0) {
        close(sock);
        perror("Failed to receive file size");
 return -1;
    }

    if (file_size <= 0) {
        close(sock);
        perror("Received invalid file size");
        return -1;
    }

    // Forwarding the file size to client
    send(client_sock, &file_size, sizeof(int), 0);

    // Streaming the data from server to client
    char buffer[BUFFER_SIZE];
    int remaining = file_size;
    while (remaining > 0) {
        int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
        int received = recv(sock, buffer, to_read, 0);
        if (received <= 0) {
            perror("Error receiving data from server");
            break;
        }
        
        // Forwarding to client
        int sent = send(client_sock, buffer, received, 0);
        if (sent <= 0) {
            perror("Error sending data to client");
            break;
        }
        
        remaining -= received;
    }

    close(sock);
    return (remaining == 0) ? 0 : -1;
}

// Function to get filenames from S2, S3, or S4
int get_filenames_from_server(const char *dir_path, char filenames[][256], int *count, int max_files, int server_port, const char *ext) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket connection to server failed");
        return 0;
    }
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Attempting to connect to the server
    if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(server_sock);
        return 0;
    }
    
    // Sending "LISTFILES" command
    char cmd[10] = "LISTFILES";
    send(server_sock, cmd, sizeof(cmd), 0);
    
    // Extracting the path components after S1 prefix
    char server_path[512];
    extract_path_components(dir_path, server_path, sizeof(server_path));
    
    // Sending the path to the server
    send(server_sock, server_path, sizeof(server_path), 0);
    
    int file_count = 0;
    recv(server_sock, &file_count, sizeof(int), 0);
    
    if (file_count <= 0) {
        close(server_sock);
        return 0;
    }
    
    // Receiving the filenames
    for (int i = 0; i < file_count && *count < max_files; i++) {
        char filename[256];
        recv(server_sock, filename, sizeof(filename), 0);
        
        // Checking if the file has the specified extension
        char *file_ext = strrchr(filename, '.');
        if (file_ext && strcmp(file_ext, ext) == 0) {
            strcpy(filenames[*count], filename);
            (*count)++;
        }
    }
    
    close(server_sock);
    return 1;
}

// Comparing function for qsort to sort filenames alphabetically
int compare_filenames(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Function to handle the dispfnames command
int handle_dispfnames(int client_sock, const char *dir_path) {
    char resolved_path[1024];
    resolve_path(dir_path, resolved_path, sizeof(resolved_path));
    
    // now I have to check if the directory exists
    DIR *dir = opendir(resolved_path);
    if (!dir) {
        perror("Directory open error");
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        return 0;
    }
    closedir(dir);
    
    // here I am declaring some arrays to store filenames by type
    char c_files[MAX_FILES][256];
    char pdf_files[MAX_FILES][256];
    char txt_files[MAX_FILES][256];
    char zip_files[MAX_FILES][256];
    int c_count = 0, pdf_count = 0, txt_count = 0, zip_count = 0;
    
    // fetching the local .c files
    dir = opendir(resolved_path);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && c_count < MAX_FILES) {
        if (entry->d_type == DT_REG) { 
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".c") == 0) {
                strcpy(c_files[c_count], entry->d_name);
                c_count++;
            }
        }
    }
    closedir(dir);
    
    // Getting the .pdf files from S2
    get_filenames_from_server(dir_path, pdf_files, &pdf_count, MAX_FILES, S2_PORT, ".pdf");
    
    // Getting .txt files from S3
    get_filenames_from_server(dir_path, txt_files, &txt_count, MAX_FILES, S3_PORT, ".txt");
    
    // Getting the .zip files from S4
    get_filenames_from_server(dir_path, zip_files, &zip_count, MAX_FILES, S4_PORT, ".zip");
    
    // using qsort to sort each file type alphabetically for better readability
    qsort(c_files, c_count, sizeof(c_files[0]), compare_filenames);
    qsort(pdf_files, pdf_count, sizeof(pdf_files[0]), compare_filenames);
    qsort(txt_files, txt_count, sizeof(txt_files[0]), compare_filenames);
    qsort(zip_files, zip_count, sizeof(zip_files[0]), compare_filenames);
    int total_files = c_count + pdf_count + txt_count + zip_count;
       
    // I am sending the total file count to client
    send(client_sock, &total_files, sizeof(int), 0);
    
    // I am sending filenames in specified order: .c, .pdf, .txt, .zip
    for (int i = 0; i < c_count; i++) {
        send(client_sock, c_files[i], sizeof(c_files[0]), 0);
    }
    
    for (int i = 0; i < pdf_count; i++) {
        send(client_sock, pdf_files[i], sizeof(pdf_files[0]), 0);
    }
    
    for (int i = 0; i < txt_count; i++) {
        send(client_sock, txt_files[i], sizeof(txt_files[0]), 0);
    }
    
    for (int i = 0; i < zip_count; i++) {
        send(client_sock, zip_files[i], sizeof(zip_files[0]), 0);
    }
    
    printf("Sent %d filenames to client for directory '%s'\n", total_files, dir_path);
    return 1;
}

// this is going to be the main function that would handle client requests
void prcclient(int client_sock) {
    while (1) {
        char cmd[10] = {0};
        int n = recv(client_sock, cmd, sizeof(cmd), 0);
        if (n <= 0) {
            close(client_sock);
            break;
        }

       if (strcmp(cmd, "DOWNLOAD") == 0) {
    int num_files;
    if (recv(client_sock, &num_files, sizeof(int), 0) <= 0 || num_files <= 0) {
        printf("Failed to get number of files.\n");
        break;
    }

    // here I am capturing the original list (to preserve client order)
    char reqs[16][512];
    int count = (num_files > 16) ? 16 : num_files;
    for (int i = 0; i < count; i++) {
        memset(reqs[i], 0, sizeof(reqs[i]));
        recv(client_sock, reqs[i], sizeof(reqs[i]), 0);
    }

    // Splitting by server
    char pdf_paths[16][512]; int pdf_n=0;
    char txt_paths[16][512]; int txt_n=0;
    char zip_paths[16][512]; int zip_n=0;

    // Here I am pre-extracting relative paths for remote servers
    for (int i = 0; i < count; i++) {
        char *ext = strrchr(reqs[i], '.');
        if (!ext) continue;
        if (strcmp(ext, ".pdf")==0 || strcmp(ext, ".txt")==0 || strcmp(ext, ".zip")==0) {
            char tmp[512]; char rel[512];
            resolve_path(reqs[i], tmp, sizeof(tmp));
            extract_path_components(tmp, rel, sizeof(rel));
            if (strcmp(ext, ".pdf")==0) { strncpy(pdf_paths[pdf_n++], rel, sizeof(pdf_paths[0])-1); }
            else if (strcmp(ext, ".txt")==0) { strncpy(txt_paths[txt_n++], rel, sizeof(txt_paths[0])-1); }
            else { strncpy(zip_paths[zip_n++], rel, sizeof(zip_paths[0])-1); }
        }
    }

    // basically in the follwing lines of code I have tried to open one connection per server and send their lists
    int s2=-1, s3=-1, s4=-1;
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa)); sa.sin_family=AF_INET; sa.sin_addr.s_addr=inet_addr("127.0.0.1");
    char dcmd[10]="DOWNLOAD";

    if (pdf_n>0) {
        s2 = socket(AF_INET, SOCK_STREAM, 0); sa.sin_port=htons(S2_PORT);
        if (connect(s2,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S2 connect"); s2=-1; }
        else {
            send(s2, dcmd, sizeof(dcmd), 0);
            send(s2, &pdf_n, sizeof(int), 0);
            for (int i=0;i<pdf_n;i++) send(s2, pdf_paths[i], sizeof(pdf_paths[i]), 0);
        }
    }
    if (txt_n>0) {
        s3 = socket(AF_INET, SOCK_STREAM, 0); sa.sin_port=htons(S3_PORT);
        if (connect(s3,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S3 connect"); s3=-1; }
        else {
            send(s3, dcmd, sizeof(dcmd), 0);
            send(s3, &txt_n, sizeof(int), 0);
            for (int i=0;i<txt_n;i++) send(s3, txt_paths[i], sizeof(txt_paths[i]), 0);
        }
    }
    if (zip_n>0) {
        s4 = socket(AF_INET, SOCK_STREAM, 0); sa.sin_port=htons(S4_PORT);
        if (connect(s4,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S4 connect"); s4=-1; }
        else {
            send(s4, dcmd, sizeof(dcmd), 0);
            send(s4, &zip_n, sizeof(int), 0);
            for (int i=0;i<zip_n;i++) send(s4, zip_paths[i], sizeof(zip_paths[i]), 0);
        }
    }

    // For each file in original order, I am serving .c locally, and for others I am trying to read next response from its server and then forwarding to client
    int idx_pdf=0, idx_txt=0, idx_zip=0;
    for (int i = 0; i < count; i++) {
        char *ext = strrchr(reqs[i], '.');
        if (ext && strcmp(ext, ".c")==0) {
            handle_download(client_sock, reqs[i]);
        } else if (ext && strcmp(ext, ".pdf")==0 && s2!=-1) {
            int fs=0; recv(s2, &fs, sizeof(int), 0); send(client_sock, &fs, sizeof(int), 0);
            if (fs>0) {
                int left=fs; char buf[BUFFER_SIZE];
                while (left>0) { int r=recv(s2, buf, left>BUFFER_SIZE?BUFFER_SIZE:left, 0); if (r<=0) break; send(client_sock, buf, r, 0); left-=r; }
            }
            idx_pdf++;
        } else if (ext && strcmp(ext, ".txt")==0 && s3!=-1) {
            int fs=0; recv(s3,&fs,sizeof(int),0); send(client_sock,&fs,sizeof(int),0);
            if (fs>0) {
                int left=fs; char buf[BUFFER_SIZE];
                while (left>0) { int r=recv(s3, buf, left>BUFFER_SIZE?BUFFER_SIZE:left, 0); if (r<=0) break; send(client_sock, buf, r, 0); left-=r; }
            }
            idx_txt++;
        } else if (ext && strcmp(ext, ".zip")==0 && s4!=-1) {
            int fs=0; recv(s4,&fs,sizeof(int),0); send(client_sock,&fs,sizeof(int),0);
            if (fs>0) {
                int left=fs; char buf[BUFFER_SIZE];
                while (left>0) { int r=recv(s4, buf, left>BUFFER_SIZE?BUFFER_SIZE:left, 0); if (r<=0) break; send(client_sock, buf, r, 0); left-=r; }
            }
            idx_zip++;
        } else {
            int err=-1; send(client_sock, &err, sizeof(int), 0);
        }
    }

    if (s2!=-1) close(s2);
    if (s3!=-1) close(s3);
    if (s4!=-1) close(s4);
}



       else if (strcmp(cmd, "REMOVE") == 0) {
    int num_files;
    if (recv(client_sock, &num_files, sizeof(int), 0) <= 0 || num_files <= 0) {
        printf("Failed to get number of files.\n");
        break;
    }

    char reqs[16][512];
    int count = (num_files > 16) ? 16 : num_files;
    for (int i = 0; i < count; i++) {
        memset(reqs[i],0,sizeof(reqs[i]));
        recv(client_sock, reqs[i], sizeof(reqs[i]), 0);
    }

    char pdf_paths[16][512]; int pdf_n=0;
    char txt_paths[16][512]; int txt_n=0;
    char zip_paths[16][512]; int zip_n=0;

    for (int i = 0; i < count; i++) {
        char *ext = strrchr(reqs[i], '.');
        if (!ext) continue;
        if (strcmp(ext, ".pdf")==0 || strcmp(ext, ".txt")==0 || strcmp(ext, ".zip")==0) {
            char tmp[512], rel[512];
            resolve_path(reqs[i], tmp, sizeof(tmp));
            extract_path_components(tmp, rel, sizeof(rel));
            if (strcmp(ext, ".pdf")==0) strncpy(pdf_paths[pdf_n++], rel, sizeof(pdf_paths[0])-1);
            else if (strcmp(ext, ".txt")==0) strncpy(txt_paths[txt_n++], rel, sizeof(txt_paths[0])-1);
            else strncpy(zip_paths[zip_n++], rel, sizeof(zip_paths[0])-1);
        }
    }

    int s2=-1, s3=-1, s4=-1;
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa)); sa.sin_family=AF_INET; sa.sin_addr.s_addr=inet_addr("127.0.0.1");
    char rcmd[10]="REMOVE";

    if (pdf_n>0) {
        s2 = socket(AF_INET,SOCK_STREAM,0); sa.sin_port=htons(S2_PORT);
        if (connect(s2,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S2 connect"); s2=-1; }
        else {
            send(s2, rcmd, sizeof(rcmd), 0);
            send(s2, &pdf_n, sizeof(int), 0);
            for (int i=0;i<pdf_n;i++) send(s2, pdf_paths[i], sizeof(pdf_paths[i]), 0);
        }
    }
    if (txt_n>0) {
        s3 = socket(AF_INET,SOCK_STREAM,0); sa.sin_port=htons(S3_PORT);
        if (connect(s3,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S3 connect"); s3=-1; }
        else {
            send(s3, rcmd, sizeof(rcmd), 0);
            send(s3, &txt_n, sizeof(int), 0);
            for (int i=0;i<txt_n;i++) send(s3, txt_paths[i], sizeof(txt_paths[i]), 0);
        }
    }
    if (zip_n>0) {
        s4 = socket(AF_INET,SOCK_STREAM,0); sa.sin_port=htons(S4_PORT);
        if (connect(s4,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S4 connect"); s4=-1; }
        else {
            send(s4, rcmd, sizeof(rcmd), 0);
            send(s4, &zip_n, sizeof(int), 0);
            for (int i=0;i<zip_n;i++) send(s4, zip_paths[i], sizeof(zip_paths[i]), 0);
        }
    }

    // Reading the results in original order and forward to client
    for (int i = 0; i < count; i++) {
        char *ext = strrchr(reqs[i], '.');
        if (ext && strcmp(ext, ".c")==0) {
            // locally remove files
            int ok = handle_remove(client_sock, reqs[i]); (void)ok; // it already sends status
        } else if (ext && strcmp(ext, ".pdf")==0 && s2!=-1) {
            int status=1; recv(s2,&status,sizeof(int),0); send(client_sock,&status,sizeof(int),0);
        } else if (ext && strcmp(ext, ".txt")==0 && s3!=-1) {
            int status=1; recv(s3,&status,sizeof(int),0); send(client_sock,&status,sizeof(int),0);
        } else if (ext && strcmp(ext, ".zip")==0 && s4!=-1) {
            int status=1; recv(s4,&status,sizeof(int),0); send(client_sock,&status,sizeof(int),0);
        } else {
            int status=1; send(client_sock,&status,sizeof(int),0); // not found/unsupported
        }
    }

    if (s2!=-1) close(s2);
    if (s3!=-1) close(s3);
    if (s4!=-1) close(s4);
}



        else if (strcmp(cmd, "TARFETCH") == 0) {
            char filetype[10] = {0};
            recv(client_sock, filetype, sizeof(filetype), 0);
            printf("Tar request received for: %s files\n", filetype);
            handle_tarfetch(client_sock, filetype);
        }

        else if (strcmp(cmd, "LISTFILES") == 0) {
            char dir_path[512] = {0};
            recv(client_sock, dir_path, sizeof(dir_path), 0);
            
            printf("Directory listing request received for: %s\n", dir_path);
            
            // Handling list files request
            handle_dispfnames(client_sock, dir_path);
            close(client_sock);
            continue;            
        }

        else if (strcmp(cmd, "UPLOAD") == 0) {
    int num_files;
    if (recv(client_sock, &num_files, sizeof(int), 0) <= 0 || num_files <= 0) {
        printf("Failed to get number of files.\n");
        break;
    }

    // I am trying to collect everything first
    typedef struct { char name[256]; char dest[256]; int size; char *data; char ext[8]; } FileRec;
    FileRec recs[16]; int n=(num_files>16)?16:num_files; memset(recs,0,sizeof(recs));

    for (int i=0;i<n;i++) {
        if (recv(client_sock, recs[i].name, sizeof(recs[i].name), 0) <= 0 ||
            recv(client_sock, recs[i].dest, sizeof(recs[i].dest), 0) <= 0 ||
            recv(client_sock, &recs[i].size, sizeof(int), 0) <= 0) {
            printf("Upload data receive failed\n");
            n=i;
            break;
        }
        recs[i].data = (char*)malloc(recs[i].size);
        int got=0; while (got < recs[i].size) {
            int r = recv(client_sock, recs[i].data+got, recs[i].size-got, 0);
            if (r<=0) break; got+=r;
        }
        char *e = strrchr(recs[i].name,'.'); strncpy(recs[i].ext, e?e:"", sizeof(recs[i].ext)-1);
    }

    // Counting per server
    int pdf_n=0, txt_n=0, zip_n=0;
    for (int i=0;i<n;i++) {
        if (strcmp(recs[i].ext,".pdf")==0) pdf_n++;
        else if (strcmp(recs[i].ext,".txt")==0) txt_n++;
        else if (strcmp(recs[i].ext,".zip")==0) zip_n++;
    }

    // saving .c locally
    for (int i=0;i<n;i++) {
        if (strcmp(recs[i].ext,".c")==0) {
            save_locally(recs[i].name, recs[i].data, recs[i].size, recs[i].dest);
        }
    }

    // now this will be a helper to send a batch  at a time to a server
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa)); sa.sin_family=AF_INET; sa.sin_addr.s_addr=inet_addr("127.0.0.1");
    char ucmd[10]="UPLOAD";

    int s2=-1, s3=-1, s4=-1;
    if (pdf_n>0) {
        s2 = socket(AF_INET, SOCK_STREAM, 0); sa.sin_port=htons(S2_PORT);
        if (connect(s2,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S2 connect"); s2=-1; }
        else {
            send(s2, ucmd, sizeof(ucmd), 0);
            send(s2, &pdf_n, sizeof(int), 0);
            for (int i=0;i<n;i++) if (strcmp(recs[i].ext,".pdf")==0) {
                send(s2, recs[i].name, sizeof(recs[i].name), 0);
                send(s2, recs[i].dest, sizeof(recs[i].dest), 0);
                send(s2, &recs[i].size, sizeof(int), 0);
                send(s2, recs[i].data, recs[i].size, 0);
            }
            close(s2);
        }
    }
    if (txt_n>0) {
        s3 = socket(AF_INET, SOCK_STREAM, 0); sa.sin_port=htons(S3_PORT);
        if (connect(s3,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S3 connect"); s3=-1; }
        else {
            send(s3, ucmd, sizeof(ucmd), 0);
            send(s3, &txt_n, sizeof(int), 0);
            for (int i=0;i<n;i++) if (strcmp(recs[i].ext,".txt")==0) {
                send(s3, recs[i].name, sizeof(recs[i].name), 0);
                send(s3, recs[i].dest, sizeof(recs[i].dest), 0);
                send(s3, &recs[i].size, sizeof(int), 0);
                send(s3, recs[i].data, recs[i].size, 0);
            }
            close(s3);
        }
    }
    if (zip_n>0) {
        s4 = socket(AF_INET, SOCK_STREAM, 0); sa.sin_port=htons(S4_PORT);
        if (connect(s4,(struct sockaddr*)&sa,sizeof(sa))<0) { perror("S4 connect"); s4=-1; }
        else {
            send(s4, ucmd, sizeof(ucmd), 0);
            send(s4, &zip_n, sizeof(int), 0);
            for (int i=0;i<n;i++) if (strcmp(recs[i].ext,".zip")==0) {
                send(s4, recs[i].name, sizeof(recs[i].name), 0);
                send(s4, recs[i].dest, sizeof(recs[i].dest), 0);
                send(s4, &recs[i].size, sizeof(int), 0);
                send(s4, recs[i].data, recs[i].size, 0);
            }
            close(s4);
        }
    }

    // removing any non-.c files that might exist under ~/S1/<dest>/<name> according to the requirement of the project 
for (int i = 0; i < n; i++) {
    if (strcmp(recs[i].ext, ".pdf")==0 || strcmp(recs[i].ext, ".txt")==0 || strcmp(recs[i].ext, ".zip")==0) {
        char local_dir[1024];
        resolve_path(recs[i].dest, local_dir, sizeof(local_dir));
        char maybe_local[1200];
        snprintf(maybe_local, sizeof(maybe_local), "%s/%s", local_dir, recs[i].name);
        unlink(maybe_local); // ignore errors
    }
}


    // freeing the buffers
    for (int i=0;i<n;i++) free(recs[i].data);
}


         else {
            printf("Unknown command: %s\n", cmd);
        }
    }

    close(client_sock);
    exit(0); // Child exits after client disconnects
}

// now this is my main function to set up the server
int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket error");
        exit(1);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Binding the socket to the specified port
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind error");
        close(server_sock);
        exit(1);
    }

    // Starting to listening for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("Listen error");
        close(server_sock);
        exit(1);
    }
    printf("S1 server listening on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

        if (client_sock < 0) {
            perror("Accept failed");
            continue; // I have placed continue here to accept next connection
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            close(client_sock);
        } else if (pid == 0) {
            // this is the child process
            close(server_sock); // child doesn't need the listener
            prcclient(client_sock); //  to handle client requests
        } else {
            close(client_sock); // Parent doesn't need this so I am closing it
        }
    }

    return 0; 
}
