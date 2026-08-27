// dsp::chain is how IQFrontEnd turns optional preprocessing stages on and off
// without tearing the graph down. The rewiring rules are subtle, so they are
// pinned here before any refactor touches them.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <dsp/chain.h>

#include "support/dsp_test_helpers.h"

using namespace sdrpp_test;

namespace {
    class Adder : public dsp::Processor<float, float> {
        using base_type = dsp::Processor<float, float>;

    public:
        void init(dsp::stream<float>* in, float delta) {
            _delta = delta;
            base_type::init(in);
            base_type::out.setBufferSize(256);
        }

        int process(int count, const float* in, float* out) {
            for (int i = 0; i < count; i++) { out[i] = in[i] + _delta; }
            return count;
        }

        DEFAULT_PROC_RUN

    private:
        float _delta = 0.0f;
    };

    // Records every out-stream change the chain reports.
    struct OutputTracker {
        std::vector<dsp::stream<float>*> seen;
        void operator()(dsp::stream<float>* out) { seen.push_back(out); }
    };
}

TEST_CASE("chain with no enabled block passes its input straight through", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a;
    a.init(nullptr, 1.0f);

    dsp::chain<float> chain(&in);
    REQUIRE(chain.out == &in);

    chain.addBlock(&a, false);
    REQUIRE(chain.out == &in);
}

TEST_CASE("chain enabling a block moves the chain output", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a;
    a.init(nullptr, 1.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, false);

    chain.enableBlock(&a, std::ref(tracker));
    REQUIRE(chain.out == &a.out);
    REQUIRE(tracker.seen.size() == 1);

    chain.disableBlock(&a, std::ref(tracker));
    REQUIRE(chain.out == &in);
    REQUIRE(tracker.seen.size() == 2);
    REQUIRE(tracker.seen.back() == &in);
}

TEST_CASE("chain enabling an already enabled block is a no-op", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a;
    a.init(nullptr, 1.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, true);

    chain.enableBlock(&a, std::ref(tracker));
    REQUIRE(tracker.seen.empty());

    chain.disableBlock(&a, std::ref(tracker));
    chain.disableBlock(&a, std::ref(tracker));
    REQUIRE(tracker.seen.size() == 1);
}

TEST_CASE("chain rejects duplicate and unknown blocks", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 2.0f);

    dsp::chain<float> chain(&in);
    chain.addBlock(&a, false);

    REQUIRE_THROWS_AS(chain.addBlock(&a, false), std::runtime_error);
    REQUIRE_THROWS_AS(chain.enableBlock(&b, [](dsp::stream<float>*) {}), std::runtime_error);
    REQUIRE_THROWS_AS(chain.disableBlock(&b, [](dsp::stream<float>*) {}), std::runtime_error);
    REQUIRE_THROWS_AS(chain.removeBlock(&b, [](dsp::stream<float>*) {}), std::runtime_error);
}

TEST_CASE("chain processes data through all enabled blocks in order", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 10.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, true);
    chain.addBlock(&b, true);
    REQUIRE(chain.out == &b.out);

    StreamCollector<float> collector(chain.out);
    StreamFeeder<float> feeder(&in);

    chain.start();
    REQUIRE(feeder.feed(constant(8, 0.0f)));
    REQUIRE(collector.waitFor(8));

    for (float v : collector.data()) { REQUIRE(v == 11.0f); }

    chain.stop();
    collector.stop();
}

TEST_CASE("chain disabling the middle block keeps the output on the last one", "[dsp][chain]") {
    // KNOWN BUG, pinned rather than asserted away: chain::blockBefore() returns
    // the *first* enabled block instead of the one immediately before, and its
    // own source comments say so ("This is wrong and must be fixed"). With three
    // enabled blocks the third is therefore wired to the first block's output
    // instead of the second's, so two blocks end up reading the same stream and
    // data delivery becomes a race. This test only checks the wiring bookkeeping
    // that is well defined; a fix should extend it to cover data flow.
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b, c;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 10.0f);
    c.init(nullptr, 100.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, true);
    chain.addBlock(&b, true);
    chain.addBlock(&c, true);
    REQUIRE(chain.out == &c.out);

    chain.disableBlock(&b, std::ref(tracker));
    REQUIRE(chain.out == &c.out); // output unchanged, b was in the middle
    REQUIRE(tracker.seen.empty());

    chain.disableBlock(&c, std::ref(tracker));
    REQUIRE(chain.out == &a.out);

    chain.disableBlock(&a, std::ref(tracker));
    REQUIRE(chain.out == &in);
}

TEST_CASE("chain of two blocks applies them in order", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 10.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, true);
    chain.addBlock(&b, true);

    StreamCollector<float> collector(chain.out);
    StreamFeeder<float> feeder(&in);

    chain.start();
    REQUIRE(feeder.feed(constant(4, 0.0f)));
    REQUIRE(collector.waitFor(4));
    REQUIRE(collector.data().back() == 11.0f);

    chain.disableBlock(&b, std::ref(tracker));
    REQUIRE(chain.out == &a.out);

    chain.stop();
    collector.stop();
}

TEST_CASE("chain disabling the last block moves the output back", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 10.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, true);
    chain.addBlock(&b, true);

    chain.disableBlock(&b, std::ref(tracker));
    REQUIRE(chain.out == &a.out);
    REQUIRE(tracker.seen.back() == &a.out);

    chain.disableBlock(&a, std::ref(tracker));
    REQUIRE(chain.out == &in);
}

TEST_CASE("chain enableAllBlocks and disableAllBlocks", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 10.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, false);
    chain.addBlock(&b, false);

    chain.enableAllBlocks(std::ref(tracker));
    REQUIRE(chain.out == &b.out);

    chain.disableAllBlocks(std::ref(tracker));
    REQUIRE(chain.out == &in);
}

TEST_CASE("chain removeBlock disables it first", "[dsp][chain]") {
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a;
    a.init(nullptr, 1.0f);

    dsp::chain<float> chain(&in);
    OutputTracker tracker;
    chain.addBlock(&a, true);
    REQUIRE(chain.out == &a.out);

    chain.removeBlock(&a, std::ref(tracker));
    REQUIRE(chain.out == &in);

    // The block is gone: touching it again must throw.
    REQUIRE_THROWS_AS(chain.enableBlock(&a, std::ref(tracker)), std::runtime_error);
}

TEST_CASE("chain setInput retargets the first enabled block", "[dsp][chain]") {
    dsp::stream<float> inA, inB;
    inA.setBufferSize(256);
    inB.setBufferSize(256);

    Adder a;
    a.init(nullptr, 1.0f);

    dsp::chain<float> chain(&inA);
    OutputTracker tracker;
    chain.addBlock(&a, true);

    chain.setInput(&inB, std::ref(tracker));
    // Output stays on the block; only the block's input moved.
    REQUIRE(chain.out == &a.out);
    REQUIRE(tracker.seen.empty());

    StreamCollector<float> collector(chain.out);
    StreamFeeder<float> feeder(&inB);
    chain.start();
    REQUIRE(feeder.feed(constant(4, 0.0f)));
    REQUIRE(collector.waitFor(4));
    REQUIRE(collector.data().back() == 1.0f);

    chain.stop();
    collector.stop();
}

TEST_CASE("chain setInput with no enabled block updates the output", "[dsp][chain]") {
    dsp::stream<float> inA, inB;
    inA.setBufferSize(256);
    inB.setBufferSize(256);

    Adder a;
    a.init(nullptr, 1.0f);

    dsp::chain<float> chain(&inA);
    OutputTracker tracker;
    chain.addBlock(&a, false);

    chain.setInput(&inB, std::ref(tracker));
    REQUIRE(chain.out == &inB);
    REQUIRE(tracker.seen.size() == 1);
    REQUIRE(tracker.seen.back() == &inB);
}

TEST_CASE("chain re-enables a block after its immediate predecessor",
          "[dsp][chain]")
{
    dsp::stream<float> in;
    in.setBufferSize(256);

    Adder a, b, c;
    a.init(nullptr, 1.0f);
    b.init(nullptr, 10.0f);
    c.init(nullptr, 100.0f);

    dsp::chain<float> chain(&in);

    chain.addBlock(&a, true);
    chain.addBlock(&b, true);
    chain.addBlock(&c, false);

    chain.enableBlock(&c, [](dsp::stream<float>*) {});

    StreamCollector<float> collector(chain.out);
    StreamFeeder<float> feeder(&in);

    chain.start();

    REQUIRE(feeder.feed(constant(256, 0.0f)));
    REQUIRE(collector.waitFor(256));

    for (float v : collector.data()) {
        REQUIRE(v == 111.0f);
    }

    chain.stop();
    collector.stop();
}