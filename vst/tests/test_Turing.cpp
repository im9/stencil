// Conformance tests for stencil::engine::Turing against
// docs/ai/turing-test-vectors.json. Sections covered:
//   - register_init        (createRegister)
//   - register_to_fraction (registerToFraction)
//   - map_to_note          (mapToNote)
//   - shift_and_flip       (shiftAndFlip)
//   - shift_and_force      (shiftAndForce)
//   - tm_step              (tmStep, end-to-end multi-step walks)
// PRNG sections (splitmix64_init, prng) duplicate rng-test-vectors.json
// and are exercised by test_Rng.cpp.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <string>

#include "Engine/Rng.h"
#include "Engine/Turing.h"

using nlohmann::json;
using stencil::engine::createRegister;
using stencil::engine::Fraction;
using stencil::engine::mapToNote;
using stencil::engine::nextU32;
using stencil::engine::RegisterBits;
using stencil::engine::registerToFraction;
using stencil::engine::RngState;
using stencil::engine::seedRng;
using stencil::engine::shiftAndFlip;
using stencil::engine::shiftAndForce;
using stencil::engine::tmStep;
using stencil::engine::TmStepResult;

namespace
{
json loadVectors()
{
    std::ifstream in(STENCIL_TURING_VECTORS_PATH);
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

TEST_CASE("createRegister produces register matching vectors", "[turing][register_init]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("register_init"));

    for (const auto& tc : v.at("register_init"))
    {
        const uint64_t seed = parseSeedU64(tc.at("seed"));
        const int length = tc.at("length").get<int>();
        const uint32_t want = tc.at("register").get<uint32_t>();

        RngState rng = seedRng(seed);
        const RegisterBits got = createRegister(length, rng);

        INFO("seed=" << tc.at("seed").at("hex").get<std::string>()
                     << " length=" << length);
        CHECK(got == want);
    }
}

TEST_CASE("registerToFraction maps to expected num/den", "[turing][register_to_fraction]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("register_to_fraction"));

    for (const auto& tc : v.at("register_to_fraction"))
    {
        const uint32_t reg = tc.at("register").get<uint32_t>();
        const int length = tc.at("length").get<int>();
        const uint32_t wantNum = tc.at("fraction").at("num").get<uint32_t>();
        const uint32_t wantDen = tc.at("fraction").at("den").get<uint32_t>();

        const Fraction f = registerToFraction(reg, length);

        INFO("register=" << tc.at("register_hex").get<std::string>()
                         << " length=" << length);
        CHECK(f.num == wantNum);
        CHECK(f.den == wantDen);
    }
}

TEST_CASE("mapToNote produces expected pitch", "[turing][map_to_note]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("map_to_note"));

    for (const auto& tc : v.at("map_to_note"))
    {
        const uint32_t num = tc.at("num").get<uint32_t>();
        const uint32_t den = tc.at("den").get<uint32_t>();
        const int lo = tc.at("range")[0].get<int>();
        const int hi = tc.at("range")[1].get<int>();
        const int want = tc.at("note").get<int>();

        const int got = mapToNote(num, den, lo, hi);

        INFO("num=" << num << " den=" << den << " range=[" << lo << "," << hi << "]");
        CHECK(got == want);
    }
}

TEST_CASE("shiftAndFlip evolves register per vectors", "[turing][shift_and_flip]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("shift_and_flip"));

    for (const auto& tc : v.at("shift_and_flip"))
    {
        const uint64_t seed = parseSeedU64(tc.at("seed"));
        const uint32_t reg = tc.at("register").get<uint32_t>();
        const int length = tc.at("length").get<int>();
        const double lock = tc.at("lock").get<double>();
        const uint32_t want = tc.at("register_after").get<uint32_t>();

        RngState rng = seedRng(seed);
        const RegisterBits got = shiftAndFlip(reg, length, lock, rng);

        INFO(tc.at("label").get<std::string>());
        CHECK(got == want);
    }
}

TEST_CASE("shiftAndForce shifts and writes the forced bit", "[turing][shift_and_force]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("shift_and_force"));

    for (const auto& tc : v.at("shift_and_force"))
    {
        const uint32_t reg = tc.at("register").get<uint32_t>();
        const int length = tc.at("length").get<int>();
        const int forceBit = tc.at("force_bit").get<int>();
        const uint32_t want = tc.at("register_after").get<uint32_t>();

        const RegisterBits got = shiftAndForce(reg, length, forceBit);

        INFO(tc.at("label").get<std::string>());
        CHECK(got == want);
    }
}

TEST_CASE("tmStep produces expected multi-step traces", "[turing][tm_step]")
{
    const json v = loadVectors();
    REQUIRE(v.contains("tm_step"));

    for (const auto& tc : v.at("tm_step"))
    {
        const std::string name = tc.at("name").get<std::string>();
        const uint64_t seed = parseSeedU64(tc.at("seed"));
        const int length = tc.at("length").get<int>();
        const double lock = tc.at("lock").get<double>();
        const double density = tc.at("density").get<double>();
        const int rangeLo = tc.at("range")[0].get<int>();
        const int rangeHi = tc.at("range")[1].get<int>();
        const uint32_t initialReg = tc.at("initial_register").get<uint32_t>();

        // Reproduce m4l host setup: seed → seedRng → consume one draw via
        // createRegister (exposed in TS as `createRegister(length, rng)`),
        // verify the initial register matches, then iterate tmStep.
        RngState rng = seedRng(seed);
        const RegisterBits initFromRng = createRegister(length, rng);
        INFO(name << " (initial_register check)");
        REQUIRE(initFromRng == initialReg);

        RegisterBits reg = initialReg;
        for (const auto& step : tc.at("trace"))
        {
            const uint32_t wantIn = step.at("register_in").get<uint32_t>();
            const int wantNote = step.at("note").get<int>();
            const bool wantActive = step.at("active").get<bool>();
            const uint32_t wantOut = step.at("register_out").get<uint32_t>();

            INFO(name << " step=" << step.at("step").get<int>());
            CHECK(reg == wantIn);

            const TmStepResult r = tmStep(reg, length, lock, density,
                                          rangeLo, rangeHi, rng);
            CHECK(r.note == wantNote);
            CHECK(r.active == wantActive);
            CHECK(r.reg == wantOut);

            reg = r.reg;
        }
    }
}
