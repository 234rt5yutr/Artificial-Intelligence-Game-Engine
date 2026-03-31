#pragma once

#include <cstdint>

namespace Core {
namespace Network {

    //==========================================================================
    // PROTOCOL CONSTANTS
    //==========================================================================
    
    constexpr uint32_t NETWORK_PROTOCOL_VERSION = 1;
    constexpr uint32_t MAX_ENTITIES_PER_SNAPSHOT = 64;
    constexpr uint32_t MAX_INPUTS_PER_PACKET = 32;
    constexpr uint32_t MAX_EVENTS_PER_PACKET = 16;

    //==========================================================================
    // PACKET TYPE ENUMERATION
    //==========================================================================

    // All packet types in the protocol
    enum class PacketType : uint8_t {
        // Handshake packets (1-9)
        ClientHello = 1,
        ServerWelcome = 2,
        ClientReady = 3,
        ServerReady = 4,
        Rejected = 5,

        // Game state packets (10-29)
        WorldSnapshot = 10,         // Server -> Client: Full world state
        DeltaSnapshot = 11,         // Server -> Client: Delta-compressed state update
        EntitySpawn = 12,           // Server -> Client: New entity spawned
        EntityDestroy = 13,         // Server -> Client: Entity destroyed
        ComponentUpdate = 14,       // Server -> Client: Single component update

        // Input packets (30-49)
        ClientInput = 30,           // Client -> Server: Player input commands
        InputAck = 31,              // Server -> Client: Input acknowledgment

        // Replication packets (50-69)
        TransformSync = 50,         // Bidirectional: Transform synchronization
        PhysicsSync = 51,           // Server -> Client: Physics state sync
        AnimationSync = 52,         // Server -> Client: Animation state sync

        // RPC packets (70-89)
        RemoteCall = 70,            // Bidirectional: Remote procedure call
        RemoteCallResponse = 71,    // Response to RPC

        // Event packets (90-109)
        GameEvent = 90,             // Server -> Client: Game event notification
        ChatMessage = 91,           // Bidirectional: Chat message

        // Connection management (110-119)
        Ping = 110,                 // Bidirectional: Latency measurement
        Pong = 111,                 // Response to ping
        Disconnect = 112,           // Graceful disconnect notification
        Heartbeat = 113,            // Keep-alive signal

        // Debug/Admin packets (120-129)
        DebugInfo = 120,            // Server -> Client: Debug information
        AdminCommand = 121,         // Client -> Server: Admin command

        // Reserved for future use
        Custom = 200                // Custom packet type (payload defines format)
    };

    //==========================================================================
    // HANDSHAKE PACKETS (already defined, kept for completeness)
    //==========================================================================

    // Handshake packet types (legacy enum for backwards compatibility)
    enum class HandshakePacketType : uint8_t {
        ClientHello = 1,
        ServerWelcome = 2,
        ClientReady = 3,
        ServerReady = 4,
        Rejected = 255
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

    #pragma pack(push, 1)

    // Client hello packet sent during handshake
    struct ClientHelloPacket {
        HandshakePacketType Type = HandshakePacketType::ClientHello;
        uint32_t ProtocolVersion = NETWORK_PROTOCOL_VERSION;
        char ClientName[64] = {};
        uint8_t Reserved[27] = {};
    };
    static_assert(sizeof(ClientHelloPacket) == 96, "ClientHelloPacket must be 96 bytes");

    // Server welcome packet received during handshake
    struct ServerWelcomePacket {
        HandshakePacketType Type = HandshakePacketType::ServerWelcome;
        uint32_t AssignedClientId = 0;
        uint32_t ServerTickRate = 60;
        char ServerName[64] = {};
        char Message[64] = {};
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
        uint64_t ServerTimestamp = 0;
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

    //==========================================================================
    // COMMON DATA STRUCTURES
    //==========================================================================

    // Network-serializable vector (3 floats)
    struct NetVec3 {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
    };
    static_assert(sizeof(NetVec3) == 12, "NetVec3 must be 12 bytes");

    // Network-serializable quaternion (4 floats)
    struct NetQuat {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float W = 1.0f;
    };
    static_assert(sizeof(NetQuat) == 16, "NetQuat must be 16 bytes");

    // Compressed quaternion (smallest three representation)
    struct NetQuatCompressed {
        int16_t A = 0;      // Smallest component 1 (scaled by 32767)
        int16_t B = 0;      // Smallest component 2
        int16_t C = 0;      // Smallest component 3
        uint8_t LargestIndex = 3;  // Which component was largest (0-3)
        uint8_t Padding = 0;
    };
    static_assert(sizeof(NetQuatCompressed) == 8, "NetQuatCompressed must be 8 bytes");

    // Network entity identifier
    struct NetEntityId {
        uint32_t Id = 0;            // Unique entity ID
        uint16_t Generation = 0;    // Generation counter for reuse detection
        uint16_t TypeId = 0;        // Entity type/archetype identifier
    };
    static_assert(sizeof(NetEntityId) == 8, "NetEntityId must be 8 bytes");

    //==========================================================================
    // PACKET HEADER
    //==========================================================================

    // Common header for all packets
    struct PacketHeader {
        PacketType Type = PacketType::Custom;
        uint8_t Flags = 0;              // Packet flags (reliable, ordered, etc.)
        uint16_t PayloadSize = 0;       // Size of payload following header
        uint32_t SequenceNumber = 0;    // Packet sequence for ordering/ack
        uint64_t Timestamp = 0;         // Sender timestamp (microseconds)
    };
    static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be 16 bytes");

    // Packet flags
    enum class PacketFlags : uint8_t {
        None = 0,
        Reliable = 1 << 0,      // Packet must be acknowledged
        Ordered = 1 << 1,       // Packet must be processed in order
        Compressed = 1 << 2,    // Payload is compressed
        Encrypted = 1 << 3,     // Payload is encrypted
        Priority = 1 << 4       // High priority packet
    };

    //==========================================================================
    // TRANSFORM SYNCHRONIZATION
    //==========================================================================

    // Full transform data for an entity
    struct NetTransform {
        NetEntityId EntityId;
        NetVec3 Position;
        NetQuat Rotation;
        NetVec3 Scale;
        NetVec3 Velocity;       // Linear velocity for interpolation
        NetVec3 AngularVelocity; // Angular velocity for interpolation
    };
    static_assert(sizeof(NetTransform) == 72, "NetTransform must be 72 bytes");

    // Compressed transform (position + compressed rotation, no scale/velocity)
    struct NetTransformCompressed {
        NetEntityId EntityId;
        NetVec3 Position;
        NetQuatCompressed Rotation;
    };
    static_assert(sizeof(NetTransformCompressed) == 28, "NetTransformCompressed must be 28 bytes");

    // Transform sync packet
    struct TransformSyncPacket {
        PacketHeader Header;
        uint32_t ServerTick = 0;        // Server tick this transform is from
        uint8_t TransformCount = 0;     // Number of transforms in packet
        uint8_t Padding[3] = {};
        // Followed by TransformCount * NetTransform or NetTransformCompressed
    };
    static_assert(sizeof(TransformSyncPacket) == 24, "TransformSyncPacket must be 24 bytes");

    //==========================================================================
    // INPUT PACKETS
    //==========================================================================

    // Input button state flags
    enum class InputButtons : uint32_t {
        None = 0,
        MoveForward = 1 << 0,
        MoveBackward = 1 << 1,
        MoveLeft = 1 << 2,
        MoveRight = 1 << 3,
        Jump = 1 << 4,
        Crouch = 1 << 5,
        Sprint = 1 << 6,
        PrimaryAction = 1 << 7,     // Fire / Attack
        SecondaryAction = 1 << 8,   // Aim / Block
        Interact = 1 << 9,
        Reload = 1 << 10,
        Use = 1 << 11,
        Inventory = 1 << 12,
        Menu = 1 << 13
    };

    // Single input sample
    struct InputSample {
        uint32_t InputSequence = 0;     // Client-assigned input sequence number
        uint32_t Buttons = 0;           // Button state (InputButtons flags)
        float MoveX = 0.0f;             // Movement axis X (-1 to 1)
        float MoveY = 0.0f;             // Movement axis Y (-1 to 1)
        float LookYaw = 0.0f;           // Look yaw angle (radians)
        float LookPitch = 0.0f;         // Look pitch angle (radians)
        uint16_t DeltaTimeMs = 0;       // Delta time since last input (ms)
        uint8_t Padding[2] = {};
    };
    static_assert(sizeof(InputSample) == 28, "InputSample must be 28 bytes");

    // Client input packet (can contain multiple samples for redundancy)
    struct ClientInputPacket {
        PacketHeader Header;
        uint32_t ClientId = 0;          // Client sending the input
        uint32_t LastAckedSequence = 0; // Last input sequence acknowledged by server
        uint8_t InputCount = 0;         // Number of input samples
        uint8_t Padding[3] = {};
        // Followed by InputCount * InputSample
    };
    static_assert(sizeof(ClientInputPacket) == 28, "ClientInputPacket must be 28 bytes");

    // Server acknowledgment of processed input
    struct InputAckPacket {
        PacketHeader Header;
        uint32_t ClientId = 0;
        uint32_t AckedInputSequence = 0;    // Highest processed input sequence
        uint32_t ServerTick = 0;            // Server tick when processed
        NetVec3 ResultPosition;             // Authoritative position after input
        NetQuat ResultRotation;             // Authoritative rotation after input
    };
    static_assert(sizeof(InputAckPacket) == 56, "InputAckPacket must be 56 bytes");

    //==========================================================================
    // ENTITY SPAWN/DESTROY
    //==========================================================================

    // Entity spawn notification
    struct EntitySpawnPacket {
        PacketHeader Header;
        NetEntityId EntityId;
        uint32_t OwnerClientId = 0;     // Owning client (0 = server-owned)
        uint32_t PrefabId = 0;          // Prefab/archetype to instantiate
        NetVec3 Position;
        NetQuat Rotation;
        NetVec3 Scale;
        uint8_t Flags = 0;              // Spawn flags
        uint8_t Padding[3] = {};
    };
    static_assert(sizeof(EntitySpawnPacket) == 76, "EntitySpawnPacket must be 76 bytes");

    // Entity destroy notification
    struct EntityDestroyPacket {
        PacketHeader Header;
        NetEntityId EntityId;
        uint32_t Reason = 0;            // Destruction reason code
        uint8_t Padding[4] = {};
    };
    static_assert(sizeof(EntityDestroyPacket) == 32, "EntityDestroyPacket must be 32 bytes");

    //==========================================================================
    // WORLD SNAPSHOT
    //==========================================================================

    // Full world snapshot header
    struct WorldSnapshotPacket {
        PacketHeader Header;
        uint32_t ServerTick = 0;
        uint32_t SnapshotId = 0;        // Unique snapshot identifier
        uint32_t EntityCount = 0;       // Total entities in snapshot
        uint32_t TotalSize = 0;         // Total bytes in multi-part snapshot
        uint16_t PartIndex = 0;         // Part index (0-based)
        uint16_t TotalParts = 0;        // Total parts in snapshot
        // Followed by entity data
    };
    static_assert(sizeof(WorldSnapshotPacket) == 36, "WorldSnapshotPacket must be 36 bytes");

    // Delta snapshot (changes since last acknowledged snapshot)
    struct DeltaSnapshotPacket {
        PacketHeader Header;
        uint32_t ServerTick = 0;
        uint32_t BaseSnapshotId = 0;    // Snapshot this delta is based on
        uint32_t SnapshotId = 0;        // New snapshot ID
        uint16_t SpawnedCount = 0;      // Entities spawned since base
        uint16_t DestroyedCount = 0;    // Entities destroyed since base
        uint16_t UpdatedCount = 0;      // Entities with changed transforms
        uint8_t Padding[2] = {};
        // Followed by:
        // - SpawnedCount * NetEntityId (spawned entities)
        // - DestroyedCount * NetEntityId (destroyed entities)
        // - UpdatedCount * NetTransformCompressed (updated transforms)
    };
    static_assert(sizeof(DeltaSnapshotPacket) == 36, "DeltaSnapshotPacket must be 36 bytes");

    //==========================================================================
    // PHYSICS SYNCHRONIZATION
    //==========================================================================

    // Physics state for a rigid body
    struct NetPhysicsState {
        NetEntityId EntityId;
        NetVec3 LinearVelocity;
        NetVec3 AngularVelocity;
        uint8_t IsAwake = 1;            // Is the body awake/active
        uint8_t Padding[3] = {};
    };
    static_assert(sizeof(NetPhysicsState) == 36, "NetPhysicsState must be 36 bytes");

    // Physics sync packet
    struct PhysicsSyncPacket {
        PacketHeader Header;
        uint32_t ServerTick = 0;
        uint8_t BodyCount = 0;
        uint8_t Padding[3] = {};
        // Followed by BodyCount * NetPhysicsState
    };
    static_assert(sizeof(PhysicsSyncPacket) == 24, "PhysicsSyncPacket must be 24 bytes");

    //==========================================================================
    // GAME EVENTS
    //==========================================================================

    // Event types
    enum class GameEventType : uint16_t {
        None = 0,
        PlayerJoined = 1,
        PlayerLeft = 2,
        PlayerDied = 3,
        PlayerRespawned = 4,
        DamageDealt = 5,
        ItemPickup = 6,
        ItemDrop = 7,
        SoundEffect = 8,
        ParticleEffect = 9,
        ObjectInteraction = 10,
        StateChange = 11,
        Custom = 1000
    };

    // Generic game event
    struct GameEventPacket {
        PacketHeader Header;
        GameEventType EventType = GameEventType::None;
        uint16_t EventDataSize = 0;
        uint32_t SourceEntityId = 0;
        uint32_t TargetEntityId = 0;
        NetVec3 Position;               // Event world position
        uint8_t EventData[32] = {};     // Type-specific event data
    };
    static_assert(sizeof(GameEventPacket) == 72, "GameEventPacket must be 72 bytes");

    //==========================================================================
    // CHAT MESSAGE
    //==========================================================================

    // Chat message types
    enum class ChatChannel : uint8_t {
        Global = 0,
        Team = 1,
        Private = 2,
        System = 3
    };

    struct ChatMessagePacket {
        PacketHeader Header;
        uint32_t SenderId = 0;
        uint32_t TargetId = 0;          // For private messages
        ChatChannel Channel = ChatChannel::Global;
        uint8_t MessageLength = 0;
        uint8_t Padding[2] = {};
        char Message[128] = {};
    };
    static_assert(sizeof(ChatMessagePacket) == 156, "ChatMessagePacket must be 156 bytes");

    //==========================================================================
    // PING/PONG (LATENCY MEASUREMENT)
    //==========================================================================

    struct PingPacket {
        PacketHeader Header;
        uint64_t ClientTimestamp = 0;   // Client send time
        uint32_t PingId = 0;            // Unique ping identifier
        uint8_t Padding[4] = {};
    };
    static_assert(sizeof(PingPacket) == 32, "PingPacket must be 32 bytes");

    struct PongPacket {
        PacketHeader Header;
        uint64_t ClientTimestamp = 0;   // Echoed client send time
        uint64_t ServerTimestamp = 0;   // Server receive time
        uint32_t PingId = 0;            // Echoed ping ID
        uint8_t Padding[4] = {};
    };
    static_assert(sizeof(PongPacket) == 40, "PongPacket must be 40 bytes");

    //==========================================================================
    // HEARTBEAT / KEEPALIVE
    //==========================================================================

    struct HeartbeatPacket {
        PacketHeader Header;
        uint32_t ClientId = 0;
        uint32_t ServerTick = 0;        // Current server tick (if from server)
        uint8_t Padding[8] = {};
    };
    static_assert(sizeof(HeartbeatPacket) == 32, "HeartbeatPacket must be 32 bytes");

    //==========================================================================
    // DISCONNECT
    //==========================================================================

    enum class DisconnectReason : uint8_t {
        Unknown = 0,
        ClientRequest = 1,
        Kicked = 2,
        Banned = 3,
        Timeout = 4,
        ServerShutdown = 5,
        VersionMismatch = 6,
        CheatDetected = 7
    };

    struct DisconnectPacket {
        PacketHeader Header;
        uint32_t ClientId = 0;
        DisconnectReason Reason = DisconnectReason::Unknown;
        uint8_t Padding[3] = {};
        char Message[64] = {};
    };
    static_assert(sizeof(DisconnectPacket) == 88, "DisconnectPacket must be 88 bytes");

    //==========================================================================
    // REMOTE PROCEDURE CALL (RPC)
    //==========================================================================

    struct RemoteCallPacket {
        PacketHeader Header;
        uint32_t CallId = 0;            // Unique call identifier
        uint32_t FunctionHash = 0;      // Hash of function name
        uint32_t TargetEntityId = 0;    // Target entity (0 = global)
        uint16_t PayloadSize = 0;       // Size of RPC arguments
        uint8_t Flags = 0;              // RPC flags (reliable, etc.)
        uint8_t Padding = 0;
        // Followed by PayloadSize bytes of serialized arguments
    };
    static_assert(sizeof(RemoteCallPacket) == 32, "RemoteCallPacket must be 32 bytes");

    struct RemoteCallResponsePacket {
        PacketHeader Header;
        uint32_t CallId = 0;            // Echoed call ID
        uint32_t ResultCode = 0;        // 0 = success, other = error code
        uint16_t PayloadSize = 0;       // Size of return value
        uint8_t Padding[2] = {};
        // Followed by PayloadSize bytes of return value
    };
    static_assert(sizeof(RemoteCallResponsePacket) == 28, "RemoteCallResponsePacket must be 28 bytes");

    #pragma pack(pop)

    //==========================================================================
    // HELPER FUNCTIONS (DECLARATIONS)
    //==========================================================================

    // Convert game Vec3 to network Vec3
    inline NetVec3 ToNetVec3(float x, float y, float z) {
        return NetVec3{ x, y, z };
    }

    // Convert game quaternion to network quaternion
    inline NetQuat ToNetQuat(float x, float y, float z, float w) {
        return NetQuat{ x, y, z, w };
    }

    // Get packet type from raw packet data
    inline PacketType GetPacketType(const void* data) {
        return static_cast<const PacketHeader*>(data)->Type;
    }

    // Get packet size from header
    inline uint32_t GetTotalPacketSize(const PacketHeader& header) {
        return sizeof(PacketHeader) + header.PayloadSize;
    }

} // namespace Network
} // namespace Core
