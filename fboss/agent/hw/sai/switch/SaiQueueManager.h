/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/sai/api/QueueApi.h"
#include "fboss/agent/hw/sai/store/SaiObjectWithCounters.h"
#include "fboss/agent/hw/sai/switch/SaiSchedulerManager.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "folly/container/F14Map.h"

#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class HwPortStats;

using SaiQueue = SaiObjectWithCounters<SaiQueueTraits>;
using SaiQueueConfig = std::pair<uint8_t, cfg::StreamType>;

struct SaiQueueHandle {
  std::shared_ptr<SaiQueue> queue;
  std::shared_ptr<SaiScheduler> scheduler;
};

using SaiQueueHandles =
    folly::F14FastMap<SaiQueueConfig, std::unique_ptr<SaiQueueHandle>>;

class SaiQueueManager {
 public:
  SaiQueueManager(SaiManagerTable* managerTable, const SaiPlatform* platform);
  SaiQueueHandles loadQueues(
      PortSaiId portSaiId,
      const std::vector<QueueSaiId>& queueSaiIds);
  void changeQueue(SaiQueueHandle* queueHandle, const PortQueue& newPortQueue);
  void resetQueue(SaiQueueHandle* queueHandle);
  void ensurePortQueueConfig(
      PortSaiId portSaiId,
      const SaiQueueHandles& queueHandles,
      const QueueConfig& queues);
  void updateStats(SaiQueueHandles& queueHandles, HwPortStats& stats);
  void getStats(SaiQueueHandles& queueHandles, HwPortStats& hwPortStats);
  QueueConfig getQueueSettings(const SaiQueueHandles& queueHandles) const;

 private:
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
};

} // namespace facebook::fboss
