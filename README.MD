# TSAM Group 37 Assignment 5

This project consists of two primary components: a server (`new_server.cpp`) and a client (`client.cpp`).

## Compilation and Execution

To compile and run the program, follow these steps:

1. Navigate to the project directory.
2. Use the provided `Makefile` to compile the sources:
   ```bash
   make all
   ```
3. After successful compilation, you can run the server using:
    ```bash
    ./tsamgroup37 <Port>

    Example: ./tsamgroup37 4002
    ```
    And the client using:
    ```bash
    ./client <IP of server> <Port>
    
    Example: ./client 127.0.0.1 4002
    ```

## Cleaning up
To remove the compiled binaries, use the clean target of the Makefile:
```bash
make clean
```

## Notes
The program was compiled and tested on Fedora 38.
