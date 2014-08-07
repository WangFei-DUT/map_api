#include <fstream>
#include <random>

#include <gtest/gtest.h>

#include <multiagent_mapping_common/test/testing_entrypoint.h>
#include <map_api_test_suite/multiprocess_fixture.h>
#include <map-api/ipc.h>
#include <sm_timing/timer.h>

#include "map_api_benchmarks/app.h"
#include "map_api_benchmarks/common.h"
#include "map_api_benchmarks/distance.h"
#include "map_api_benchmarks/kmeans-view.h"
#include "map_api_benchmarks/multi-kmeans-hoarder.h"
#include "map_api_benchmarks/multi-kmeans-worker.h"
#include "map_api_benchmarks/simple-kmeans.h"
#include "floating-point-test-helpers.h"

namespace map_api {
namespace benchmarks {

TEST(KmeansView, InsertFetch) {
  constexpr size_t kClusters = 100, kDataPerCluster = 100;
  constexpr Scalar kTolerance = 1e-4 * kClusters * kDataPerCluster;
  app::init();
  std::mt19937 generator(42);
  DescriptorVector descriptors_in, centers_in, descriptors_out, centers_out;
  std::vector<unsigned int> membership_in, membership_out;
  GenerateTestData(kClusters, kDataPerCluster, 0, generator(), 20., .5,
                   &centers_in, &descriptors_in, &membership_in);
  EXPECT_EQ(kClusters * kDataPerCluster, descriptors_in.size());
  EXPECT_EQ(kClusters, centers_in.size());
  EXPECT_EQ(descriptors_in.size(), membership_in.size());

  Chunk* descriptor_chunk = app::data_point_table->newChunk();
  Chunk* center_chunk = app::center_table->newChunk();
  Chunk* membership_chunk = app::association_table->newChunk();
  KmeansView exporter(descriptor_chunk, center_chunk, membership_chunk);
  exporter.insert(descriptors_in, centers_in, membership_in);

  KmeansView importer(descriptor_chunk, center_chunk, membership_chunk);
  importer.fetch(&descriptors_out, &centers_out, &membership_out);
  EXPECT_EQ(descriptors_in.size(), descriptors_out.size());
  EXPECT_EQ(centers_in.size(), centers_out.size());

  // total checksum
  DescriptorType descriptor_sum;
  descriptor_sum.resize(2, Eigen::NoChange);
  descriptor_sum.setZero();
  for (const DescriptorType& in_descriptor : descriptors_in) {
    descriptor_sum += in_descriptor;
  }
  for (const DescriptorType& out_descriptor : descriptors_out) {
    descriptor_sum -= out_descriptor;
  }
  EXPECT_LT(descriptor_sum.norm(), kTolerance);

  // per-cluster checksum (data could be re-arranged)
  Scalar cluster_sum_product_in = 1., cluster_sum_product_out = 1.;
  for (size_t i = 0; i < centers_in.size(); ++i) {
    DescriptorType cluster_sum;
    cluster_sum.resize(2, Eigen::NoChange);
    cluster_sum.setZero();
    for (size_t j = 0; j < descriptors_in.size(); ++j) {
      if (membership_in[j] == i) {
        cluster_sum += descriptors_in[j];
      }
    }
    cluster_sum_product_in *= cluster_sum.norm();
  }
  for (size_t i = 0; i < centers_out.size(); ++i) {
    DescriptorType cluster_sum;
    cluster_sum.resize(2, Eigen::NoChange);
    cluster_sum.setZero();
    for (size_t j = 0; j < descriptors_out.size(); ++j) {
      if (membership_out[j] == i) {
        cluster_sum += descriptors_out[j];
      }
    }
    cluster_sum_product_out *= cluster_sum.norm();
  }
  EXPECT_EQ(cluster_sum_product_in, cluster_sum_product_out);

  app::kill();
}

class MultiKmeans : public map_api_test_suite::MultiprocessTest {
 protected:
  void SetUpImpl() {
    app::init();
    if (getSubprocessId() == 0){
      DescriptorVector gt_centers;
      DescriptorVector descriptors;
      std::vector<unsigned int> gt_membership, membership;
      generator_ = std::mt19937(40);
      GenerateTestData(kNumfeaturesPerCluster, kNumClusters, kNumNoise,
                       generator_(), kAreaWidth, kClusterRadius,
                       &gt_centers, &descriptors, &gt_membership);
      ASSERT_FALSE(descriptors.empty());
      ASSERT_EQ(descriptors[0].size(), 2u);
      hoarder_.init(descriptors, gt_centers, kAreaWidth, generator_(),
                    &data_chunk_id_, &center_chunk_id_, &membership_chunk_id_);
    }
  }

  void TearDownImpl() {
    app::kill();
  }

  void popIdsInitWorker(){
    CHECK(IPC::pop(&data_chunk_id_));
    CHECK(IPC::pop(&center_chunk_id_));
    CHECK(IPC::pop(&membership_chunk_id_));
    Chunk* descriptor_chunk = app::data_point_table->getChunk(data_chunk_id_);
    Chunk* center_chunk = app::center_table->getChunk(center_chunk_id_);
    Chunk* membership_chunk =
        app::association_table->getChunk(membership_chunk_id_);
    worker_.reset(new MultiKmeansWorker(descriptor_chunk, center_chunk,
                                        membership_chunk));
  }

  void pushIds() {
    IPC::push(data_chunk_id_);
    IPC::push(center_chunk_id_);
    IPC::push(membership_chunk_id_);
  }

  void clearFile(const char* file_name) {
    std::ofstream filestream;
    filestream.open(file_name, std::ios::out | std::ios::trunc);
    filestream.close();
  }

  void putRankMeanMinMax(const char* file_name, const char* tag) {
    std::ofstream filestream;
    filestream.open(file_name, std::ios::out | std::ios::app);
    filestream << PeerId::selfRank() << " " <<
        timing::Timing::GetMeanSeconds(tag) << " " <<
        timing::Timing::GetMinSeconds(tag) << " " <<
        timing::Timing::GetMaxSeconds(tag) << std::endl;
    filestream.close();
  }

  static constexpr size_t kNumClusters = 20;
  static constexpr size_t kNumfeaturesPerCluster = 40;
  static constexpr size_t kNumNoise = 100;
  static constexpr double kAreaWidth = 20.;
  static constexpr double kClusterRadius = 1;

  map_api::Id data_chunk_id_, center_chunk_id_, membership_chunk_id_;
  MultiKmeansHoarder hoarder_;
  std::unique_ptr<MultiKmeansWorker> worker_;
  std::mt19937 generator_;

  const char* kRankFile = "ranks.txt";
  const char* kReadLockFile = "readlock.txt";
  const char* kWriteLockFile = "writelock.txt";

  const char* kReadLockTag = "map_api::Chunk::distributedReadLock";
  const char* kWriteLockTag = "map_api::Chunk::distributedWriteLock";
};

TEST_F(MultiKmeans, KmeansHoarderWorker) {
  enum Processes {HOARDER, WORKER};
  constexpr size_t kIterations = 10;
  int current_barrier = 0;
  DistanceType::result_type result;
  std::vector<DistanceType::result_type> results;
  if (getSubprocessId() == HOARDER) {
    launchSubprocess(WORKER);
    IPC::barrier(current_barrier++, 1);
    pushIds();
    IPC::barrier(current_barrier++, 1);
    // wait for worker to collect chunks and optimize once
    for (size_t i = 0; i < kIterations; ++i) {
      IPC::barrier(current_barrier++, 1);
      std::string result_string;
      IPC::pop(&result_string);
      std::istringstream ss(result_string);
      ss >> result;
      results.push_back(result);
      hoarder_.refresh();
    }
    CHECK_EQ(kIterations, results.size());
    for (size_t i = 1; i < kIterations; ++i) {
      EXPECT_LE(results[i], results[i-1]);
    }
  }
  if (getSubprocessId() == WORKER) {
    IPC::barrier(current_barrier++, 1);
    // wait for hoarder to send chunk ids
    IPC::barrier(current_barrier++, 1);
    popIdsInitWorker();
    for (size_t i = 0; i < kIterations; ++i) {
      result = worker_->clusterOnceAll(generator_());
      std::ostringstream ss;
      ss << result;
      IPC::push(ss.str());
      IPC::barrier(current_barrier++, 1);
    }
  }
}

TEST_F(MultiKmeans, CenterWorkers) {
  enum Barriers {INIT, IDS_PUSHED, DIE};
  constexpr size_t kIterations = 5;
  if (getSubprocessId() == 0) {
    for (size_t i = 1; i <= kNumClusters; ++i) {
      launchSubprocess(i);
    }
    clearFile(kRankFile);
    clearFile(kReadLockFile);
    clearFile(kWriteLockFile);
    IPC::barrier(INIT, kNumClusters);
    pushIds();
    IPC::barrier(IDS_PUSHED, kNumClusters);
    // TODO(tcies) trigger!
    //hoarder_.startRefreshThread();
    IPC::barrier(DIE, kNumClusters);
    //hoarder_.stopRefreshThread();
  } else {
    IPC::barrier(INIT, kNumClusters);
    // wait for hoarder to send chunk ids
    IPC::barrier(IDS_PUSHED, kNumClusters);
    popIdsInitWorker();
    for (size_t i = 0; i < kIterations; ++i) {
      worker_->clusterOnceOne(getSubprocessId() - 1, generator_());
      std::ofstream rankfile;
      rankfile.open("ranks.txt", std::ios::out | std::ios::app);
      rankfile << PeerId::selfRank() << std::endl;
    }
    LOG(INFO) << timing::Timing::Print();
    putRankMeanMinMax(kReadLockFile, kReadLockTag);
    putRankMeanMinMax(kWriteLockFile, kWriteLockTag);
    IPC::barrier(DIE, kNumClusters);
  }
}

}  // namespace map_api
}  // namespace benchmarks

MULTIAGENT_MAPPING_UNITTEST_ENTRYPOINT
