#pragma once
#include <optional>
#include <string>
#include <QString>

struct ApplicationArguments
{
    enum class NetworkMode
    {
        Client,
        Server,
        AutoPeer,
        Disabled
    };

    std::optional<std::string> rom_path;
    std::optional<std::string> save_path;
    std::optional<bool> server_mode;
    bool no_menu = false;
    std::optional<uint32_t> test_auto_close_ms;
    std::optional<std::string> host;
    std::optional<uint16_t> port;
    std::optional<NetworkMode> network_mode;
    std::string peer_id;
};
