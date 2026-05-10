// Conformance tests for stencil::engine::Rng against
// docs/ai/rng-test-vectors.json (cross-target / cross-repo synced per
// ADR 005 §RNG sharing). Every entry must match the m4l reference
// implementation byte-for-byte.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <string>

#include "Engine/Rng.h"

using nlohmann::json;
using stencil::engine::nextU32;
using stencil::engine::RngState;
using stencil::engine::seedRng;

namespace
{
json loadVectors()
{
    std::ifstream in(STENCIL_RNG_VECTORS_PATH);
    REQUIRE(in.good());
    json j;
    in >> j;
    return j;
}

uint64_t parseSeedU64(const json& seed)
{
    return std::stoull(seed.at("decimal").get<std::string>());
}
}

TEST_CASE("xoshiro state words after SplitMix64 seeding", "[rng][seed]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("splitmix64_init"));

    for (const auto& tc : v.at("splitmix64_init"))
    {
        const uint64_t seed = parseSeedU64(tc.at("seed"));
        const RngState s = seedRng(seed);

        const auto& expected = tc.at("xoshiro_state_s");
        REQUIRE(expected.size() == 4);
        for (size_t i = 0; i < 4; ++i)
        {
            const uint32_t want = expected[i].at("decimal").get<uint32_t>();
            INFO("seed=" << tc.at("seed").at("hex").get<std::string>()
                         << " word=" << i);
            CHECK(s.s[i] == want);
        }
    }
}

TEST_CASE("xoshiro128++ first-N draws match vectors", "[rng][prng]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("prng"));

    for (const auto& tc : v.at("prng"))
    {
        const uint64_t seed = parseSeedU64(tc.at("seed"));
        RngState rng = seedRng(seed);

        const auto& draws = tc.at("draws");
        for (size_t i = 0; i < draws.size(); ++i)
        {
            const uint32_t got = nextU32(rng);
            const uint32_t want = draws[i].at("decimal").get<uint32_t>();
            INFO("seed=" << tc.at("seed").at("hex").get<std::string>()
                         << " draw=" << i);
            CHECK(got == want);
        }
    }
}
