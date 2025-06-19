#pragma once
#ifndef _SIMPLIDFS_MESSAGE_H
#define _SIMPLIDFS_MESSAGE_H

#include <cstdint> // For fixed-width integer types
#include <sstream> // For std::ostringstream, std::istringstream
#include <stdexcept> // For std::runtime_error, std::invalid_argument, std::out_of_range
#include <string>
#include <vector> // Not strictly needed for these specific implementations, but good for general message handling
#include <openssl/evp.h>

/**
 * @brief Defines the types of messages that can be exchanged within the
 * SimpliDFS system.
 */
enum class MessageType {
  // Client to MetaServer, MetaServer to Node
  CreateFile, ///< Request to create a new file. _Filename required.
  WriteFile,  ///< Request to write content to a file. _Filename and _Content
              ///< required.
  ReadFile,   ///< Request to read content from a file. _Filename required.
  // MetaServer to Node (response may be simpler or part of a different
  // mechanism)
  FileCreated,  ///< Confirmation that a file has been created.
  FileWritten,  ///< Confirmation that content has been written to a file.
  FileRead,     ///< Contains the content of a read file. _Filename and _Content
                ///< used.
  FileRemoved,  ///< Confirmation that a file has been removed. (May be
                ///< deprecated by DeleteFile)
                // Node to MetaServer
  RegisterNode, ///< Request from a Node to register with the MetaServer.
                ///< _Filename (as NodeID), _NodeAddress, _NodePort required.
  Heartbeat,    ///< Heartbeat signal from a Node to MetaServer. _Filename (as
                ///< NodeID) required.
                // MetaServer to Node
  ReplicateFileCommand, ///< Command from MetaServer to a source Node to
                        ///< replicate a file to another Node. _Filename (file
                        ///< to replicate), _NodeAddress (target node's
                        ///< address:port), _Content (source node ID for
                        ///< logging/confirmation by target) required.
  ReceiveFileCommand,   ///< Command from MetaServer to a destination Node to
                        ///< expect a file from another Node. _Filename (file to
  ///< receive), _NodeAddress (source node's address:port),
  ///< _Content (target node ID for logging/confirmation by
  ///< source) required.
  // Client to MetaServer, MetaServer to Node
  DeleteFile, ///< Request to delete a file. _Filename required.

  // New MessageTypes
  GetAttr,
  GetAttrResponse,
  Readdir,
  ReaddirResponse,
  Access,
  AccessResponse,
  Open,
  OpenResponse,
  CreateFileResponse,
  Read,
  ReadResponse,
  Write,
  WriteResponse,
  TruncateFile,
  TruncateFileResponse,
  Unlink,
  UnlinkResponse,
  Rename,
  RenameResponse,
  Utimens,
  UtimensResponse,

  // Operations for Mkdir, Rmdir, StatFs
  Mkdir,
  MkdirResponse,
  Rmdir,
  RmdirResponse,
  StatFs,
  StatFsResponse,

  // For FUSE adapter to request node locations for a file from metaserver
  GetFileNodeLocationsRequest,  // _Path will contain the file path
  GetFileNodeLocationsResponse, // _Data will contain comma-separated "ip:port"
                                // strings for nodes; _ErrorCode for status

  // Raft consensus messages used by the metadata server cluster
  RaftRequestVote,
  RaftRequestVoteResponse,
  RaftAppendEntries,
  RaftAppendEntriesResponse,
  /// Snapshot delta from edge node during hot-cache mode
  SnapshotDelta
};

/**
 * @brief Represents a message for communication between components of
 * SimpliDFS. The meaning and usage of _Filename, _Content, _NodeAddress, and
 * _NodePort can vary depending on the _Type of the message.
 */
struct Message {
  MessageType _Type; ///< The type of the message, determining how other fields
                     ///< are interpreted.

  /**
   * @brief Primary identifier, often a filename.
   * For node registration or heartbeats, this typically stores the
   * nodeIdentifier/nodeName. For file operations (Create, Write, Read, Delete),
   * it's the name of the file. For replication commands, it's the name of the
   * file being replicated.
   */
  std::string _Filename;

  /**
   * @brief Content payload of the message.
   * For WriteFile, this is the content to write.
   * For FileRead (response), this is the content read from the file.
   * For Replicate/ReceiveFileCommand, this can be used to store auxiliary
   * information like source/target node IDs for confirmation. For other types,
   * it may be unused or have a specific contextual meaning.
   */
  std::string _Content;

  /**
   * @brief Network address associated with the message, typically an IP address
   * or hostname. For RegisterNode, this is the address of the registering node.
   * For ReplicateFileCommand, this is the address of the target node for
   * replication. For ReceiveFileCommand, this is the address of the source node
   * for replication. Format: "ip:port" string or just "ip" if _NodePort is used
   * separately.
   */
  std::string _NodeAddress;

  /**
   * @brief Network port associated with the message.
   * For RegisterNode, this is the port the registering node is listening on.
   * For other types where a specific port is relevant (e.g., distinguishing
   * services on a node).
   */
  int _NodePort = 0; // Default initialize

  // New fields
  int _ErrorCode = 0;   // For responses, errno values
  uint32_t _Mode = 0;   // File mode
  uint32_t _Uid = 0;    // User ID
  uint32_t _Gid = 0;    // Group ID
  int64_t _Offset = 0;  // File offset
  uint64_t _Size = 0;   // File size or operation size
  std::string _Data;    // General purpose data field
  std::string _Path;    // Alternative to _Filename, for clarity
  std::string _NewPath; // For rename operations

public:
  /**
   * @brief Encode binary data using URL-safe base64 without padding.
   */
  static std::string b64Encode(const std::string &in) {
    if (in.empty()) return "";
    std::string out(((in.size() + 2) / 3) * 4, '\0');
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                              reinterpret_cast<const unsigned char *>(in.data()),
                              in.size());
    out.resize(len);
    for (char &c : out) {
      if (c == '+') c = '-';
      else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
  }

  /**
   * @brief Decode URL-safe base64 without padding.
   */
  static std::string b64Decode(const std::string &in) {
    if (in.empty()) return "";
    std::string tmp = in;
    for (char &c : tmp) {
      if (c == '-') c = '+';
      else if (c == '_') c = '/';
    }
    int added = (4 - tmp.size() % 4) % 4;
    tmp.append(added, '=');
    int pad = 0;
    if (!tmp.empty() && tmp.back() == '=') pad++;
    if (tmp.size() > 1 && tmp[tmp.size() - 2] == '=') pad++;
    std::string out((tmp.size() / 4) * 3, '\0');
    int len = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                              reinterpret_cast<const unsigned char *>(tmp.data()),
                              tmp.size());
    if (len < 0) return "";
    len -= pad;
    if (len < 0) len = 0;
    out.resize(len);
    return out;
  }
  /**
   * @brief Serializes a Message object into a string representation.
   * The format is:
   * Type|Filename|Content|NodeAddress|NodePort|_ErrorCode|_Mode|_Uid|_Gid|_Offset|_Size|_Data|_Path|_NewPath
   * @param msg The Message object to serialize.
   * @return A string representing the serialized message.
   * @note Fields are separated by '|'. Empty fields will result in consecutive
   * delimiters.
   */
  inline static std::string Serialize(const Message &msg) {
    std::ostringstream oss;
    oss << static_cast<int>(msg._Type) << '|' << msg._Filename << '|' <<
        b64Encode(msg._Content) << '|' << msg._NodeAddress << '|' << msg._NodePort
        << '|' << msg._ErrorCode << '|' << msg._Mode << '|' << msg._Uid << '|' << msg._Gid
        << '|' << msg._Offset << '|' << msg._Size << '|' <<
        b64Encode(msg._Data) << '|' << msg._Path << '|' << msg._NewPath;
    return oss.str();
  }

  /**
   * @brief Deserializes a string into a Message object.
   * Parses a string formatted as
   * Type|Filename|Content|NodeAddress|NodePort|_ErrorCode|_Mode|_Uid|_Gid|_Offset|_Size|_Data|_Path|_NewPath
   * @param data The string data to deserialize.
   * @return A Message object.
   * @throw std::runtime_error if parsing fails for critical fields like
   * MessageType or numeric fields, or if the data string is fundamentally
   * malformed.
   * @note Fields are parsed sequentially. Assumes all fields are present, even
   * if empty for strings or zero for numeric types.
   */
  inline static Message Deserialize(const std::string &data) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(data);

    // Split the serialized string on '|'. Include a final empty token when
    // the data ends with a delimiter so trailing fields are preserved as
    // empty strings.
    while (std::getline(iss, token, '|')) {
      tokens.push_back(token);
    }
    if (!data.empty() && data.back() == '|') {
      tokens.emplace_back("");
    }

    if (tokens.empty()) {
      throw std::runtime_error(
          "Deserialize error: Missing MessageType. Data: '" + data + "'");
    }

    Message msg{};
    size_t idx = 0;
    auto next = [&tokens, &idx](const std::string &field) -> std::string {
      if (idx < tokens.size()) {
        return tokens[idx++];
      }
      // Missing trailing fields are interpreted as empty strings so
      // older message formats remain compatible.
      return "";
    };

    try {
      msg._Type = static_cast<MessageType>(std::stoi(next("MessageType")));
    } catch (const std::exception &e) {
      throw std::runtime_error(std::string("Deserialize error: ") + e.what());
    }

    msg._Filename = next("Filename");
    {
      std::string enc = next("Content");
      msg._Content = enc.empty() ? std::string() : b64Decode(enc);
    }
    msg._NodeAddress = next("NodeAddress");

    token = next("NodePort");
    if (!token.empty()) msg._NodePort = std::stoi(token);

    token = next("ErrorCode");
    if (!token.empty()) msg._ErrorCode = std::stoi(token);

    token = next("Mode");
    if (!token.empty()) msg._Mode = static_cast<uint32_t>(std::stoul(token));

    token = next("Uid");
    if (!token.empty()) msg._Uid = static_cast<uint32_t>(std::stoul(token));

    token = next("Gid");
    if (!token.empty()) msg._Gid = static_cast<uint32_t>(std::stoul(token));

    token = next("Offset");
    if (!token.empty()) msg._Offset = std::stoll(token);

    token = next("Size");
    if (!token.empty()) msg._Size = std::stoull(token);

    {
      std::string enc = next("Data");
      msg._Data = enc.empty() ? std::string() : b64Decode(enc);
    }
    msg._Path = next("Path");
    msg._NewPath = next("NewPath");

    return msg;
  }
};

#endif // _SIMPLIDFS_MESSAGE_H
