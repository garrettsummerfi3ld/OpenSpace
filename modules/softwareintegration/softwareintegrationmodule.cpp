/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2020                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/softwareintegration/softwareintegrationmodule.h>

#include <modules/softwareintegration/rendering/renderablepointscloud.h>
#include <openspace/documentation/documentation.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/openspaceengine.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/rendering/renderable.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scene.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/scripting/lualibrary.h>
#include <openspace/util/factorymanager.h>
#include <openspace/query/query.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/fmt.h>
#include <ghoul/glm.h>
#include <ghoul/io/socket/tcpsocket.h>
#include <ghoul/io/socket/tcpsocketserver.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/assert.h>
#include <ghoul/misc/templatefactory.h>
#include <functional>

using namespace std::string_literals;

#pragma optimize {"",off}

namespace {
    constexpr const char* _loggerCat = "SoftwareIntegrationModule";
} // namespace

namespace openspace {

    const unsigned int SoftwareConnection::ProtocolVersion = 1;

    SoftwareIntegrationModule::SoftwareIntegrationModule() : OpenSpaceModule(Name) {}

    SoftwareConnection::Message::Message(MessageType type, std::vector<char> content)
        : type(type)
        , content(std::move(content))
    {}

    SoftwareConnection::SoftwareConnectionLostError::SoftwareConnectionLostError()
        : ghoul::RuntimeError("Connection lost", "Connection")
    {}

    SoftwareConnection::SoftwareConnection(std::unique_ptr<ghoul::io::TcpSocket> socket)
        : _socket(std::move(socket))
    {}

    void SoftwareIntegrationModule::internalInitialize(const ghoul::Dictionary&) {
        auto fRenderable = FactoryManager::ref().factory<Renderable>();
        ghoul_assert(fRenderable, "No renderable factory existed");

        fRenderable->registerClass<RenderablePointsCloud>("RenderablePointsCloud");

        start(4700);
    }

    void SoftwareIntegrationModule::internalDeinitializeGL() {

    }

    // Connection 
    bool SoftwareConnection::isConnectedOrConnecting() const {
        return _socket->isConnected() || _socket->isConnecting();
    }

    // Connection 
    void SoftwareConnection::disconnect() {
        if (_socket) {
            _socket->disconnect();
        }
    }

    // Connection 
    ghoul::io::TcpSocket* SoftwareConnection::socket() {
        return _socket.get();
    }

    // Connection 
    SoftwareConnection::Message SoftwareConnection::receiveMessage() {
        // Header consists of version (1 char), message type (4 char) & message size (4 char)
        size_t HeaderSize = 9 * sizeof(char);

        // Create basic buffer for receiving first part of messages
        std::vector<char> headerBuffer(HeaderSize);
        std::vector<char> messageBuffer;

        // Receive the header data
        if (!_socket->get(headerBuffer.data(), HeaderSize)) {
            LERROR("Failed to read header from socket. Disconnecting.");
            throw SoftwareConnectionLostError();
        }

        // Read and convert version number
        std::string version;
        version.push_back(headerBuffer[0]);
        const uint32_t protocolVersionIn = std::stoi(version);

        // Make sure that header matches the protocol version
        if (!(protocolVersionIn == ProtocolVersion)) {
            LERROR(fmt::format(
                "Protocol versions do not match. Remote version: {}, Local version: {}",
                protocolVersionIn,
                ProtocolVersion
            ));
            throw SoftwareConnectionLostError();
        }

        std::string header = "O";
        sendMessage(header);
        LERROR(fmt::format("Meddelandet som skickas {}", header));

        // Read message typ: byte 1-4
        std::string type;
        for(int i = 1; i < 5; i++)
            type.push_back(headerBuffer[i]);

        // Read and convert message size: byte 5-8
        std::string messageSizeIn;
        for (int i = 5; i < 9; i++)
            messageSizeIn.push_back(headerBuffer[i]);
        const size_t messageSize = stoi(messageSizeIn);

        // Receive the message data
        messageBuffer.resize(messageSize);
        if (!_socket->get(messageBuffer.data(), messageSize)) {
            LERROR("Failed to read message from socket. Disconnecting.");
            throw SoftwareConnectionLostError();
        }

        // And delegate decoding depending on type
        if (type == "CONN")
            return Message(MessageType::Connection, messageBuffer);
        else if( type == "ASGN")
            return Message(MessageType::AddSceneGraphNode, messageBuffer);
        else if (type == "RSGN")
            return Message(MessageType::RemoveSceneGraphNode, messageBuffer);
        else if (type == "UPCO")
            return Message(MessageType::Color, messageBuffer);
        else if (type == "UPOP")
            return Message(MessageType::Opacity, messageBuffer);
        else if( type == "UPSI")
            return Message(MessageType::Size, messageBuffer);
        else if (type == "DISC")
            return Message(MessageType::Disconnection, messageBuffer);
        else {
            LERROR(fmt::format("Unsupported message type: {}. Disconnecting...", type));
            return Message(MessageType::Disconnection, messageBuffer);
        }
    }

    //Connection
    bool SoftwareConnection::sendMessage(std::string message) {
        if (!_socket->put<char>(message.data(), message.size())) {
            return false;
        }

        return true;
    }

    // Server
    void SoftwareIntegrationModule::start(int port)
    {
        _socketServer.listen(port);

        _serverThread = std::thread([this]() { handleNewPeers(); });
        _eventLoopThread = std::thread([this]() { eventLoop(); });
    }

    // Server
    void SoftwareIntegrationModule::stop() {
        _shouldStop = true;
        _socketServer.close();
    }

    // Server
    void SoftwareIntegrationModule::handleNewPeers() {
        while (!_shouldStop) {
            std::unique_ptr<ghoul::io::TcpSocket> socket =
                _socketServer.awaitPendingTcpSocket();

            socket->startStreams();

            const size_t id = _nextConnectionId++;
            std::shared_ptr<Peer> p = std::make_shared<Peer>(Peer{
                id,
                "",
                SoftwareConnection(std::move(socket)),
                SoftwareConnection::Status::Connecting,
                std::thread()
                });
            auto it = _peers.emplace(p->id, p);
            it.first->second->thread = std::thread([this, id]() {
                handlePeer(id);
            });
        }
    }

    // Server
    std::shared_ptr<SoftwareIntegrationModule::Peer> SoftwareIntegrationModule::peer(size_t id) {
        std::lock_guard<std::mutex> lock(_peerListMutex);
        auto it = _peers.find(id);
        if (it == _peers.end()) {
            return nullptr;
        }
        return it->second;
    }

    void SoftwareIntegrationModule::handlePeer(size_t id) {
        while (!_shouldStop) {
            std::shared_ptr<Peer> p = peer(id);
            if (!p) {
                return;
            }

            if (!p->connection.isConnectedOrConnecting()) {
                return;
            }
            try {
                SoftwareConnection::Message m = p->connection.receiveMessage();
                _incomingMessages.push({ id, m });
            }
            catch (const SoftwareConnection::SoftwareConnectionLostError&) {
                LERROR(fmt::format("Connection lost to {}", p->id));
                _incomingMessages.push({
                    id,
                    SoftwareConnection::Message(
                        SoftwareConnection::MessageType::Disconnection, std::vector<char>()
                    )
                    });
                return;
            }
        }
    }

    void SoftwareIntegrationModule::eventLoop() {
        while (!_shouldStop) {
            PeerMessage pm = _incomingMessages.pop();
            handlePeerMessage(std::move(pm));
        }
    }

    void SoftwareIntegrationModule::handlePeerMessage(PeerMessage peerMessage) {
        const size_t peerId = peerMessage.peerId;
        auto it = _peers.find(peerId);
        if (it == _peers.end()) {
            return;
        }

        std::shared_ptr<Peer>& peer = it->second;

        const SoftwareConnection::MessageType messageType = peerMessage.message.type;
        std::vector<char>& message = peerMessage.message.content;
        switch (messageType) {
        case SoftwareConnection::MessageType::Connection: {
            std::string software(message.begin(), message.end());
            LINFO(fmt::format("OpenSpace has connected with {} through socket.", software));
            break;
        } 
        case SoftwareConnection::MessageType::AddSceneGraphNode: {
            std::string identifier = readIdentifier(message);
            glm::vec3 color = readColor(message);
            std::string file = readString(message);
            float opacity = readFloatValue(message);
            float size = readFloatValue(message);
            std::string guiName = readString(message);

            ghoul::Dictionary renderable = {
                { "Type", "RenderablePointsCloud"s },
                { "Color", static_cast<glm::dvec3>(color)},
                { "File", file },
                { "Opacity", static_cast<double>(opacity) },
                { "Size", static_cast<double>(size)}
            };

            ghoul::Dictionary gui = {
                { "Name", guiName },
                { "Path", "/Examples"s }
            };

            ghoul::Dictionary node = {
                { "Identifier", identifier },
                { "Renderable", renderable },
                { "GUI", gui }
            };

            try {
                SceneGraphNode* sgn = global::renderEngine.scene()->loadNode(node);
                if (!sgn) {
                    LERROR("Scene", "Could not load scene graph node");
                }
                global::renderEngine.scene()->initializeNode(sgn);
            }
            catch (const documentation::SpecificationError& e) {
                return LERROR(fmt::format("Documentation SpecificationError: Error loading scene graph node {}",
                        e.what())
                );
            }
            catch (const ghoul::RuntimeError& e) {
                return LERROR(fmt::format("RuntimeError: Error loading scene graph node {}",
                    e.what())
                );
            }
            break;
        }
        case SoftwareConnection::MessageType::RemoveSceneGraphNode: {
            std::string identifier(message.begin(), message.end());
            LERROR(fmt::format("Identifier: {}", identifier));
            
            break;
        }
        case SoftwareConnection::MessageType::Color: {
            std::string identifier = readIdentifier(message);
            glm::vec3 color = readColor(message);

            // Update color of renderable
            const Renderable* myrenderable = renderable(identifier);
            properties::Property* colorProperty = myrenderable->property("Color");
            colorProperty->set(color); 
            break;
        }
        case SoftwareConnection::MessageType::Opacity: {
            std::string identifier = readIdentifier(message);
            float opacity = readFloatValue(message);

            // Update opacity of renderable
            const Renderable* myrenderable = renderable(identifier);
            properties::Property* opacityProperty = myrenderable->property("Opacity");
            opacityProperty->set(opacity);
            break;
        }
        case SoftwareConnection::MessageType::Size: {
            std::string identifier = readIdentifier(message);
            float size = readFloatValue(message);

            // Update size of renderable
            const Renderable * myrenderable = renderable(identifier);
            properties::Property* sizeProperty = myrenderable->property("Size");
            sizeProperty->set(size);
            break;
        }
        case SoftwareConnection::MessageType::Disconnection: {
            disconnect(*peer);
            break;
        }
        default:
            LERROR(fmt::format(
                "Unsupported message type: {}", static_cast<int>(messageType)
            ));
            break;
        }
    }

    std::string SoftwareIntegrationModule::readIdentifier(std::vector<char>& message) {

        std::string length;
        length.push_back(message[0]);
        length.push_back(message[1]);

        int lengthOfIdentifier = stoi(length);
        int counter = 0;
        messageOffset = 2;

        std::string identifier;
        while (counter != lengthOfIdentifier)
        {
            identifier.push_back(message[messageOffset]);
            messageOffset++;
            counter++;
        }

        return identifier;
    }

    // Read size value or opacity value
    float SoftwareIntegrationModule::readFloatValue(std::vector<char>& message) {

        std::string length;
        length.push_back(message[messageOffset]);
        messageOffset += 1;

        int lengthOfValue = stoi(length);
        std::string value;
        int counter = 0;
        while (counter != lengthOfValue)
        {
            value.push_back(message[messageOffset]);
            messageOffset++;
            counter++;
        }
        float floatValue = std::stof(value);

        return floatValue;
    }

    // Read Color value
    glm::vec3 SoftwareIntegrationModule::readColor(std::vector<char>& message) {

        std::string lengthOfColor; // Not used for now, but sent in message
        lengthOfColor.push_back(message[messageOffset]);
        lengthOfColor.push_back(message[messageOffset + 1]);
        messageOffset += 2;

        // Red
        std::string red;
        while (message[messageOffset] != ',')
        {
            if (message[messageOffset] == '(')
                messageOffset++;
            else {
                red.push_back(message[messageOffset]);
                messageOffset++;
            }
        }

        // Green
        std::string green;
        messageOffset++;
        while (message[messageOffset] != ',')
        {
            green.push_back(message[messageOffset]);
            messageOffset++;
        }

        // Blue
        std::string blue;
        messageOffset++;
        while (message[messageOffset] != ')')
        {
            blue.push_back(message[messageOffset]);
            messageOffset++;
        }
        messageOffset++;

        // Convert rgb string to floats
        float r = std::stof(red);
        float g = std::stof(green);
        float b = std::stof(blue);

        glm::vec3 color(r, g, b);

        return color;
    }

    // Read File path or GUI Name
    std::string SoftwareIntegrationModule::readString(std::vector<char>& message) {

        std::string length;
        length.push_back(message[messageOffset]);
        length.push_back(message[messageOffset + 1]);
        messageOffset += 2;

        int lengthOfString = stoi(length);
        std::string name;
        int counter = 0;
        while (counter != lengthOfString)
        {
            name.push_back(message[messageOffset]);
            messageOffset++;
            counter++;
        }

        return name;
    }

    // Server
    bool SoftwareIntegrationModule::isConnected(const Peer& peer) const {
        return peer.status != SoftwareConnection::Status::Connecting &&
            peer.status != SoftwareConnection::Status::Disconnected;
    }

    // Server
    void SoftwareIntegrationModule::disconnect(Peer& peer) {
        if (isConnected(peer)) {
            _nConnections = nConnections() - 1;
        }

        peer.connection.disconnect();
        peer.thread.join();
        _peers.erase(peer.id);
    }

    size_t SoftwareIntegrationModule::nConnections() const {
        return _nConnections;
    }

    std::vector<documentation::Documentation> SoftwareIntegrationModule::documentations() const {
        return {
            RenderablePointsCloud::Documentation(),
        };
    }

    scripting::LuaLibrary SoftwareIntegrationModule::luaLibrary() const {
        scripting::LuaLibrary res;
        res.name = "softwareintegration";
        res.scripts = {
            absPath("${MODULE_SOFTWAREINTEGRATION}/scripts/network.lua")
        };
        return res;
    }
} // namespace openspace

