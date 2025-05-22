#pragma once
#ifndef _SIMPLIDFS_MESSAGE_H
#define _SIMPLIDFS_MESSAGE_H

#include <string>
#include <stdexcept> // Required for Deserialize exception handling

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
	DeleteFile              ///< Request to delete a file. _Filename required.
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
	int _NodePort;          

public: 
    /**
     * @brief Serializes a Message object into a string representation.
     * The format is: Type|Filename|Content|NodeAddress|NodePort
     * @param msg The Message object to serialize.
     * @return A string representing the serialized message.
     * @note Fields are separated by '|'. Empty fields will result in consecutive delimiters.
     */
    static std::string Serialize(const Message& msg);

    /**
     * @brief Deserializes a string into a Message object.
     * Parses a string formatted as Type|Filename|Content|NodeAddress|NodePort.
     * @param data The string data to deserialize.
     * @return A Message object.
     * @throw std::runtime_error if parsing fails for critical fields like MessageType or numeric NodePort.
     * @note Robustness for missing optional fields (like Filename for some types) is handled by
     *       allowing empty strings for those fields. Callers should validate fields based on MessageType.
     */
    static Message Deserialize(const std::string& data);
};

#endif
