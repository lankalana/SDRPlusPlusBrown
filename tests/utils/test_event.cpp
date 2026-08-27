// utils/event.h (the C-style Event used across the core) and utils/new_event.h
// (the std::function based replacement).
//
// Both are on the hot path for VFO/sample-rate changes, and Event in particular
// has a subtle contract: emit() copies the handler list first so a handler may
// unbind itself, or others, while the event is being dispatched.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <utils/event.h>
#include <utils/new_event.h>

namespace {
    struct Recorder {
        std::vector<int> seen;
        static void handle(int value, void* ctx) {
            ((Recorder*)ctx)->seen.push_back(value);
        }
    };
}

// ------------------------------------------------------------------- Event

TEST_CASE("Event calls every bound handler", "[utils][event]") {
    Event<int> ev;
    Recorder a, b;
    EventHandler<int> ha(Recorder::handle, &a);
    EventHandler<int> hb(Recorder::handle, &b);

    ev.bindHandler(&ha);
    ev.bindHandler(&hb);
    ev.emit(42);

    REQUIRE(a.seen == std::vector<int>{ 42 });
    REQUIRE(b.seen == std::vector<int>{ 42 });
}

TEST_CASE("Event with no handlers is a no-op", "[utils][event]") {
    Event<int> ev;
    REQUIRE_NOTHROW(ev.emit(1));
    REQUIRE(ev.handlers.empty());
}

TEST_CASE("Event unbindHandler stops delivery", "[utils][event]") {
    Event<int> ev;
    Recorder a;
    EventHandler<int> ha(Recorder::handle, &a);

    ev.bindHandler(&ha);
    ev.emit(1);
    ev.unbindHandler(&ha);
    ev.emit(2);

    REQUIRE(a.seen == std::vector<int>{ 1 });
    REQUIRE(ev.handlers.empty());
}

TEST_CASE("Event unbinding an unknown handler is ignored", "[utils][event]") {
    // It logs an error rather than throwing; callers rely on that during
    // teardown, when the bind may never have happened.
    Event<int> ev;
    Recorder a;
    EventHandler<int> ha(Recorder::handle, &a);
    REQUIRE_NOTHROW(ev.unbindHandler(&ha));
}

TEST_CASE("Event handlers preserve bind order", "[utils][event]") {
    Event<int> ev;
    std::vector<int> order;

    struct Ctx { std::vector<int>* order; int id; };
    Ctx c1{ &order, 1 }, c2{ &order, 2 }, c3{ &order, 3 };
    auto fn = [](int, void* ctx) { ((Ctx*)ctx)->order->push_back(((Ctx*)ctx)->id); };

    EventHandler<int> h1(fn, &c1), h2(fn, &c2), h3(fn, &c3);
    ev.bindHandler(&h1);
    ev.bindHandler(&h2);
    ev.bindHandler(&h3);
    ev.emit(0);

    REQUIRE(order == std::vector<int>{ 1, 2, 3 });
}

TEST_CASE("Event handler can unbind itself during emit", "[utils][event]") {
    // emit() iterates over a copy of the handler list precisely so this is safe.
    // A rewrite that iterates the live list would crash here.
    struct SelfRemover {
        Event<int>* ev;
        EventHandler<int>* self;
        int calls = 0;
    };

    Event<int> ev;
    SelfRemover sr;
    EventHandler<int> h([](int, void* ctx) {
        auto* s = (SelfRemover*)ctx;
        s->calls++;
        s->ev->unbindHandler(s->self);
    }, &sr);
    sr.ev = &ev;
    sr.self = &h;

    ev.bindHandler(&h);
    ev.emit(1);
    REQUIRE(sr.calls == 1);
    REQUIRE(ev.handlers.empty());

    ev.emit(2);
    REQUIRE(sr.calls == 1);
}

TEST_CASE("Event supports non-trivial payload types", "[utils][event]") {
    Event<std::string> ev;
    std::string got;
    EventHandler<std::string> h([](std::string v, void* ctx) { *(std::string*)ctx = v; }, &got);

    ev.bindHandler(&h);
    ev.emit("hello");
    REQUIRE(got == "hello");
}

// ---------------------------------------------------------------- NewEvent

TEST_CASE("NewEvent dispatches to bound lambdas", "[utils][event][newevent]") {
    NewEvent<int> ev;
    int sum = 0;
    ev.bind([&](int v) { sum += v; });
    ev.bind([&](int v) { sum += v * 10; });

    ev(3);
    REQUIRE(sum == 33);
}

TEST_CASE("NewEvent returns distinct handler IDs", "[utils][event][newevent]") {
    NewEvent<> ev;
    auto a = ev.bind([]() {});
    auto b = ev.bind([]() {});
    REQUIRE(a != b);
}

TEST_CASE("NewEvent unbind removes a handler", "[utils][event][newevent]") {
    NewEvent<int> ev;
    int calls = 0;
    auto id = ev.bind([&](int) { calls++; });

    ev(1);
    REQUIRE(calls == 1);

    ev.unbind(id);
    ev(1);
    REQUIRE(calls == 1);
}

TEST_CASE("NewEvent unbind throws for an unknown ID", "[utils][event][newevent]") {
    NewEvent<int> ev;
    REQUIRE_THROWS_AS(ev.unbind(1234), std::runtime_error);

    auto id = ev.bind([](int) {});
    ev.unbind(id);
    REQUIRE_THROWS_AS(ev.unbind(id), std::runtime_error);
}

TEST_CASE("NewEvent reuses freed IDs", "[utils][event][newevent]") {
    // genID scans from 1 for the first free slot, so an unbind makes the ID
    // available again. Anything holding on to a stale ID would silently unbind
    // the wrong handler.
    NewEvent<> ev;
    auto a = ev.bind([]() {});
    ev.unbind(a);
    auto b = ev.bind([]() {});
    REQUIRE(a == b);
}

TEST_CASE("NewEvent binds a member function with a context", "[utils][event][newevent]") {
    struct Target {
        int total = 0;
        void add(int v) { total += v; }
    };

    NewEvent<int> ev;
    Target t;
    ev.bind(&Target::add, &t);

    ev(5);
    ev(7);
    REQUIRE(t.total == 12);
}

TEST_CASE("NewEvent supports multiple arguments and no arguments", "[utils][event][newevent]") {
    NewEvent<int, std::string> two;
    int gotInt = 0;
    std::string gotStr;
    two.bind([&](int i, std::string s) { gotInt = i; gotStr = s; });
    two(9, "nine");
    REQUIRE(gotInt == 9);
    REQUIRE(gotStr == "nine");

    NewEvent<> none;
    bool fired = false;
    none.bind([&]() { fired = true; });
    none();
    REQUIRE(fired);
}
