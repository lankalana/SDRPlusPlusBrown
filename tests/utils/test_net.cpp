// utils/net.h: the socket wrapper behind the network source/sink, rigctl and
// the reporting modules.
//
// The Address tests are pure. The socket tests use real loopback sockets, which
// is the only way to cover the timeout and short-read paths; they bind to an
// ephemeral high port and skip rather than fail if the environment forbids it.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <utils/net.h>

using namespace std::chrono_literals;

namespace {
    // Finds a free port by trying a range. Returns 0 if none worked, which the
    // tests treat as "environment does not allow sockets".
    std::shared_ptr<net::Listener> listenSomewhere(int& portOut) {
        for (int port = 39510; port < 39560; port++) {
            try {
                auto l = net::listen("127.0.0.1", port);
                if (l && l->listening()) {
                    portOut = port;
                    return l;
                }
            }
            catch (const std::exception&) {
                // Port in use, try the next one.
            }
        }
        portOut = 0;
        return nullptr;
    }
}

// ----------------------------------------------------------------- Address

TEST_CASE("net::Address default constructs to 0.0.0.0:0", "[utils][net]") {
    net::Address addr;
    REQUIRE(addr.getIP() == 0);
    REQUIRE(addr.getPort() == 0);
    REQUIRE(addr.getIPStr() == "0.0.0.0");
}

TEST_CASE("net::Address from a host string resolves loopback", "[utils][net]") {
    net::Address addr("127.0.0.1", 8080);
    REQUIRE(addr.getIPStr() == "127.0.0.1");
    REQUIRE(addr.getPort() == 8080);
    REQUIRE(addr.getIP() == 0x7F000001u);
}

TEST_CASE("net::Address from a host-order IP", "[utils][net]") {
    net::Address addr(0xC0A80001u, 1234); // 192.168.0.1
    REQUIRE(addr.getIPStr() == "192.168.0.1");
    REQUIRE(addr.getIP() == 0xC0A80001u);
    REQUIRE(addr.getPort() == 1234);
}

TEST_CASE("net::Address setters round-trip", "[utils][net]") {
    net::Address addr;
    addr.setIP(0x0A000001u); // 10.0.0.1
    addr.setPort(4242);

    REQUIRE(addr.getIP() == 0x0A000001u);
    REQUIRE(addr.getIPStr() == "10.0.0.1");
    REQUIRE(addr.getPort() == 4242);
}

TEST_CASE("net::Address throws for an unresolvable host", "[utils][net]") {
    REQUIRE_THROWS_AS(net::Address("this.host.does.not.exist.invalid", 80), std::runtime_error);
}

TEST_CASE("net::listInterfaces returns something usable", "[utils][net]") {
    // Not every CI container has a named interface, so this only checks that
    // the call is safe and that any entry it does return is well formed.
    auto ifaces = net::listInterfaces();
    for (const auto& [name, info] : ifaces) {
        INFO("interface " << name);
        REQUIRE_FALSE(name.empty());
        // Netmask and broadcast are derived from the address; nothing stronger
        // is portable.
        (void)info;
    }
}

// -------------------------------------------------------------------- TCP

TEST_CASE("TCP loopback carries bytes both ways", "[utils][net][tcp]") {
    int port = 0;
    auto listener = listenSomewhere(port);
    if (!listener) {
        WARN("no bindable loopback port available, skipping");
        return;
    }

    std::shared_ptr<net::Socket> server;
    std::thread acceptor([&]() { server = listener->accept(nullptr, 3000); });

    auto client = net::connect("127.0.0.1", port);
    acceptor.join();

    REQUIRE(client != nullptr);
    REQUIRE(server != nullptr);
    REQUIRE(client->isOpen());
    REQUIRE(server->isOpen());
    REQUIRE(client->type() == net::SOCKET_TYPE_TCP);

    const std::string msg = "hello over tcp";
    REQUIRE(client->sendstr(msg) == (int)msg.size());

    std::vector<uint8_t> buf(msg.size());
    REQUIRE(server->recv(buf.data(), buf.size(), true, 3000) == (int)msg.size());
    REQUIRE(std::string((char*)buf.data(), buf.size()) == msg);

    // And back the other way.
    const std::string reply = "ack";
    REQUIRE(server->sendstr(reply) == 3);
    std::vector<uint8_t> rbuf(3);
    REQUIRE(client->recv(rbuf.data(), 3, true, 3000) == 3);
    REQUIRE(std::string((char*)rbuf.data(), 3) == reply);

    client->close();
    server->close();
    REQUIRE_FALSE(client->isOpen());
    listener->stop();
    REQUIRE_FALSE(listener->listening());
}

TEST_CASE("TCP recvline splits on newlines", "[utils][net][tcp]") {
    int port = 0;
    auto listener = listenSomewhere(port);
    if (!listener) {
        WARN("no bindable loopback port available, skipping");
        return;
    }

    std::shared_ptr<net::Socket> server;
    std::thread acceptor([&]() { server = listener->accept(nullptr, 3000); });
    auto client = net::connect("127.0.0.1", port);
    acceptor.join();
    REQUIRE(server != nullptr);

    client->sendstr("first\nsecond\n");

    std::string line;
    REQUIRE(server->recvline(line, 0, 3000) > 0);
    REQUIRE(line == "first");
    REQUIRE(server->recvline(line, 0, 3000) > 0);
    REQUIRE(line == "second");

    client->close();
    server->close();
    listener->stop();
}

TEST_CASE("TCP recv times out with nothing to read", "[utils][net][tcp]") {
    int port = 0;
    auto listener = listenSomewhere(port);
    if (!listener) {
        WARN("no bindable loopback port available, skipping");
        return;
    }

    std::shared_ptr<net::Socket> server;
    std::thread acceptor([&]() { server = listener->accept(nullptr, 3000); });
    auto client = net::connect("127.0.0.1", port);
    acceptor.join();
    REQUIRE(server != nullptr);

    std::vector<uint8_t> buf(16);
    auto start = std::chrono::steady_clock::now();
    int got = server->recv(buf.data(), buf.size(), false, 200);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // 0 means "timed out or closed" per the documented contract.
    REQUIRE(got == 0);
    REQUIRE(elapsed >= 150ms);

    client->close();
    server->close();
    listener->stop();
}

TEST_CASE("TCP recv reports closure by the peer", "[utils][net][tcp]") {
    int port = 0;
    auto listener = listenSomewhere(port);
    if (!listener) {
        WARN("no bindable loopback port available, skipping");
        return;
    }

    std::shared_ptr<net::Socket> server;
    std::thread acceptor([&]() { server = listener->accept(nullptr, 3000); });
    auto client = net::connect("127.0.0.1", port);
    acceptor.join();
    REQUIRE(server != nullptr);

    client->close();

    std::vector<uint8_t> buf(16);
    REQUIRE(server->recv(buf.data(), buf.size(), false, 3000) <= 0);

    server->close();
    listener->stop();
}

TEST_CASE("Listener accept times out when nobody connects", "[utils][net][tcp]") {
    int port = 0;
    auto listener = listenSomewhere(port);
    if (!listener) {
        WARN("no bindable loopback port available, skipping");
        return;
    }

    auto sock = listener->accept(nullptr, 150);
    REQUIRE(sock == nullptr);
    listener->stop();
}

TEST_CASE("connect to a closed port throws", "[utils][net][tcp]") {
    // Port 1 on loopback is never listening in any sane environment.
    REQUIRE_THROWS_AS(net::connect("127.0.0.1", 1), std::runtime_error);
}

// -------------------------------------------------------------------- UDP

TEST_CASE("UDP loopback carries a datagram", "[utils][net][udp]") {
    // Bind two sockets pointing at each other.
    int aPort = 39601, bPort = 39602;
    std::shared_ptr<net::Socket> a, b;
    for (int attempt = 0; attempt < 20 && !a; attempt++) {
        try {
            a = net::openudp("127.0.0.1", bPort, "127.0.0.1", aPort);
            b = net::openudp("127.0.0.1", aPort, "127.0.0.1", bPort);
        }
        catch (const std::exception&) {
            a.reset();
            b.reset();
            aPort += 2;
            bPort += 2;
        }
    }
    if (!a || !b) {
        WARN("no bindable loopback UDP ports available, skipping");
        return;
    }

    REQUIRE(a->type() == net::SOCKET_TYPE_UDP);

    const std::string msg = "datagram";
    REQUIRE(a->sendstr(msg) == (int)msg.size());

    std::vector<uint8_t> buf(64);
    int got = b->recv(buf.data(), buf.size(), false, 3000);
    REQUIRE(got == (int)msg.size());
    REQUIRE(std::string((char*)buf.data(), got) == msg);

    a->close();
    b->close();
}

TEST_CASE("UDP recv times out", "[utils][net][udp]") {
    std::shared_ptr<net::Socket> s;
    int port = 39701;
    for (int attempt = 0; attempt < 20 && !s; attempt++, port++) {
        try {
            s = net::openudp("127.0.0.1", port + 1000, "127.0.0.1", port);
        }
        catch (const std::exception&) {
            s.reset();
        }
    }
    if (!s) {
        WARN("no bindable loopback UDP port available, skipping");
        return;
    }

    std::vector<uint8_t> buf(16);
    REQUIRE(s->recv(buf.data(), buf.size(), false, 150) == 0);
    s->close();
}
