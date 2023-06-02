// Display images inside a terminal
// Copyright (C) 2023  JustKidding
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "sway.hpp"
#include "os.hpp"

#include <string>
#include <iostream>
#include <array>
#include <fmt/format.h>
#include <stack>
#include <thread>

using njson = nlohmann::json;

constexpr auto ipc_magic = std::string_view{"i3-ipc"};
constexpr auto ipc_header_size = ipc_magic.size() + 8;

SwaySocket::SwaySocket()
{
    const auto sway_sock = os::getenv("SWAYSOCK");
    if (!sway_sock.has_value()) {
        throw std::runtime_error("SWAY NOT SUPPORTED");
    }
    socket_path = sway_sock.value();
    logger = spdlog::get("wayland");
    socket = std::make_unique<UnixSocket>(socket_path);
    logger->info("Using sway socket {}", socket_path);
}

struct __attribute__((packed)) ipc_header {
    std::array<char, ipc_magic.size()> magic;
    uint32_t len;
    uint32_t type;
};

auto SwaySocket::get_window_info() -> struct WaylandWindow
{
    const auto window = current_window();
    const auto& rect = window["rect"];
    return {
        .width = rect["width"],
        .height = rect["height"],
        .x = rect["x"],
        .y = rect["y"]
    };
}

void SwaySocket::initial_setup(const std::string_view appid)
{
    disable_focus(appid);
    enable_floating(appid);
}

void SwaySocket::disable_focus(const std::string_view appid)
{
    std::ignore = ipc_command(fmt::format(R"(no_focus [app_id="{}"])", appid));
}

void SwaySocket::enable_floating(const std::string_view appid)
{
    std::ignore = ipc_command(appid, "floating enable");
}

void SwaySocket::move_window(const std::string_view appid, int xcoord, int ycoord)
{
    std::ignore = ipc_command(appid, fmt::format("move absolute position {} {}", xcoord, ycoord));
}

auto SwaySocket::ipc_message(ipc_message_type type, const std::string_view payload) const -> nlohmann::json
{
    struct ipc_header header;
    header.len = payload.size();
    header.type = type;
    ipc_magic.copy(header.magic.data(), ipc_magic.size());

    if (!payload.empty()) {
        logger->debug("Running socket command {}", payload);
    }
    socket->write(reinterpret_cast<const void*>(&header), ipc_header_size);
    socket->write(reinterpret_cast<const void*>(payload.data()), payload.size());

    socket->read(reinterpret_cast<void*>(&header), ipc_header_size);
    std::string buff (header.len, 0);
    socket->read(reinterpret_cast<void*>(buff.data()), buff.size());
    return njson::parse(buff);
}

auto SwaySocket::current_window() const -> nlohmann::json
{
    logger->debug("Obtaining sway tree");
    auto tree = ipc_message(IPC_GET_TREE);
    std::stack<njson> nodes_st;

    nodes_st.push(tree);

    while (!nodes_st.empty()) {
        auto top = nodes_st.top();
        nodes_st.pop();
        bool focused = top["focused"];
        if (focused) {
            return top;
        }
        for (auto& node: top["nodes"]) {
            nodes_st.push(node);
        }
        for (auto& node: top["floating_nodes"]) {
            nodes_st.push(node);
        }
    }
    return nullptr;
}

auto SwaySocket::ipc_command(std::string_view appid, std::string_view command) const -> nlohmann::json
{
    const auto cmd = fmt::format(R"(for_window [app_id="{}"] {})", appid, command);
    return ipc_message(IPC_COMMAND, cmd);
}

auto SwaySocket::ipc_command(std::string_view command) const -> nlohmann::json
{
    return ipc_message(IPC_COMMAND, command);
}
