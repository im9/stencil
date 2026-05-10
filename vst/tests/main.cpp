// Test entry point. Phase 1 has engine-only tests with no JUCE dependency,
// so we just run Catch2. Phase 3 (Editor tests) will add
// juce::initialiseJuce_GUI() / shutdownJuce_GUI() around the session per
// oedipa's pattern (Catch2 statics outlive JUCE statics → DeletedAtShutdown
// abort if JUCE init lives at namespace scope).

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    return Catch::Session().run(argc, argv);
}
