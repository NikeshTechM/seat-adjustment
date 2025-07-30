#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define BUFFER_SIZE 2048
#define NXP_PORT 44821
#define NXP_IP "192.168.1.230"
#define ANDROID_PORT 60001

// Create and connect socket to NXP
int connect_to_nxp_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed (NXP)");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NXP_PORT);
    inet_pton(AF_INET, NXP_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to NXP failed");
        close(sock);
        return -1;
    }

    return sock;
}

// Adjust seat and send to Android and NXP
void adjust_seat(const std::string& name, json current, json target, int android_socket, int nxp_socket) {
    const char* keys[] = {"Headrest", "Back", "Height", "HPos"};

    for (const char* key : keys) {
        if (!current.contains(key) || !target.contains(key)) continue;

        int current_val = current[key];
        int target_val = target[key];

        while (current_val != target_val) {
            current_val += (current_val < target_val) ? 1 : -1;
            current[key] = current_val;

            json msg;
            msg["Name"] = name;
            msg["Seat"] = current;

            std::string json_str = msg.dump() + "\n";

            // Send to Android
            send(android_socket, json_str.c_str(), json_str.size(), 0);

            // Send to NXP
            send(nxp_socket, json_str.c_str(), json_str.size(), 0);

            printf("Sent: %s", json_str.c_str());
            sleep(1);  // Delay between steps
        }
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE];
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    //Test log
     fprintf""Starting container-01".\n");

    // Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // Bind to Android port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(ANDROID_PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for Android connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Waiting for Android connection on port %d...\n", ANDROID_PORT);

    while (1) {
        // Accept Android connection
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        memset(buffer, 0, BUFFER_SIZE);
        ssize_t read_bytes = read(new_socket, buffer, BUFFER_SIZE - 1);
        if (read_bytes <= 0) {
            close(new_socket);
            continue;
        }

        buffer[read_bytes] = '\0';
        printf("Received: %s\n", buffer);

        try {
            auto received_json = json::parse(buffer);

            if (!received_json.contains("Name") ||
                !received_json.contains("CurrentSeat") ||
                !received_json.contains("TargetSeat")) {
                fprintf(stderr, "Invalid JSON format (missing keys)\n");
                close(new_socket);
                continue;
            }

            std::string name = received_json["Name"];
            json current = received_json["CurrentSeat"];
            json target = received_json["TargetSeat"];

            // Connect to NXP
            int nxp_socket = connect_to_nxp_socket();
            if (nxp_socket < 0) {
                fprintf(stderr, "Skipping seat adjustment due to NXP connection issue.\n");
                close(new_socket);
                continue;
            }

            // Perform seat adjustment
            adjust_seat(name, current, target, new_socket, nxp_socket);

            // Close NXP socket after adjustment
            close(nxp_socket);

        } catch (std::exception& e) {
            fprintf(stderr, "JSON Parse Error: %s\n", e.what());
        }

        close(new_socket);  // Close Android socket
    }

    close(server_fd);
    return 0;
}
