// so this is the server S2 which will deal with all .pdf files and receives and sends all pdf files to S1 based on user requests

//all necessary header files included
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <arpa/inet.h>
#include <dirent.h>    // For DIR, struct dirent, opendir(), readdir(), closedir() å
#include <sys/types.h> // For additional type definitions

#define PORT 4561  // S2 port
#define BUFFER_SIZE 4096

// strip_s1_prefix will remove any ~/S1 or ~S1 prefix from a path so that 
//S2 can store files in its own ~/S2/... structure using the rest of the path
static const char* strip_s1_prefix(const char *p) {
    while (*p == ' ' || *p == '\t') p++;

    // ~/S1/... or ~/s1/...
    if (p[0] == '~' && p[1] == '/' && (p[2] == 'S' || p[2] == 's') && p[3] == '1' && p[4] == '/')
        return p + 5;

    // ~S1/... or ~s1/...
    if (p[0] == '~' && (p[1] == 'S' || p[1] == 's') && p[2] == '1' && p[3] == '/')
        return p + 4;

    // exactly "~/S1" or "~/s1"
    if (strcmp(p, "~/S1") == 0 || strcmp(p, "~/s1") == 0)
        return "";

    // exactly "~S1" or "~s1"
    if (strcmp(p, "~S1") == 0 || strcmp(p, "~s1") == 0)
        return "";

    return p;
}


// here is the create directories function
void create_directories(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    int len = strlen(tmp);

    for (int i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0777);
}

void save_file(const char *filename, const char *data, int size, const char *dest_path) {
    const char *home = getenv("HOME");
    const char *rel = strip_s1_prefix(dest_path);
    char full_path[1024];

    // Handling the "~/S2/..." case
    // I am always trying to target S2 using the relative path extracted from the S1 path
    snprintf(full_path, sizeof(full_path), "%s/S2/%s", home, rel);

    // Creating directories if needed
    create_directories(full_path);

    // Final file path
    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", full_path, filename);

    // Write file
    FILE *fp = fopen(file_path, "wb");
    if (fp) {
        fwrite(data, 1, size, fp);
        fclose(fp);
        printf("Stored PDF file at %s\n", file_path);
    } else {
        perror("Error writing PDF file");
    }
}


// Function to handle file download requests
int handle_download(int client_sock, const char *path) {
    const char *home = getenv("HOME");
    char resolved_path[1024];
    
    // Creating path with S2 directory
    snprintf(resolved_path, sizeof(resolved_path), "%s/S2/%s", home, path);
    
    printf("Looking for PDF file at: %s\n", resolved_path);
    
  
    FILE *fp = fopen(resolved_path, "rb");
    if (!fp) {
        fprintf(stderr, "S2: fopen('%s') failed: %s\n", resolved_path, strerror(errno));
        int error_code = -1;
        if (send(client_sock, &error_code, sizeof(int), 0) <= 0) {
    fprintf(stderr, "S2: send(file_size=-1) failed: %s\n", strerror(errno));
}
        return 0;
    }
    
    // Getting the file size
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    rewind(fp);
    
    // Sending file size
    send(client_sock, &file_size, sizeof(int), 0);
    
    // Reading and sending file data
    char *buffer = malloc(file_size);
    fread(buffer, 1, file_size, fp);
    send(client_sock, buffer, file_size, 0);
    
    free(buffer);
    fclose(fp);
    printf("Sent PDF file to S1: %s\n", resolved_path);
    return 1;
}

// Function to resolve file path

void resolve_path(const char *input_path, char *resolved_path, size_t resolved_size) {

    const char *home = getenv("HOME");

    // Handling ~/ at the beginning of the path entered by the user

    if (strncmp(input_path, "~/", 2) == 0) {

        snprintf(resolved_path, resolved_size, "%s/S2/%s", home, input_path + 2);

    }

    // Handling the absolute path

    else if (input_path[0] == '/') {

        snprintf(resolved_path, resolved_size, "%s/S2%s", home, input_path);

    }

    // Handling the relative path

    else {

        snprintf(resolved_path, resolved_size, "%s/S2/%s", home, input_path);

    }

}

// Handlng the file removal request

int handle_remove(int client_sock, const char *path) {

    char resolved_path[1024];

    resolve_path(path, resolved_path, sizeof(resolved_path));

    int status_code = 0;  // 0: Success, 1: File not found, 2: Permission denied/error

    printf("Attempting to remove PDF file: %s\n", resolved_path);

    // Checking if the file exists

    if (access(resolved_path, F_OK) != 0) {

        status_code = 1;  // File not found

        send(client_sock, &status_code, sizeof(int), 0);

        return 0;

    }

    // Trying to remove the file

    if (remove(resolved_path) != 0) {

        status_code = 2;  // Permission denied or other error

        send(client_sock, &status_code, sizeof(int), 0);

        return 0;

    }


    status_code = 0;

    send(client_sock, &status_code, sizeof(int), 0);

    printf("Successfully removed PDF file: %s\n", resolved_path);

    return 1;

}


// now this is the function to create a tar file of all PDF files in S2 directory
int create_pdf_tar(const char *tar_path) {
    const char *home = getenv("HOME");
    char command[2048];  // here I have increased the buffer size for safety
    
    // Creating the tar file with full path
    snprintf(command, sizeof(command), 
             "cd %s/S2 && tar -cf %s $(find . -type f -name '*.pdf' 2>/dev/null)", 
             home, tar_path);
    
    int ret = system(command);
    if (ret != 0) {
        printf("tar command failed with status %d\n", ret);
        return -1;
    }
    
    // Verifying that the tar file was created
    if (access(tar_path, F_OK) != 0) {
        printf("Tar file was not created: %s\n", tar_path);
        return -1;
    }
    
    return 0;
}

// Function to handle TARFETCH requests from S1
void handle_tarfetch(int client_sock) {
    // Creating a unique temp file name using PID
    char tar_path[1024];
    snprintf(tar_path, sizeof(tar_path), "/tmp/pdf_temp_%d.tar", getpid());
    
    if (create_pdf_tar(tar_path) != 0) {
        printf("Error creating PDF tar file\n");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    
    FILE *fp = fopen(tar_path, "rb");
    if (!fp) {
        perror("Error opening tar file");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        remove(tar_path);  
        return;
    }
    
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("Error seeking tar file");
        fclose(fp);
        remove(tar_path);
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    
    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("Error getting tar file size");
        fclose(fp);
        remove(tar_path);
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    rewind(fp);
    
    // Sending the file size to S1
    int file_size_int = (file_size > INT_MAX) ? -1 : (int)file_size;
    if (send(client_sock, &file_size_int, sizeof(int), 0) <= 0) {
        perror("Error sending file size");
        fclose(fp);
        remove(tar_path);
        return;
    }

    if (file_size_int < 0) { 
    fclose(fp);
    remove(tar_path);
    return;
}
    
    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;
    while (!feof(fp)) {
        size_t read = fread(buffer, 1, BUFFER_SIZE, fp);
        if (read > 0) {
            ssize_t sent = send(client_sock, buffer, read, 0);
            if (sent <= 0) {
                perror("Error sending file data");
                break;
            }
            total_sent += sent;
        }
        if (ferror(fp)) {
            perror("Error reading tar file");
            break;
        }
    }
    
    fclose(fp);
    remove(tar_path);
    
    if (total_sent == file_size) {
        printf("Successfully sent PDF tar file to S1 (%ld bytes)\n", file_size);
    } else {
        printf("Warning: Only sent %zu of %ld bytes\n", total_sent, file_size);
    }
}

// now this is a function to handle LISTFILES request for .pdf files
void handle_list_files(int client_sock, const char *dir_path) {
    char resolved_path[1024];
    const char *home = getenv("HOME");
    
    printf("DEBUG: Received path from S1: '%s'\n", dir_path);
    
    // here I am converting ~/S1 to ~/S2 or ~/S1/folder to ~/S2/folder
    char adjusted_path[512] = {0};
    
    if (strncmp(dir_path, "~/S1", 4) == 0) {
        if (strlen(dir_path) > 4) {
            snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path + 5);
        } else {
            adjusted_path[0] = '\0';
        }
    } else if (strncmp(dir_path, "~S1", 3) == 0) {
        // Handling the ~S1 prefix (without slash)
        if (strlen(dir_path) > 3) {
            snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path + 4);
        } else {
            adjusted_path[0] = '\0';
        }
    } else {
        // Any other path is treated as relative to S2 root
        snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path);
    }
    
    printf("DEBUG: Adjusted path: '%s'\n", adjusted_path);
    
    // here I am adjusting the full path for S2
    if (strlen(adjusted_path) > 0) {
        snprintf(resolved_path, sizeof(resolved_path), "%s/S2/%s", home, adjusted_path);
    } else {
        snprintf(resolved_path, sizeof(resolved_path), "%s/S2", home);
    }
    
    printf("DEBUG: Looking for PDF files in: '%s'\n", resolved_path);
    
    //printf("DEBUG: Looking for PDF files in: '%s'\n", resolved_path);
    
    // Checking if the directory exists
    DIR *dir = opendir(resolved_path);
    if (!dir) {
        // Directory not found, so it will send 0 file count
        int file_count = 0;
        perror("DEBUG: Directory open error");
        printf("DEBUG: Failed to open directory '%s'\n", resolved_path);
        send(client_sock, &file_count, sizeof(int), 0);
        return;
    }
    
    // Counting and collecting .pdf files
    struct dirent *entry;
    char filenames[1000][256];  // Up to 1000 files
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL && file_count < 1000) {
        if (entry->d_type == DT_REG) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".pdf") == 0) {
                strcpy(filenames[file_count], entry->d_name);
                printf("DEBUG: Found PDF file: %s\n", entry->d_name);
                file_count++;
            }
        }
    }
    closedir(dir);
    
    printf("DEBUG: Total PDF files found: %d\n", file_count);
    
    // Sending the total file count
    send(client_sock, &file_count, sizeof(int), 0);
    
    // Sending each filename
    for (int i = 0; i < file_count; i++) {
        send(client_sock, filenames[i], sizeof(filenames[0]), 0);
        printf("DEBUG: Sent filename: %s\n", filenames[i]);
    }
    
    printf("S2: Sent %d .pdf filenames to S1 for directory '%s'\n", file_count, dir_path);
}


int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
if (server_sock < 0) {
    perror("S2 socket");
    exit(1);
}

int opt = 1;
if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("S2 setsockopt");
    close(server_sock);
    exit(1);
}

struct sockaddr_in server_addr;
memset(&server_addr, 0, sizeof(server_addr));
server_addr.sin_family = AF_INET;
server_addr.sin_port   = htons(PORT);
server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("S2 bind");
    close(server_sock);
    exit(1);
}

if (listen(server_sock, 5) < 0) {
    perror("S2 listen");
    close(server_sock);
    exit(1);
}

printf("S2 server listening on port %d...\n", PORT);


    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

        char cmd[10] = {0};
        recv(client_sock, cmd, sizeof(cmd), 0);
        
        // here I am checking if this is a download request
        if (strcmp(cmd, "DOWNLOAD") == 0) {
    int num_files = 0;

    // First it will receive how many file paths S1 will send
    if (recv(client_sock, &num_files, sizeof(int), 0) <= 0 || num_files <= 0) {
        perror("Failed to receive num_files");
        close(client_sock);
        continue;
    }

    // Looping over each requested path
    for (int i = 0; i < num_files; i++) {
        char file_path[512] = {0};
        if (recv(client_sock, file_path, sizeof(file_path), 0) <= 0) {
            perror("Failed to receive file path");
            break;
        }
        printf("Download request received from S1 for: %s\n", file_path);
        handle_download(client_sock, file_path);
    }

    close(client_sock);
    continue;
}


        // Checking if this is a remove request

        if (strcmp(cmd, "REMOVE") == 0) {
            int num_files = 0;
recv(client_sock, &num_files, sizeof(int), 0);

for (int i = 0; i < num_files; i++) {
    char file_path[512] = {0};
    recv(client_sock, file_path, sizeof(file_path), 0);
    printf("Remove request received for: %s\n", file_path);
    handle_remove(client_sock, file_path);
}

            close(client_sock);
            continue;
        }

        if (strcmp(cmd, "TARFETCH") == 0) {
            char filetype[10] = {0};
            recv(client_sock, filetype, sizeof(filetype), 0);
            
            if (strcmp(filetype, ".pdf") == 0) {
                printf("Tar request received for PDF files\n");
                handle_tarfetch(client_sock);
            } else {
                // because only PDF files are supported on S2
                printf("Unsupported file type for tar request: %s\n", filetype);
                int error = -1;
                send(client_sock, &error, sizeof(int), 0);
            }
            
            close(client_sock);
            continue;
        }

        // Checking if this is a list files request
        else if (strcmp(cmd, "LISTFILES") == 0) {
            char dir_path[512];
            recv(client_sock, dir_path, sizeof(dir_path), 0);
            
            printf("Directory listing request received for: %s\n", dir_path);
            
            // Handling the list files request
            handle_list_files(client_sock, dir_path);
            close(client_sock);
            continue;
        }

    // Upload
    if (strcmp(cmd, "UPLOAD") == 0) {
       int num_files = 0;
       recv(client_sock, &num_files, sizeof(int), 0);

       for (int i = 0; i < num_files; i++) {
           char filename[256] = {0}, dest_path[256] = {0};
           int file_size = 0;

           recv(client_sock, filename, sizeof(filename), 0);
           recv(client_sock, dest_path, sizeof(dest_path), 0);
           recv(client_sock, &file_size, sizeof(int), 0);

           printf("Upload request received for: %s (%d bytes) to %s\n", filename, file_size, dest_path);

           char *file_data = malloc(file_size);
           if (!file_data) {
             perror("Memory allocation failed");
             break;
            }

          int received = 0;
          while (received < file_size) {
         int r = recv(client_sock, file_data + received, file_size - received, 0);
         if (r <= 0) break;
         received += r;
         }

         save_file(filename, file_data, file_size, dest_path);
         free(file_data);
        }

        
close(client_sock);
continue;


    }
    }

    return 0;
}
