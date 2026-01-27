/**
 * @file src/subprocess/ipc_protocol.h
 * @brief IPC protocol definitions for communication between main process (control plane)
 *        and subprocess (data plane) in the separated streaming architecture.
 *
 * This implements the architecture described in the design document:
 * - Main Process (SYSTEM - Control Plane): RTSP handshake, authentication, control commands
 * - Subprocess (User - Data Plane): Capture, encode, send via RTP/UDP
 */
#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
#endif

namespace subprocess {
namespace ipc {

  /**
   * @brief Protocol version for IPC communication.
   * Increment this when making breaking changes to the protocol.
   */
  constexpr uint32_t PROTOCOL_VERSION = 1;

  /**
   * @brief Maximum size of IPC messages.
   * Control commands are small (few bytes), so this is more than enough.
   */
  constexpr size_t MAX_MESSAGE_SIZE = 4096;

  /**
   * @brief Named pipe name prefix for IPC communication.
   */
#ifdef _WIN32
  constexpr const char* PIPE_NAME_PREFIX = "\\\\.\\pipe\\sunshine_stream_";
#else
  constexpr const char* PIPE_NAME_PREFIX = "/tmp/sunshine_stream_";
#endif

  /**
   * @brief IPC command types sent from main process to subprocess.
   * These are lightweight control commands, not data.
   */
  enum class command_type_e : uint8_t {
    // Session lifecycle
    INIT = 0x01,           // Initialize streaming session with configuration
    START = 0x02,          // Start capturing and streaming
    STOP = 0x03,           // Stop streaming and cleanup
    SHUTDOWN = 0x04,       // Shutdown subprocess completely

    // Encoder control (back-channel from client feedback)
    REQUEST_IDR = 0x10,           // Request an IDR frame
    INVALIDATE_REF_FRAMES = 0x11, // Invalidate reference frames
    CHANGE_BITRATE = 0x12,        // Dynamic bitrate change
    SET_QP = 0x13,                // Set quantization parameter
    SET_FEC_PERCENTAGE = 0x14,    // Adjust FEC percentage

    // Socket handover
    SOCKET_INFO = 0x20,    // Pass socket information for RTP sending

    // Heartbeat
    PING = 0xF0,           // Heartbeat ping
    PONG = 0xF1,           // Heartbeat response
  };

  /**
   * @brief Response types from subprocess to main process.
   */
  enum class response_type_e : uint8_t {
    OK = 0x00,             // Command executed successfully
    ERROR = 0x01,          // Command failed
    STATUS = 0x02,         // Status update

    // Events
    FRAME_SENT = 0x10,     // Frame was sent (optional, for stats)
    ENCODER_ERROR = 0x11,  // Encoder encountered an error
    CAPTURE_ERROR = 0x12,  // Capture encountered an error
  };

  /**
   * @brief IPC message header.
   * All messages start with this header.
   */
#pragma pack(push, 1)
  struct message_header_t {
    uint32_t magic;        // Magic number for validation: 0x53554E53 ('SUNS')
    uint32_t version;      // Protocol version
    uint16_t type;         // command_type_e or response_type_e
    uint16_t flags;        // Reserved for future use
    uint32_t payload_size; // Size of payload following this header
    uint32_t sequence;     // Message sequence number for correlation
  };

  static constexpr uint32_t MESSAGE_MAGIC = 0x53554E53; // 'SUNS' in ASCII

  /**
   * @brief Initialization command payload.
   * Contains all information needed to start a streaming session.
   */
  struct init_payload_t {
    // Video configuration
    int32_t width;
    int32_t height;
    int32_t framerate;
    int32_t framerate_num;      // For fractional framerates (NTSC)
    int32_t framerate_den;
    int32_t bitrate;            // Kbps
    int32_t slices_per_frame;
    int32_t num_ref_frames;
    int32_t video_format;       // 0=H.264, 1=HEVC, 2=AV1
    int32_t dynamic_range;      // 0=SDR, 1=HDR
    int32_t chroma_sampling;    // 0=4:2:0, 1=4:4:4
    int32_t encoder_csc_mode;

    // Audio configuration
    int32_t audio_channels;
    int32_t audio_sample_rate;
    int32_t audio_bitrate;

    // Network configuration
    int32_t packet_size;
    int32_t fec_percentage;
    int32_t min_fec_packets;

    // Encryption
    uint32_t encryption_flags;
    uint8_t gcm_key[16];        // AES-128 key
    uint8_t iv[16];             // Initial IV

    // Display to capture (null-terminated, max 256 chars)
    char display_name[256];

    // Client identification
    char client_name[64];
    uint32_t session_id;
  };

  /**
   * @brief Socket information for RTP sending.
   * On Windows, this includes WSADuplicateSocket information.
   */
  struct socket_info_t {
    int32_t socket_type;        // 0=video, 1=audio
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t remote_addr[16];    // IPv6 address (or IPv4-mapped)
    uint8_t local_addr[16];     // Local interface address
    int32_t address_family;     // AF_INET or AF_INET6

#ifdef _WIN32
    // WSAPROTOCOL_INFO for socket duplication
    uint8_t protocol_info[sizeof(WSAPROTOCOL_INFOW)];
#else
    // Unix: file descriptor is passed via SCM_RIGHTS
    int32_t reserved;
#endif
  };

  /**
   * @brief Bitrate change command payload.
   */
  struct bitrate_change_t {
    int32_t new_bitrate_kbps;
    int32_t max_bitrate_kbps;   // Optional maximum
  };

  /**
   * @brief Reference frame invalidation payload.
   */
  struct invalidate_ref_frames_t {
    int64_t first_frame;
    int64_t last_frame;
  };

  /**
   * @brief Status/error response payload.
   */
  struct status_payload_t {
    int32_t error_code;
    char message[256];
  };

#pragma pack(pop)

  /**
   * @brief Utility function to create an IPC message.
   * @param type The command or response type.
   * @param sequence The sequence number for correlation.
   * @return A message header initialized with the given parameters.
   */
  inline message_header_t
  make_header(uint16_t type, uint32_t sequence = 0, uint32_t payload_size = 0) {
    message_header_t header {};
    header.magic = MESSAGE_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.type = type;
    header.flags = 0;
    header.payload_size = payload_size;
    header.sequence = sequence;
    return header;
  }

  /**
   * @brief Validate a message header.
   * @param header The header to validate.
   * @return true if the header is valid, false otherwise.
   */
  inline bool
  validate_header(const message_header_t& header) {
    return header.magic == MESSAGE_MAGIC &&
           header.version == PROTOCOL_VERSION &&
           header.payload_size <= MAX_MESSAGE_SIZE;
  }

}  // namespace ipc
}  // namespace subprocess
