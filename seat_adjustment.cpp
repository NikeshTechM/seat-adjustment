#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <iostream>
#include <fstream>

using ordered_json = nlohmann::ordered_json;

/* ================================
   CONSTANTS (UNCHANGED)
================================ */
#define BUFFER_SIZE 2048
#define ANDROID_PORT 60001
#define NXP_PORT 44821
#define NXP_IP "192.168.1.102"

/* ================================
   GLOBAL FLAGS
================================ */
bool IS_TEST_MODE = false;

/* ================================
   NXP COMMUNICATION (RUNTIME ONLY)
================================ */
void send_to_nxp(const std::string& json_str) {
    if (IS_TEST_MODE) return;   // 🚫 No hardware in CI

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(NXP_PORT);
    inet_pton(AF_INET, NXP_IP, &server.sin_addr);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0) {
        send(sock, json_str.c_str(), json_str.size(), 0);
    }

    close(sock);
}

/* ================================
   PURE SEAT ADJUSTMENT LOGIC
   (USED BY RUNTIME + TEST)
================================ */
ordered_json simulate_adjustment(
    ordered_json current,
    const ordered_json& target
) {
    const std::vector<std::string> order = {
        "Back", "HPos", "VPos", "Headrest"
    };

    for (const auto& key : order) {
        if (!current.contains(key) || !target.contains(key)) continue;

        while (current[key] != target[key]) {
            current[key] = current[key].get<int>() +
                           ((current[key] < target[key]) ? 1 : -1);
        }
    }

    return current;
}

/* ================================
   RUNTIME ADJUSTMENT (SOCKET FLOW)
================================ */
void adjust_seat(const std::string& seat,
                 ordered_json current,
                 ordered_json target,
                 int android_socket) {

    auto build_payload = [&](const std::string& status) {
        ordered_json msg;
        msg["Status"] = status;
        msg["SeatType"] = seat;
        msg["Seat"] = current;
        msg["TargetSeat"] = target;
        return msg.dump() + "\n";
    };

    // ---------- START ----------
    std::string start = build_payload("Start");
    send(android_socket, start.c_str(), start.size(), 0);
    std::thread([=]{ sleep(1); send_to_nxp(start); }).detach();

    // ---------- ADJUST ----------
    const std::vector<std::string> order = {
        "Back", "HPos", "VPos", "Headrest"
    };

    for (const auto& key : order) {
        if (!current.contains(key) || !target.contains(key)) continue;

        while (current[key] != target[key]) {
            current[key] = current[key].get<int>() +
                           ((current[key] < target[key]) ? 1 : -1);

            std::string prog = build_payload("InProgress");
            send(android_socket, prog.c_str(), prog.size(), 0);
            std::thread([=]{ sleep(1); send_to_nxp(prog); }).detach();

            usleep(50 * 1000);
        }
    }

    // ---------- END ----------
    std::string end = build_payload("End");
    send(android_socket, end.c_str(), end.size(), 0);
    std::thread([=]{ sleep(1); send_to_nxp(end); }).detach();
}

/* ================================
   CI / CODEBUILD TEST MODE
================================ */
int run_test(const std::string& file) {
    std::ifstream f(file);
    if (!f.is_open()) {
        std::cerr << "❌ Cannot open test file\n";
        return 1;
    }

    ordered_json j;
    f >> j;

    auto payload = j["TelemetryPayload"];
    auto current = payload["CurrentSeat"];
    auto target  = payload["TargetSeat"];

    // 🔹 Compute real result
    auto actual = simulate_adjustment(current, target);

    bool pass = true;

    for (auto& [key, value] : target.items()) {
        if (!actual.contains(key) || actual[key] != value) {
            std::cerr << "❌ Mismatch on " << key
                      << " | Expected=" << value
                      << " Actual=" << actual[key] << "\n";
            pass = false;
        }
    }

    if (pass) {
        std::cout << "✅ TEST PASSED\n";
        return 0;
    }

    std::cerr << "❌ TEST FAILED\n";
    return 1;
}

/* ================================
   ANDROID SERVER MODE
================================ */
int run_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return 1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
               &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ANDROID_PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return 1;
    if (listen(server_fd, 3) < 0) return 1;

    std::cout << "🟢 Waiting for Android on port 60001...\n";

    while (true) {
        int sock = accept(server_fd, nullptr, nullptr);
        if (sock < 0) continue;

        char buffer[BUFFER_SIZE]{};
        ssize_t r = read(sock, buffer, BUFFER_SIZE - 1);
        if (r <= 0) {
            close(sock);
            continue;
        }

        try {
            auto j = ordered_json::parse(buffer);

            if (!j.contains("SeatType") ||
                !j.contains("CurrentSeat") ||
                !j.contains("TargetSeat")) {
                close(sock);
                continue;
            }

            adjust_seat(
                j["SeatType"],
                j["CurrentSeat"],
                j["TargetSeat"],
                sock
            );
        } catch (const std::exception& e) {
            std::cerr << "JSON error: " << e.what() << "\n";
        }

        close(sock);
    }
}

/* ================================
   MAIN
================================ */
int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--test") {
        IS_TEST_MODE = true;
        return run_test(argv[2]);
    }

    IS_TEST_MODE = false;
    return run_server();
}
