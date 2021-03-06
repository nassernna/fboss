// Copyright 2004-present Facebook. All Rights Reserved.
extern "C" {
#include <bcm/l3.h>
#include <bcm/mpls.h>
}

#include <folly/IPAddressV6.h>
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmLabelSwitchingUtils.h"
#include "fboss/agent/hw/bcm/tests/BcmLinkStateDependentTests.h"
#include "fboss/agent/hw/bcm/tests/BcmMplsTestUtils.h"
#include "fboss/agent/hw/bcm/tests/BcmSwitchEnsemble.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"
#include "fboss/agent/hw/bcm/tests/BcmTestPacketTrapAclEntry.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestPacketSnooper.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

namespace {
const facebook::fboss::LabelForwardingEntry::Label kTopLabel{1101};
}

namespace facebook::fboss {

class BcmDataPlaneMPLSTest : public BcmLinkStateDependentTests {
 protected:
  void SetUp() override {
    BcmLinkStateDependentTests::SetUp();
    ecmpHelper_ = std::make_unique<utility::EcmpSetupTargetedPorts6>(
        getProgrammedState(), RouterID(0));

    ecmpSwapHelper_ = std::make_unique<
        utility::MplsEcmpSetupTargetedPorts<folly::IPAddressV6>>(
        getProgrammedState(),
        kTopLabel,
        LabelForwardingAction::LabelForwardingType::SWAP);
  }

  cfg::SwitchConfig initialConfig() const override {
    std::vector<PortID> ports = {
        masterLogicalPortIds()[0],
        masterLogicalPortIds()[1],
    };
    auto config = utility::onePortPerVlanConfig(
        getHwSwitch(), std::move(ports), cfg::PortLoopbackMode::MAC, true);

    cfg::QosMap qosMap;
    for (auto tc = 0; tc < 8; tc++) {
      // setup ingress qos map for dscp
      cfg::DscpQosMap dscpMap;
      dscpMap.internalTrafficClass = tc;
      for (auto dscp = 0; dscp < 8; dscp++) {
        dscpMap.fromDscpToTrafficClass.push_back(8 * tc + dscp);
      }

      // setup egress qos map for tc
      cfg::ExpQosMap expMap;
      expMap.internalTrafficClass = tc;
      expMap.fromExpToTrafficClass.push_back(tc);
      expMap.fromTrafficClassToExp_ref() = 7 - tc;

      qosMap.dscpMaps.push_back(dscpMap);
      qosMap.expMaps.push_back(expMap);
    }
    config.qosPolicies.resize(1);
    config.qosPolicies[0].name = "qp";
    config.qosPolicies[0].qosMap_ref() = qosMap;

    cfg::TrafficPolicyConfig policy;
    policy.defaultQosPolicy_ref() = "qp";
    config.dataPlaneTrafficPolicy_ref() = policy;

    return config;
  }

  uint32_t featuresDesired() const override {
    return (BcmSwitch::LINKSCAN_DESIRED | BcmSwitch::PACKET_RX_DESIRED);
  }

  void addRoute(
      folly::IPAddressV6 prefix,
      uint8_t mask,
      PortDescriptor port,
      LabelForwardingAction::LabelStack stack) {
    auto state = ecmpHelper_->resolveNextHops(
        getProgrammedState(),
        {
            port,
        });

    applyNewState(ecmpHelper_->setupIp2MplsECMPForwarding(
        state,
        {port},
        {{port, std::move(stack)}},
        {RoutePrefixV6{prefix, mask}}));
  }

  LabelForwardingEntry::Label programLabelSwap(PortDescriptor port) {
    auto state = ecmpHelper_->resolveNextHops(
        getProgrammedState(),
        {
            port,
        });
    applyNewState(ecmpSwapHelper_->setupECMPForwarding(state, {port}));
    auto swapNextHop = ecmpSwapHelper_->nhop(port);
    return swapNextHop.action.swapWith().value();
  }

  std::unique_ptr<utility::MplsEcmpSetupTargetedPorts<folly::IPAddressV6>>
      ecmpSwapHelper_;

  void sendL3Packet(
      folly::IPAddressV6 dst,
      PortID from,
      std::optional<DSCP> dscp = std::nullopt) {
    CHECK(ecmpHelper_);
    auto vlanId = utility::firstVlanID(initialConfig());
    // construct eth hdr
    const auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);

    EthHdr::VlanTags_t vlans{
        VlanTag(vlanId, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN))};

    EthHdr eth{intfMac, intfMac, std::move(vlans), 0x86DD};
    // construct l3 hdr
    IPv6Hdr ip6{folly::IPAddressV6("1::10"), dst};
    ip6.nextHeader = 59; /* no next header */
    if (dscp) {
      ip6.trafficClass = (dscp.value() << 2); // last two bits are ECN
    }
    // get tx packet
    auto pkt =
        utility::EthFrame(eth, utility::IPPacket<folly::IPAddressV6>(ip6))
            .getTxPacket(getHwSwitch());
    // send pkt on src port, let it loop back in switch and be l3 switched
    getHwSwitch()->sendPacketOutOfPortSync(std::move(pkt), from);
  }

  int getLabelEntryFromFib() {
    bcm_mpls_tunnel_switch_t info;
    bcm_mpls_tunnel_switch_t_init(&info);
    info.label = kTopLabel;
    info.port = BCM_GPORT_INVALID;
    auto rv = bcm_mpls_tunnel_switch_get(0, &info); // query label fib
    EXPECT_EQ(rv, 0);
    bcm_l3_egress_t egr;
    bcm_l3_egress_t_init(&egr);
    rv = bcm_l3_egress_get(0, info.egress_if, &egr); // query egress
    EXPECT_EQ(rv, 0);
    return egr.mpls_label;
  }

  void sendMplsPacket(
      uint32_t topLabel,
      PortID from,
      std::optional<EXP> exp = std::nullopt) {
    // construct eth hdr
    const auto srcMac = utility::kLocalCpuMac();
    const auto dstMac = utility::kLocalCpuMac(); /* for l3 switching */

    uint8_t tc = exp.has_value() ? static_cast<uint8_t>(exp.value()) : 0;
    MPLSHdr::Label mplsLabel{topLabel, tc, true, 128};

    // construct l3 hdr
    auto srcIp = folly::IPAddressV6(("1001::"));
    auto dstIp = folly::IPAddressV6(("2001::"));

    // get tx packet
    auto frame = utility::getEthFrame(
        srcMac, dstMac, {mplsLabel}, srcIp, dstIp, 10000, 20000);

    auto pkt = frame.getTxPacket(getHwSwitch());

    // send pkt on src port, let it loop back in switch and be l3 switched
    getHwSwitch()->sendPacketOutOfPortSync(std::move(pkt), from);
  }
  bool skipTest() {
    return !isSupported(HwAsic::Feature::MPLS);
  }
  std::unique_ptr<utility::EcmpSetupTargetedPorts6> ecmpHelper_;
};

TEST_F(BcmDataPlaneMPLSTest, Push) {
  if (skipTest()) {
    return;
  }
  auto setup = [=]() {
    // setup ip2mpls route to 2401::201:ab00/120 through
    // port 0 w/ stack {101, 102}
    addRoute(
        folly::IPAddressV6("2401::201:ab00"),
        120,
        PortDescriptor(masterLogicalPortIds()[0]),
        {101, 102});
  };
  auto verify = [=]() {
    // capture packet exiting port 0 (entering due to loopback)
    auto packetCapture = BcmTestPacketTrapAclEntry(
        getHwSwitch()->getUnit(), masterLogicalPortIds()[0]);
    HwTestPacketSnooper snooper(getHwSwitchEnsemble());
    // generate the packet entering  port 1
    sendL3Packet(
        folly::IPAddressV6("2401::201:ab01"),
        masterLogicalPortIds()[1],
        DSCP(16)); // tc = 2 for dscp = 16
    auto pkt = snooper.waitForPacket();
    auto mplsPayLoad = pkt ? pkt->mplsPayLoad() : std::nullopt;
    EXPECT_TRUE(mplsPayLoad.has_value());
    if (!mplsPayLoad) {
      return;
    }
    auto expectedMplsHdr = MPLSHdr({
        MPLSHdr::Label{102, 5, 0, 254}, // exp = 5 for tc = 2
        MPLSHdr::Label{101, 5, 1, 254}, // exp = 5 for tc = 2
    });
    EXPECT_EQ(mplsPayLoad->header(), expectedMplsHdr);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmDataPlaneMPLSTest, Swap) {
  if (skipTest()) {
    return;
  }
  auto setup = [=]() {
    // setup ip2mpls route to 2401::201:ab00/120 through
    // port 0 w/ stack {101, 102}
    programLabelSwap(PortDescriptor(masterLogicalPortIds()[0]));
  };
  auto verify = [=]() {
    // capture packet exiting port 0 (entering due to loopback)
    auto packetCapture = BcmTestPacketTrapAclEntry(
        getHwSwitch()->getUnit(), masterLogicalPortIds()[0]);
    HwTestPacketSnooper snooper(getHwSwitchEnsemble());

    // generate the packet entering  port 1
    sendMplsPacket(
        1101, masterLogicalPortIds()[1], EXP(5)); // send packet with exp 5
    auto pkt = snooper.waitForPacket();

    auto mplsPayLoad = pkt ? pkt->mplsPayLoad() : std::nullopt;

    EXPECT_TRUE(mplsPayLoad.has_value());
    if (!mplsPayLoad) {
      return;
    }
    uint32_t mpls_label = getLabelEntryFromFib();
    auto expectedMplsHdr = MPLSHdr({
        MPLSHdr::Label{mpls_label, 2, true, 127}, // exp is remarked to 2
    });
    EXPECT_EQ(mplsPayLoad->header(), expectedMplsHdr);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
