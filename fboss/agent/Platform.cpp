/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/Platform.h"
#include "fboss/agent/platforms/common/PlatformProductInfo.h"

#include <folly/Subprocess.h>
#include <string>
#include "fboss/agent/AgentConfig.h"

DEFINE_string(
    crash_switch_state_file,
    "crash_switch_state",
    "File for dumping SwitchState state on crash");

DEFINE_string(
    crash_hw_state_file,
    "crash_hw_state",
    "File for dumping HW state on crash");

DEFINE_string(mac, "", "The local MAC address for this switch");

DEFINE_string(mgmt_if, "eth0", "name of management interface");

using folly::MacAddress;
using folly::Subprocess;

namespace {
MacAddress localMacAddress() {
  if (!FLAGS_mac.empty()) {
    return MacAddress(FLAGS_mac);
  }

  // TODO(t4543375): Get the base MAC address from the BMC.
  //
  // For now, we take the MAC address from eth0, and enable the
  // "locally administered" bit.  This MAC should be unique, and it's fine for
  // us to use a locally administered address for now.
  std::vector<std::string> cmd{"/sbin/ip", "address", "ls", FLAGS_mgmt_if};
  Subprocess p(cmd, Subprocess::Options().pipeStdout());
  auto out = p.communicate();
  p.waitChecked();
  auto idx = out.first.find("link/ether ");
  if (idx == std::string::npos) {
    throw std::runtime_error("unable to determine local mac address");
  }
  MacAddress eth0Mac(out.first.substr(idx + 11, 17));
  return MacAddress::fromHBO(eth0Mac.u64HBO() | 0x0000020000000000);
}

const std::map<int32_t, facebook::fboss::cfg::PlatformPortEntry>
    kEmptyPlatformPorts = {};

const std::map<std::string, facebook::fboss::phy::DataPlanePhyChip>
    kEmptyChips = {};
} // namespace
namespace facebook::fboss {

Platform::Platform(
    std::unique_ptr<PlatformProductInfo> productInfo,
    std::unique_ptr<PlatformMapping> platformMapping)
    : productInfo_(std::move(productInfo)),
      platformMapping_(std::move(platformMapping)) {}
Platform::~Platform() {}

std::string Platform::getCrashHwStateFile() const {
  return getCrashInfoDir() + "/" + FLAGS_crash_hw_state_file;
}

std::string Platform::getCrashSwitchStateFile() const {
  return getCrashInfoDir() + "/" + FLAGS_crash_switch_state_file;
}

const AgentConfig* Platform::config() {
  if (!config_) {
    return reloadConfig();
  }
  return config_.get();
}

const AgentConfig* Platform::reloadConfig() {
  config_ = AgentConfig::fromDefaultFile();
  return config_.get();
}

void Platform::setConfig(std::unique_ptr<AgentConfig> config) {
  config_ = std::move(config);
}

const std::map<int32_t, cfg::PlatformPortEntry>& Platform::getPlatformPorts()
    const {
  return platformMapping_ ? platformMapping_->getPlatformPorts()
                          : kEmptyPlatformPorts;
}

const std::optional<phy::PortProfileConfig> Platform::getPortProfileConfig(
    cfg::PortProfileID profileID) const {
  if (platformMapping_) {
    const auto& supportedProfiles = platformMapping_->getSupportedProfiles();
    auto itProfileConfig = supportedProfiles.find(profileID);
    if (itProfileConfig != supportedProfiles.end()) {
      return itProfileConfig->second;
    }
  }
  return std::nullopt;
}

const std::optional<phy::DataPlanePhyChip> Platform::getDataPlanePhyChip(
    std::string chipName) const {
  const auto& chips = getDataPlanePhyChips();
  if (auto chip = chips.find(chipName); chip != chips.end()) {
    return chip->second;
  } else {
    return std::nullopt;
  }
}

const std::map<std::string, phy::DataPlanePhyChip>&
Platform::getDataPlanePhyChips() const {
  return platformMapping_ ? platformMapping_->getChips() : kEmptyChips;
}

cfg::PortSpeed Platform::getPortMaxSpeed(PortID portID) const {
  return platformMapping_->getPortMaxSpeed(portID);
}

void Platform::init(
    std::unique_ptr<AgentConfig> config,
    uint32_t hwFeaturesDesired) {
  // take ownership of the config if passed in
  config_ = std::move(config);
  std::ignore = getLocalMac();
  initImpl(hwFeaturesDesired);
  // We should always initPorts() here instead of leaving the hw/ to call
  initPorts();
}

MacAddress Platform::getLocalMac() const {
  static std::optional<MacAddress> kLocalMac;
  if (!kLocalMac.has_value()) {
    kLocalMac = localMacAddress();
  }
  return kLocalMac.value();
}

void Platform::getProductInfo(ProductInfo& info) {
  CHECK(productInfo_);
  productInfo_->getInfo(info);
}

PlatformMode Platform::getMode() const {
  CHECK(productInfo_);
  return productInfo_->getMode();
}

} // namespace facebook::fboss
