# Simple TFTP Server in C

This is a simple implementation of a TFTP (Trivial File Transfer Protocol) server written in C. It provides a basic example of how to use the TFTP protocol for file transfer. The server is implemented with standard libraries and utilizes Winsock2 for handling network connections.

## Dependencies
- Winsock2: This is a Windows-specific library used for network programming. Here is a link to its documentation: [Winsock2 Documentation](https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2)

## Code Overview

Here is a brief explanation of some of the most important parts of the code:

- `#define PORT_1 69` and `#define PORT_2 4259`: These lines define the ports on which the server listens for connections.

- `struct stat`: This structure is used to keep track of statistical information about the server's operations, including the number of blocks transferred and the number of errors encountered.

- `enum mode` and `enum opcode`: These enumerations define the transfer modes and operation codes used in the TFTP protocol, respectively.

- `union tftp_message`: This union defines the structure of a TFTP message. It can represent any of the four types of messages used in the TFTP protocol: Read Request (RRQ), Write Request (WRQ), Data, and Acknowledgment (ACK).

- `tftp_send_error()`: This function sends an error message to the client. It constructs an error message with the given error code and string, and sends it to the client using the `sendto()` function from the Winsock2 library. You can find the documentation for `sendto()` here: [sendto() Documentation](https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-sendto)

---

**Russian**

Это простая реализация сервера TFTP (Trivial File Transfer Protocol), написанного на C. Пример использования протокола TFTP для передачи файлов. Сервер реализован с использованием стандартных библиотек и использует Winsock2 для работы с сетевыми подключениями.
