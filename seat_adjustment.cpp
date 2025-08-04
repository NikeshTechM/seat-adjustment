#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <thread>

using json = nlohmann::json;

#define BUFFER_SIZE 2048
#define NXP_PORT 44821
#define NXP_IP "192.168.1.102"

// Send seat data to NXP
void send_to_nxp(const char* json_str) {
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed (NXP)");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NXP_PORT);
    inet_pton(AF_INET, NXP_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to NXP failed");
        close(sock);
        return;
    }

    send(sock, json_str, strlen(json_str), 0);
    close(sock);

    printf("Sent to NXP: %s", json_str);
}

// Adjust the seat step by step and send updates to Android and NXP
void adjust_seat(const std::string& name, json current, json target, int android_socket) {
    int send_count = 0;

    while (current != target) {
        for (const auto& key : {"Headrest", "Back", "Height", "HPos"}) {
            if (!current.contains(key) || !target.contains(key)) continue;

            int current_val = current[key].get<int>();
            int target_val = target[key].get<int>();

            if (current_val < target_val)
                current_val += 1;
            else if (current_val > target_val)
                current_val -= 1;

            current[key] = current_val;
        }

        // Construct JSON message
        json msg;
        if (send_count == 10) {
            msg["SeatType"] = "Error";  // Inject error
        } else {
            msg["SeatType"] = name;
        }

        msg["Seat"] = current;
        msg["TargetSeat"] = target;
        std::string json_str = msg.dump() + "\n";

        // Send to Android
        send(android_socket, json_str.c_str(), json_str.size(), 0);
        printf("Sent to Android: %s", json_str.c_str());

        // Send to NXP in parallel
        std::thread nxp_thread(send_to_nxp, json_str.c_str());
        nxp_thread.detach();

        send_count++;

        // Stop after sending the error message once
        if (send_count == 11) {
            printf("error .\n");
            break;
        }

        sleep(1);  // Delay between steps
    }
}


int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE];
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(60001);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Waiting for Android connection on port 60001...\n");

    while (1) {
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
        printf("received: %s\n", buffer);

        try {
            auto received_json = json::parse(buffer);

            if (!received_json.contains("SeatType") ||
                !received_json.contains("CurrentSeat") ||
                !received_json.contains("TargetSeat")) {
                fprintf(stderr, "Invalid JSON format (missing keys)\n");
                close(new_socket);
                continue;
            }

            std::string name = received_json["SeatType"];
            json current = received_json["CurrentSeat"];
            json target = received_json["TargetSeat"];

            adjust_seat(name, current, target, new_socket);

        } catch (std::exception& e) {
            fprintf(stderr, "JSON Parse Error: %s\n", e.what());
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}

