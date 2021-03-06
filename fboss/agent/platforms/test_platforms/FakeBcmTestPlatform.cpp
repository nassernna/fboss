/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/platforms/test_platforms/FakeBcmTestPlatform.h"

#include "fboss/agent/hw/bcm/BcmCosQueueManagerUtils.h"
#include "fboss/agent/hw/switch_asics/FakeAsic.h"
#include "fboss/agent/platforms/common/PlatformProductInfo.h"
#include "fboss/agent/platforms/test_platforms/FakeBcmTestPlatformMapping.h"
#include "fboss/agent/platforms/test_platforms/FakeBcmTestPort.h"

namespace facebook::fboss {

FakeBcmTestPlatform::FakeBcmTestPlatform()
    : BcmTestPlatform(
          fakeProductInfo(),
          std::make_unique<FakeBcmTestPlatformMapping>()) {
  asic_ = std::make_unique<FakeAsic>();
}

FakeBcmTestPlatform::~FakeBcmTestPlatform() {}

std::unique_ptr<BcmTestPort> FakeBcmTestPlatform::createTestPort(PortID id) {
  return std::make_unique<FakeBcmTestPort>(id, this);
}

std::string FakeBcmTestPlatform::getVolatileStateDir() const {
  return tmpDir_.path().string() + "/volatile";
}

std::string FakeBcmTestPlatform::getPersistentStateDir() const {
  return tmpDir_.path().string() + "/persist";
}

HwAsic* FakeBcmTestPlatform::getAsic() const {
  return asic_.get();
}

const PortQueue& FakeBcmTestPlatform::getDefaultPortQueueSettings(
    cfg::StreamType streamType) const {
  return utility::getDefaultPortQueueSettings(
      utility::BcmChip::TOMAHAWK, streamType);
}

const PortQueue& FakeBcmTestPlatform::getDefaultControlPlaneQueueSettings(
    cfg::StreamType streamType) const {
  return utility::getDefaultControlPlaneQueueSettings(
      utility::BcmChip::TOMAHAWK, streamType);
}

} // namespace facebook::fboss
