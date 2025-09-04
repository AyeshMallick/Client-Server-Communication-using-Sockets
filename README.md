# Client-Server-Communication-using-Sockets
A C-based multi-server file management system using sockets. Client interacts only with S1, which stores `.c` files locally and routes `.pdf`, `.txt`, and `.zip` files to S2, S3, and S4. Supports upload, download, remove, and tarball fetch commands through a simple CLI.

# Multi-Server File Management System

This project implements a client-server architecture in C using sockets.  
It simulates a distributed file system where a single client communicates only with **Server S1**, which acts as a coordinator and router for other servers:

- **S1**: Main server. Stores `.c` files locally, forwards other requests.
- **S2**: Handles `.pdf` files.
- **S3**: Handles `.txt` files.
- **S4**: Handles `.zip` files.

The client provides a **CLI interface** supporting file upload, download, removal, and tarball fetching (`downltar`). The routing is transparent to the user—S1 hides the fact that multiple servers are involved.

---

## Features
- Upload up to 3 files at once to a given path.
- Download 1–2 files simultaneously from S1.
- Remove 1–2 files (local or remote).
- Create and fetch tar archives of `.c`, `.pdf`, or `.txt` files.
- Centralized client connection: user only connects to S1.

---

## Commands
- `uploadf file1 file2 ... dest_path` – Upload files to destination.  
- `downlf file1 [file2]` – Download one or two files.  
- `removef file1 [file2]` – Remove one or two files.  
- `downltar .ext` – Fetch a tar archive of `.c`, `.pdf`, or `.txt` files.

---

## Architecture
Client ↔ S1 (router + .c storage)
↳ S2 (.pdf)
↳ S3 (.txt)
↳ S4 (.zip)


---

## How to Run
1. Compile each server and client:
   ```bash
   gcc -o Server1 Server1.c 
   gcc -o Server2 Server2.c 
   gcc -o Server3 Server3.c 
   gcc -o Server4 Server4.c 
   gcc -o Client Client.c
   
2. Start servers in separate terminals:
```bash
./Server1
./Server2
./Server3
./Server4


```
3.Run the client
```bash
./Client

```
4. Requirements

-- Linux/Unix environment

-- GCC compiler

-- Basic Linux/UNIX knowledge




