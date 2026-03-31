#pragma once

#include <cstdint>

namespace Core {
namespace Network {

    // Handshake packet types (used internally during connection setup)
    enum class HandshakePacketType : uint8_t {
        ClientHello = 1,        // Client -> Server: Initial hello with client info
        ServerWelcome = 2,      // Server -> Client: Welcome with assigned client ID
        ClientReady = 3,        // Client -> Server: Client ready to receive game state
        ServerReady = 4,        // Server -> Client: Server ready, handshake complete
        Rejected = 255          // Server -> Client: Connection rejected
    };

    // Rejection reason codes
    enum class RejectionReason : uint32_t {
        None = 0,
        ServerFull = 1,
        VersionMismatch = 2,
        Banned = 3,
        InvalidClientInfo = 4,
        Timeout = 5,
        ServerShuttingDown = 6
    };

    // Client hello packet sent during handshake
    #pragma pack(push, 1)
    struct ClientHelloPacket {
        HandshakePacketType Type = HandshakePacketType::ClientHello;
        uint32_t ProtocolVersion = 1;
        char ClientName[64] = {};
        uint8_t Reserved[27] = {};  // Padding for future use
    };
    static_assert(sizeof(ClientHelloPacket) == 96, "ClientHelloPacket must be 96 bytes");

    // Server welcome packet received during handshake
    struct ServerWelcomePacket {
        HandshakePacketType Type = HandshakePacketType::ServerWelcome;
        uint32_t AssignedClientId = 0;
        uint32_t ServerTickRate = 60;   // Server's tick rate in Hz
        char ServerName[64] = {};
        char Message[64] = {};          // Welcome message or rejection reason
        uint8_t Reserved[23] = {};
    };
    static_assert(sizeof(ServerWelcomePacket) == 160, "ServerWelcomePacket must be 160 bytes");

    // Client ready packet (acknowledgment after welcome)
    struct ClientReadyPacket {
        HandshakePacketType Type = HandshakePacketType::ClientReady;
        uint32_t AcknowledgedClientId = 0;
        uint8_t Reserved[91] = {};
    };
    static_assert(sizeof(ClientReadyPacket) == 96, "ClientReadyPacket must be 96 bytes");

    // Server ready packet (final handshake step)
    struct ServerReadyPacket {
        HandshakePacketType Type = HandshakePacketType::ServerReady;
        uint64_t ServerTimestamp = 0;   // Server time for sync
        uint8_t Reserved[87] = {};
    };
    static_assert(sizeof(ServerReadyPacket) == 96, "ServerReadyPacket must be 96 bytes");

    // Rejection packet
    struct RejectionPacket {
        HandshakePacketType Type = HandshakePacketType::Rejected;
        uint32_t ReasonCode = 0;
        char Reason[128] = {};
        uint8_t Reserved[27] = {};
    };
    static_assert(sizeof(RejectionPacket) == 160, "RejectionPacket must be 160 bytes");
    #pragma pack(pop)

} // namespace Network
} // namespace Core
