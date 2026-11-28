#pragma once
#ifndef _SIMPLIDFS_MESSAGE_H
#define _SIMPLIDFS_MESSAGE_H

#include <algorithm>
#include <cstdint> // For fixed-width integer types
#include <sstream> // For std::ostringstream, std::istringstream
#include <stdexcept> // For std::runtime_error, std::invalid_argument, std::out_of_range
#include <string>
#include <string_view>
#include <vector> // Not strictly needed for these specific implementations, but good for general message handling

/**
 * @brief Defines the types of messages that can be exchanged within the SimpliDFS system.
 */
enum class MessageType{
    // Client to MetaServer, MetaServer to Node
	CreateFile,             ///< Request to create a new file. _Filename required.
	WriteFile,              ///< Request to write content to a file. _Filename and _Content required.
	ReadFile,               ///< Request to read content from a file. _Filename required.
    // MetaServer to Node (response may be simpler or part of a different mechanism)
	FileCreated,            ///< Confirmation that a file has been created.
	FileWritten,            ///< Confirmation that content has been written to a file.
	FileRead,               ///< Contains the content of a read file. _Filename and _Content used.
	FileRemoved,            ///< Confirmation that a file has been removed. (May be deprecated by DeleteFile)
    // Node to MetaServer
	RegisterNode,           ///< Request from a Node to register with the MetaServer. _Filename (as NodeID), _NodeAddress, _NodePort required.
	Heartbeat,              ///< Heartbeat signal from a Node to MetaServer. _Filename (as NodeID) required.
    // MetaServer to Node
	ReplicateFileCommand,   ///< Command from MetaServer to a source Node to replicate a file to another Node. _Filename (file to replicate), _NodeAddress (target node's address:port), _Content (source node ID for logging/confirmation by target) required.
	ReceiveFileCommand,     ///< Command from MetaServer to a destination Node to expect a file from another Node. _Filename (file to receive), _NodeAddress (source node's address:port), _Content (target node ID for logging/confirmation by source) required.
    // Client to MetaServer, MetaServer to Node
	DeleteFile,              ///< Request to delete a file. _Filename required.

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
    GetFileNodeLocationsRequest,    // _Path will contain the file path
    GetFileNodeLocationsResponse,   // _Data will contain comma-separated "ip:port" strings for nodes; _ErrorCode for status

    // Raft consensus messages used by the metadata server cluster
    RaftRequestVote,
    RaftRequestVoteResponse,
    RaftAppendEntries,
    RaftAppendEntriesResponse,
    RaftInstallSnapshot,
    RaftInstallSnapshotResponse,
    RaftTrimLog,
    /// Snapshot delta from edge node during hot-cache mode
    SnapshotDelta
};

/**
 * @brief Represents a message for communication between components of SimpliDFS.
 * The meaning and usage of _Filename, _Content, _NodeAddress, and _NodePort can vary
 * depending on the _Type of the message.
 */
struct Message{
	MessageType _Type;          ///< The type of the message, determining how other fields are interpreted.
	
    /** 
     * @brief Primary identifier, often a filename. 
     * For node registration or heartbeats, this typically stores the nodeIdentifier/nodeName.
     * For file operations (Create, Write, Read, Delete), it's the name of the file.
     * For replication commands, it's the name of the file being replicated.
     */
	std::string _Filename; 
	
    /**
     * @brief Content payload of the message.
     * For WriteFile, this is the content to write.
     * For FileRead (response), this is the content read from the file.
     * For Replicate/ReceiveFileCommand, this can be used to store auxiliary information like source/target node IDs for confirmation.
     * For other types, it may be unused or have a specific contextual meaning.
     */
	std::string _Content;
	
    /**
     * @brief Network address associated with the message, typically an IP address or hostname.
     * For RegisterNode, this is the address of the registering node.
     * For ReplicateFileCommand, this is the address of the target node for replication.
     * For ReceiveFileCommand, this is the address of the source node for replication.
     * Format: "ip:port" string or just "ip" if _NodePort is used separately.
     */
	std::string _NodeAddress; 
	
    /**
     * @brief Network port associated with the message.
     * For RegisterNode, this is the port the registering node is listening on.
     * For other types where a specific port is relevant (e.g., distinguishing services on a node).
     */
	int _NodePort = 0; // Default initialize

    // New fields
    int _ErrorCode = 0;         // For responses, errno values
    uint32_t _Mode = 0;         // File mode
    uint32_t _Uid = 0;          // User ID
    uint32_t _Gid = 0;          // Group ID
    int64_t _Offset = 0;        // File offset
    uint64_t _Size = 0;         // File size or operation size
    std::string _Data;          // General purpose data field
    std::string _Path;          // Alternative to _Filename, for clarity
    std::string _NewPath;       // For rename operations

  private:
    static constexpr std::string_view escapedPrefix = "escaped1|";

    static bool needsEscaping(std::string_view field) {
      return field.find_first_of(std::string_view("|\0", 2)) !=
             std::string_view::npos;
    }

    static std::string escapeField(std::string_view field) {
      std::string encoded;
      encoded.reserve(field.size());
      for (char ch : field) {
        switch (ch) {
        case '\0':
          encoded += "%00";
          break;
        case '\n':
          encoded += "%0A";
          break;
        case '|':
          encoded += "%7C";
          break;
        case '%':
          encoded += "%25";
          break;
        default:
          encoded += ch;
          break;
        }
      }
      return encoded;
    }

    static void unescapeField(std::string &field) {
      std::size_t write = 0;
      for (std::size_t read = 0; read < field.size(); ++read) {
        char ch = field[read];
        if (ch == '%') {
          const auto escape = std::string_view(field).substr(read, 3);
          if (escape == "%00")
            ch = '\0';
          else if (escape == "%0A")
            ch = '\n';
          else if (escape == "%7C")
            ch = '|';
          else if (escape != "%25")
            throw std::runtime_error("Deserialize error: Invalid field escape");
          read += 2;
        }
        field[write++] = ch;
      }
      field.resize(write);
    }

  public:
    /**
     * @brief Serializes a Message object into a string representation.
     * The format is:
     * Type|Filename|Content|NodeAddress|NodePort|_ErrorCode|_Mode|_Uid|_Gid|_Offset|_Size|_Data|_Path|_NewPath
     * @param msg The Message object to serialize.
     * @return A string representing the serialized message.
     * @note Messages containing NUL, pipe, or a newline in the final field use
     * the escaped1 prefix. This form escapes NUL, LF, pipe and percent in
     * string fields. Peers must support escaped1 to exchange these messages.
     */
    inline static std::string Serialize(const Message &msg) {
      const bool escaped =
          needsEscaping(msg._Filename) || needsEscaping(msg._Content) ||
          needsEscaping(msg._NodeAddress) || needsEscaping(msg._Data) ||
          needsEscaping(msg._Path) || needsEscaping(msg._NewPath) ||
          msg._NewPath.find('\n') != std::string::npos;
      std::ostringstream oss;
      const auto writeField = [&](const std::string &field) {
        if (escaped)
          oss << escapeField(field);
        else
          oss << field;
      };
      if (escaped)
        oss << escapedPrefix;
      oss << static_cast<int>(msg._Type) << '|';
      writeField(msg._Filename);
      oss << '|';
      writeField(msg._Content);
      oss << '|';
      writeField(msg._NodeAddress);
      oss << '|' << msg._NodePort << '|' << msg._ErrorCode << '|' << msg._Mode
          << '|' << msg._Uid << '|' << msg._Gid << '|' << msg._Offset << '|'
          << msg._Size << '|';
      writeField(msg._Data);
      oss << '|';
      writeField(msg._Path);
      oss << '|';
      writeField(msg._NewPath);
      return oss.str();
    }

    /**
     * @brief Deserializes a string into a Message object.
     * Parses a string formatted as Type|Filename|Content|NodeAddress|NodePort|_ErrorCode|_Mode|_Uid|_Gid|_Offset|_Size|_Data|_Path|_NewPath
     * @param data The string data to deserialize.
     * @return A Message object.
     * @throw std::runtime_error if parsing fails for critical fields like MessageType or numeric fields,
     *        or if the data string is fundamentally malformed.
     * @note Fields are parsed sequentially. Assumes all fields are present, even if empty for strings or zero for numeric types.
     */
    inline static Message Deserialize(const std::string &data) {
      const bool escaped =
          data.compare(0, escapedPrefix.size(), escapedPrefix) == 0;
      std::string_view fields(data);
      if (escaped) {
        fields.remove_prefix(escapedPrefix.size());
        if (std::count(fields.begin(), fields.end(), '|') != 13 ||
            fields.find_first_of(std::string_view("\0\n", 2)) !=
                std::string_view::npos)
          throw std::runtime_error(
              "Deserialize error: Invalid escaped message fields");
      }
      Message msg = deserializeFields(fields);
      if (escaped) {
        unescapeField(msg._Filename);
        unescapeField(msg._Content);
        unescapeField(msg._NodeAddress);
        unescapeField(msg._Data);
        unescapeField(msg._Path);
        unescapeField(msg._NewPath);
      }
      return msg;
    }

  private:
    static Message deserializeFields(std::string_view data) {
        std::istringstream iss{std::string(data)};
        const bool trailingDelimiter = !data.empty() && data.back() == '|';
        std::string token;
        Message msg{}; // Value-initialize
        int msg_type_int;

        auto get_token = [&](const std::string& field_name) {
            if (!std::getline(iss, token, '|')) {
                 if (iss.eof() && trailingDelimiter) { // if data ends with delimiter, it means the last field is empty
                    token.clear(); // Treat as empty
                    return;
                }
                throw std::runtime_error("Deserialize error: Stream ended prematurely or missing delimiter. Expected " + field_name + ". Data: '" + std::string(data) + "'");
            }
        };

        auto get_last_token = [&](const std::string& field_name) {
            if (!std::getline(iss, token)) { // Read the rest of the stream for the last field
                if (iss.eof() && trailingDelimiter) { // if data ends with delimiter, it means the last field is empty
                     token.clear(); // Treat as empty
                     return;
                }
                // If stream is just ended without a trailing delimiter, it could be an error or last field is empty
                // However, getline sets eofbit if it reads up to EOF. If token is empty and eof is true, it's an empty last field.
                // If token is not empty, it's a valid last field.
                // If token is empty and eof is not true (e.g. badbit or failbit), then it's an error.
                // For robustness, if getline fails and token is empty, assume it's an empty last field if eof is set.
                if (iss.eof() && token.empty() && !trailingDelimiter) {
                    // This case means stream ended, and there was no final delimiter, and no content for the last field.
                    // This is ambiguous. Could be a malformed message missing its last field or an empty last field without a trailing delimiter.
                    // Given current logic, if it's the very last field, an empty token is acceptable.
                } else if (!iss.eof() && !iss.good()) { // Check for other stream errors
                     throw std::runtime_error("Deserialize error: Stream error while reading " + field_name + ". Data: '" + std::string(data) + "'");
                }
            }
        };


        // Type
        get_token("MessageType");
        try {
            msg_type_int = std::stoi(token);
            msg._Type = static_cast<MessageType>(msg_type_int);
        } catch (const std::invalid_argument& ia) {
            throw std::runtime_error("Deserialize error: Invalid message type format '" + token + "'. " + std::string(ia.what()));
        } catch (const std::out_of_range& oor) {
            throw std::runtime_error("Deserialize error: Message type value out of range '" + token + "'. " + std::string(oor.what()));
        }

        // Filename
        get_token("Filename");
        msg._Filename = token;
        
        // Content
        get_token("Content");
        msg._Content = token;

        // NodeAddress
        get_token("NodeAddress");
        msg._NodeAddress = token;

        // NodePort
        get_token("NodePort");
        if (!token.empty()) {
            try {
                msg._NodePort = std::stoi(token);
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid NodePort format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: NodePort value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _ErrorCode
        get_token("ErrorCode");
        if (!token.empty()) {
            try {
                msg._ErrorCode = std::stoi(token);
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid ErrorCode format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: ErrorCode value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _Mode
        get_token("Mode");
        if (!token.empty()) {
            try {
                msg._Mode = static_cast<uint32_t>(std::stoul(token));
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid Mode format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: Mode value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _Uid
        get_token("Uid");
        if (!token.empty()) {
            try {
                msg._Uid = static_cast<uint32_t>(std::stoul(token));
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid Uid format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: Uid value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _Gid
        get_token("Gid");
        if (!token.empty()) {
            try {
                msg._Gid = static_cast<uint32_t>(std::stoul(token));
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid Gid format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: Gid value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _Offset
        get_token("Offset");
        if (!token.empty()) {
            try {
                msg._Offset = std::stoll(token);
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid Offset format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: Offset value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _Size
        get_token("Size");
        if (!token.empty()) {
            try {
                msg._Size = std::stoull(token);
            } catch (const std::invalid_argument& ia) {
                throw std::runtime_error("Deserialize error: Invalid Size format '" + token + "'. " + std::string(ia.what()));
            } catch (const std::out_of_range& oor) {
                throw std::runtime_error("Deserialize error: Size value out of range '" + token + "'. " + std::string(oor.what()));
            }
        }

        // _Data
        get_token("Data");
        msg._Data = token;

        // _Path
        get_token("Path");
        msg._Path = token;

        // _NewPath (last field)
        get_last_token("NewPath");
        msg._NewPath = token;

        return msg;
    }
};

#endif // _SIMPLIDFS_MESSAGE_H
