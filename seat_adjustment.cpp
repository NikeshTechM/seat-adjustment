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
#include <vector>

using ordered_json = nlohmann::ordered_json;  // Maintains insertion order
#define BUFFER_SIZE 2048
#define NXP_PORT 44821
#define NXP_IP "192.168.1.102"

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

void adjust_seat(const std::string& name, ordered_json current, ordered_json target, int android_socket) {
    const std::vector<std::string> update_order = {"Back", "HPos", "VPos", "Headrest"};

    // ------------------ START MESSAGE ------------------
    ordered_json start_payload;
    start_payload["Status"] = "Start";
    start_payload["SeatType"] = name;

    ordered_json seat_data, target_data;
    for (const auto& key : update_order) {
        seat_data[key] = current.value(key, 0);
        target_data[key] = target.value(key, 0);
    }

    start_payload["Seat"] = seat_data;
    start_payload["TargetSeat"] = target_data;

    std::string start_str = start_payload.dump() + "\n";
    send(android_socket, start_str.c_str(), start_str.size(), 0);
    printf("Sent to Android: %s", start_str.c_str());
    std::thread(send_to_nxp, start_str.c_str()).detach();

    // ------------------ SEAT ADJUSTMENT ------------------
    for (const std::string& key : update_order) {
        if (!current.contains(key) || !target.contains(key)) continue;


        int current_val = current[key].get<int>();
        int target_val = target[key].get<int>();
        int step = (key == "Back") ? 5 : 1;

        while (current_val != target_val) {
            if (current_val < target_val) {
                current_val += step;
                if (current_val > target_val) current_val = target_val;
            } else {
                current_val -= step;
                if (current_val < target_val) current_val = target_val;
            }

            current[key] = current_val;

            // If fully matched, skip redundant "InProgress"
            bool all_matched = true;
            for (const auto& check_key : update_order) {
                if (current.value(check_key, 0) != target.value(check_key, 0)) {
                    all_matched = false;
                    break;
                }
            }
            if (all_matched) break;

            ordered_json progress_msg;
            progress_msg["Status"] = "InProgress";
            progress_msg["SeatType"] = name;

            ordered_json current_seat, target_seat;
            for (const auto& k : update_order) {
                current_seat[k] = current.value(k, 0);
                target_seat[k] = target.value(k, 0);
            }

            progress_msg["Seat"] = current_seat;
            progress_msg["TargetSeat"] = target_seat;

            std::string json_str = progress_msg.dump() + "\n";

            // Send to Android with 50ms delay
            send(android_socket, json_str.c_str(), json_str.size(), 0);
            printf("Sent to Android: %s", json_str.c_str());
            usleep(50 * 1000);  // 50ms

            // Send to NXP with 1s delay
            std::thread([json_str]() {
                send_to_nxp(json_str.c_str());
                sleep(1);  // 1s delay for NXP only
            }).detach();
        }
    }

    // ------------------ END MESSAGE ------------------
    ordered_json end_payload;
    end_payload["Status"] = "End";
    end_payload["SeatType"] = name;

    ordered_json final_current, final_target;
    for (const auto& key : update_order) {
        final_current[key] = current.value(key, 0);
        final_target[key] = target.value(key, 0);
    }

    end_payload["Seat"] = final_current;
    end_payload["TargetSeat"] = final_target;

    std::string end_str = end_payload.dump() + "\n";
    send(android_socket, end_str.c_str(), end_str.size(), 0);
    printf("Sent to Android: %s", end_str.c_str());
    std::thread(send_to_nxp, end_str.c_str()).detach();
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
        printf("Received: %s\n", buffer);

        try {
            auto received_json = ordered_json::parse(buffer);

            if (!received_json.contains("SeatType") ||
                !received_json.contains("CurrentSeat") ||
                !received_json.contains("TargetSeat")) {
                fprintf(stderr, "Invalid JSON format (missing keys)\n");
                close(new_socket);
                continue;
            }

            std::string name = received_json["SeatType"];
            ordered_json current = received_json["CurrentSeat"];
            ordered_json target = received_json["TargetSeat"];

            adjust_seat(name, current, target, new_socket);

        } catch (std::exception& e) {
            fprintf(stderr, "JSON Parse Error: %s\n", e.what());
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}

