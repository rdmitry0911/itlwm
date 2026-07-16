#include <cstdlib>
#include <iostream>

#include "itlwm/hal_iwn/IwnHt40Contracts.hpp"

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "IWN HT40 contract test failed: " << message << '\n';
        std::exit(1);
    }
}

void testNvmPairGeometry()
{
    using namespace IwnHt40Contracts;

    require(isPrimaryWithSecondaryAbove(149, 153),
            "149/153 is one lower-primary 5 GHz HT40 pair");
    require(!isPrimaryWithSecondaryAbove(149, 149),
            "a 40 MHz pair must contain distinct endpoints");
    require(!isPrimaryWithSecondaryAbove(149, 145),
            "the NVM pair must be above its lower primary");
    require(!isPrimaryWithSecondaryAbove(254, 258),
            "out-of-range endpoint is not an 8-bit channel pair");
}

void testPowerRecordSelection()
{
    using namespace IwnHt40Contracts;
    unsigned int powerChannel = 0;

    require(nvmPowerChannel(149, false, &powerChannel) && powerChannel == 149,
            "149 with secondary above uses the 149 NVM record");
    require(nvmPowerChannel(153, true, &powerChannel) && powerChannel == 149,
            "153 with secondary below uses the 149 NVM record");
    require(nvmPowerChannel(5, true, &powerChannel) && powerChannel == 1,
            "2.4 GHz secondary-below uses the lower pair record");
    require(nvmPowerChannel(5, false, &powerChannel) && powerChannel == 5,
            "2.4 GHz secondary-above keeps its own lower pair record");
    require(!nvmPowerChannel(3, true, &powerChannel),
            "secondary-below underflow is rejected");
    require(!nvmPowerChannel(255, false, &powerChannel),
            "power lookup beyond the NVM array is rejected");
    require(!nvmPowerChannel(149, false, nullptr),
            "null power-record output is rejected");
}

void testDirectionalAdmission()
{
    using namespace IwnHt40Contracts;

    require(allowsLocalDirection(true, kSecondaryOffsetAbove, true, false),
            "HT40U admits secondary-above");
    require(allowsLocalDirection(true, kSecondaryOffsetBelow, false, true),
            "HT40D admits secondary-below");
    require(!allowsLocalDirection(true, kSecondaryOffsetBelow, true, false),
            "HT40U rejects secondary-below");
    require(!allowsLocalDirection(true, kSecondaryOffsetAbove, false, true),
            "HT40D rejects secondary-above");
    require(!allowsLocalDirection(false, kSecondaryOffsetAbove, true, false),
            "peer without HT40 capability is rejected");
    require(!allowsLocalDirection(true, kSecondaryOffsetNone, true, true),
            "no secondary offset is rejected");
}

void testSgiAdmissionByEffectiveWidth()
{
    using namespace IwnHt40Contracts;

    require(allowsSgiForEffectiveHtWidth(false, true, false),
            "HT20 admits only peer SGI20");
    require(!allowsSgiForEffectiveHtWidth(false, false, true),
            "HT20 rejects peer SGI40-only");
    require(allowsSgiForEffectiveHtWidth(true, false, true),
            "HT40 admits only peer SGI40");
    require(!allowsSgiForEffectiveHtWidth(true, true, false),
            "HT40 rejects peer SGI20-only");
    require(allowsSgiForEffectiveHtWidth(false, true, true),
            "HT20 keeps SGI when the peer supports both widths");
    require(allowsSgiForEffectiveHtWidth(true, true, true),
            "HT40 keeps SGI when the peer supports both widths");
}

} // namespace

int main()
{
    testNvmPairGeometry();
    testPowerRecordSelection();
    testDirectionalAdmission();
    testSgiAdmissionByEffectiveWidth();
    return 0;
}
