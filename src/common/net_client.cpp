// LBN Coop SA network client implementation (original code).
#include "net_client.h"
#include "log.h"
#include "net_common.h"
#include <enet/enet.h>
#include <chrono>

namespace lbn {

Client::~Client() { disconnect(100); }

bool Client::connect(const std::string& hostName, uint16_t port, const std::string& nick, uint16_t skin,
                     uint32_t timeoutMs) {
    if (status_ != Status::Idle) disconnect(100);
    if (!netInit()) { LOGE("Cliente: falha ao inicializar ENet"); return false; }
    netRef_ = true;
    host_ = enet_host_create(nullptr, 1, kChanCount, 0, 0);
    if (!host_) {
        LOGE("Cliente: nao foi possivel criar o socket UDP");
        teardown();
        return false;
    }
    ENetAddress addr{};
    if (enet_address_set_host(&addr, hostName.c_str()) != 0) {
        LOGE("Cliente: endereco invalido '%s'", hostName.c_str());
        teardown();
        return false;
    }
    addr.port = port;
    peer_ = enet_host_connect(host_, &addr, kChanCount, LBN_PROTOCOL_MAGIC);
    if (!peer_) {
        LOGE("Cliente: enet_host_connect falhou");
        teardown();
        return false;
    }
    timeoutMs_ = timeoutMs;
    enet_peer_timeout(peer_, 32, timeoutMs / 2, timeoutMs);
    nick_ = nick; skin_ = skin;
    status_ = Status::Connecting;
    connectStart_ = nowMs();
    rejected_ = false;
    selfId_ = 0xFF;
    LOGI("Cliente: conectando a %s:%u como '%s'...", hostName.c_str(), unsigned(port), nick.c_str());
    return true;
}

void Client::teardown() {
    if (host_) { enet_host_destroy(host_); host_ = nullptr; }
    peer_ = nullptr;
    status_ = Status::Idle;
    selfId_ = 0xFF;
    if (netRef_) { netShutdown(); netRef_ = false; }
}

void Client::disconnect(uint32_t flushMs) {
    if (!host_) { status_ = Status::Idle; return; }
    if (peer_ && status_ != Status::Idle) {
        enet_peer_disconnect(peer_, kQuitCode);
        ENetEvent ev;
        uint32_t until = nowMs() + flushMs;
        bool done = false;
        while (!done && nowMs() < until && enet_host_service(host_, &ev, 10) >= 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) done = true;
        }
        if (!done) enet_peer_reset(peer_);
        LOGI("Cliente: desconectado");
    }
    teardown();
}

void Client::service(uint32_t waitMs) {
    if (!host_) return;
    ENetEvent ev;
    int r = enet_host_service(host_, &ev, waitMs);
    while (r > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            status_ = Status::Handshake;
            {
                HelloMsg h; h.nick = nick_; h.skin = skin_;
                sendTo(peer_, encode(h), true);
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            handlePacket(ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT: {
            Status was = status_;
            bool wasRejected = rejected_;
            teardown();
            if (wasRejected) { /* Rejected event already queued */ }
            else if (was == Status::Connecting) {
                ClientEvent e{ClientEvent::ConnectFailed}; e.text = "host nao respondeu";
                push(e);
                LOGW("Cliente: falha ao conectar (host nao respondeu)");
            } else {
                ClientEvent e{ClientEvent::Disconnected};
                e.text = ev.data == kQuitCode ? "o host encerrou a sessao" : "conexao perdida (timeout)";
                push(e);
                LOGW("Cliente: %s", e.text.c_str());
            }
            return; // host_ is gone
        }
        default: break;
        }
        if (!host_) return;
        r = enet_host_check_events(host_, &ev);
    }
    // Overall handshake timeout (ENet itself retries the connect for a while).
    if (host_ && status_ != Status::Connected && nowMs() - connectStart_ > timeoutMs_ + 2000) {
        teardown();
        ClientEvent e{ClientEvent::ConnectFailed}; e.text = "tempo esgotado";
        push(e);
        LOGW("Cliente: tempo esgotado ao conectar");
    }
}

void Client::handlePacket(const uint8_t* data, size_t len) {
    MsgType t;
    if (!peekType(data, len, t)) return;
    switch (t) {
    case MsgType::Welcome: {
        WelcomeMsg w;
        if (!decode(data, len, w)) return;
        selfId_ = w.yourId;
        serverName_ = w.serverName;
        status_ = Status::Connected;
        ClientEvent e{ClientEvent::Connected}; e.id = w.yourId; e.text = w.serverName; e.nick = w.yourNick;
        push(e);
        LOGI("Cliente: conectado ao servidor '%s' (meu id %u, apelido '%s')", w.serverName.c_str(), unsigned(w.yourId), w.yourNick.c_str());
        if (!w.yourNick.empty() && w.yourNick != nick_)
            LOGW("Cliente: apelido '%s' ja estava em uso; o servidor me chamou de '%s'", nick_.c_str(), w.yourNick.c_str());
        break;
    }
    case MsgType::Reject: {
        RejectMsg rm;
        if (!decode(data, len, rm)) return;
        rejected_ = true;
        ClientEvent e{ClientEvent::Rejected};
        e.text = std::string(rejectReasonPtBr(rm.reason)) + (rm.detail.empty() ? "" : " (" + rm.detail + ")");
        push(e);
        LOGW("Cliente: recusado pelo servidor: %s", e.text.c_str());
        break;
    }
    case MsgType::PlayerJoin: {
        PlayerJoinMsg j;
        if (!decode(data, len, j) || j.id >= LBN_MAX_PLAYERS) return;
        ClientEvent e{ClientEvent::PlayerJoined}; e.id = j.id; e.nick = j.nick; e.skin = j.skin;
        push(e);
        break;
    }
    case MsgType::PlayerLeave: {
        PlayerLeaveMsg l;
        if (!decode(data, len, l) || l.id >= LBN_MAX_PLAYERS) return;
        ClientEvent e{ClientEvent::PlayerLeft}; e.id = l.id; e.leaveReason = l.reason;
        push(e);
        break;
    }
    case MsgType::StateS2C: {
        ClientEvent e{ClientEvent::State};
        if (!decodeStateS2C(data, len, e.id, e.state) || e.id >= LBN_MAX_PLAYERS || !isSaneState(e.state)) return;
        e.recvMs = nowMs();
        push(e);
        break;
    }
    case MsgType::ChatS2C: {
        ChatMsg c;
        if (!decodeChatS2C(data, len, c)) return;
        ClientEvent e{ClientEvent::Chat}; e.id = c.from; e.text = c.text;
        push(e);
        break;
    }
    case MsgType::Pings: {
        ClientEvent e{ClientEvent::Pings};
        if (!decode(data, len, e.pings)) return;
        push(e);
        break;
    }
    case MsgType::DamageS2C: {
        ClientEvent e{ClientEvent::Damage};
        if (!decodeDamageS2C(data, len, e.damage) || e.damage.peer >= LBN_MAX_PLAYERS || e.damage.amountX10 > 2000) return;
        e.id = e.damage.peer;
        push(e);
        break;
    }
    case MsgType::WorldS2C: {
        WorldS2CMsg m;
        if (!decode(data, len, m)) return;
        ClientEvent e{ClientEvent::World}; e.id = m.authorityId; e.world = m.w;
        push(e);
        break;
    }
    case MsgType::Session: {
        SessionMsg m;
        if (!decode(data, len, m)) return;
        ClientEvent e{ClientEvent::Session}; e.id = m.authorityId;
        push(e);
        LOGI("Cliente: autoridade do mundo (host) = id %u", unsigned(m.authorityId));
        break;
    }
    default: break;
    }
}

bool Client::pollEvent(ClientEvent& out) {
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

bool Client::sendState(const PlayerState& s) {
    if (status_ != Status::Connected) return false;
    return sendTo(peer_, encodeStateC2S(s), false);
}

bool Client::sendChat(const std::string& text) {
    if (status_ != Status::Connected || text.empty()) return false;
    return sendTo(peer_, encodeChatC2S(text), true);
}

bool Client::sendDamage(const DamageMsg& d) {
    if (status_ != Status::Connected) return false;
    return sendTo(peer_, encodeDamageC2S(d), true);
}

bool Client::sendWorld(const WorldState& w) {
    if (status_ != Status::Connected) return false;
    return sendTo(peer_, encodeWorldC2S(w), true);
}

uint32_t Client::pingMs() const { return peer_ ? peer_->roundTripTime : 0; }

// ---------------- AsyncClient ----------------

bool AsyncClient::connect(const std::string& host, uint16_t port, const std::string& nick, uint16_t skin,
                          uint32_t timeoutMs) {
    stop();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!client_.connect(host, port, nick, skin, timeoutMs)) return false;
    }
    run_ = true;
    thread_ = std::thread([this] {
        while (run_) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                client_.service(0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });
    return true;
}

void AsyncClient::stop() {
    run_ = false;
    if (thread_.joinable()) thread_.join();
}

void AsyncClient::disconnect(uint32_t flushMs) {
    stop();
    std::lock_guard<std::mutex> lk(mu_);
    client_.disconnect(flushMs);
}

bool AsyncClient::pollEvent(ClientEvent& out) {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.pollEvent(out);
}
bool AsyncClient::sendState(const PlayerState& s) {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.sendState(s);
}
bool AsyncClient::sendChat(const std::string& text) {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.sendChat(text);
}
bool AsyncClient::sendDamage(const DamageMsg& d) {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.sendDamage(d);
}
bool AsyncClient::sendWorld(const WorldState& w) {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.sendWorld(w);
}
Client::Status AsyncClient::status() {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.status();
}
uint8_t AsyncClient::selfId() {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.selfId();
}
uint32_t AsyncClient::pingMs() {
    std::lock_guard<std::mutex> lk(mu_);
    return client_.pingMs();
}

} // namespace lbn
