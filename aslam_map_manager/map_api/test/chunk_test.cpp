#include <glog/logging.h>
#include <gtest/gtest.h>

#include <multiagent_mapping_common/test/testing_entrypoint.h>

#include "map-api/ipc.h"

using namespace map_api;

#include "net_table_test_fixture.cpp"

TEST_P(NetTableTest, NetInsert) {
  Chunk* chunk = table_->newChunk();
  ASSERT_TRUE(chunk);
  insert(42, chunk);
}

TEST_P(NetTableTest, ParticipationRequest) {
  enum SubProcesses {ROOT, A};
  enum Barriers {INIT, DIE};
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    Chunk* chunk= table_->newChunk();
    ASSERT_TRUE(chunk);

    IPC::barrier(INIT, 1);

    EXPECT_EQ(1, MapApiHub::instance().peerSize());
    EXPECT_EQ(0, chunk->peerSize());
    EXPECT_EQ(1, chunk->requestParticipation());
    EXPECT_EQ(1, chunk->peerSize());

    IPC::barrier(DIE, 1);
  } else {
    IPC::barrier(INIT, 1);
    IPC::barrier(DIE, 1);
  }
}

TEST_P(NetTableTest, FullJoinTwice) {
  enum SubProcesses {ROOT, A, B};
  enum Barriers {ROOT_A_INIT, A_JOINED_B_INIT, B_JOINED, DIE};
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    Chunk* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    insert(42, chunk);

    IPC::barrier(ROOT_A_INIT, 1);

    EXPECT_EQ(1, MapApiHub::instance().peerSize());
    EXPECT_EQ(0, chunk->peerSize());
    EXPECT_EQ(1, chunk->requestParticipation());
    EXPECT_EQ(1, chunk->peerSize());
    launchSubprocess(B);

    IPC::barrier(A_JOINED_B_INIT, 2);

    EXPECT_EQ(2, MapApiHub::instance().peerSize());
    EXPECT_EQ(1, chunk->peerSize());
    EXPECT_EQ(1, chunk->requestParticipation());
    EXPECT_EQ(2, chunk->peerSize());

    IPC::barrier(B_JOINED, 2);

    IPC::barrier(DIE, 2);
  }
  if(getSubprocessId() == A){
    IPC::barrier(ROOT_A_INIT, 1);
    IPC::barrier(A_JOINED_B_INIT, 2);
    EXPECT_EQ(1, count());
    IPC::barrier(B_JOINED, 2);
    IPC::barrier(DIE, 2);
  }
  if(getSubprocessId() == B){
    IPC::barrier(A_JOINED_B_INIT, 2);
    IPC::barrier(B_JOINED, 2);
    EXPECT_EQ(1, count());
    IPC::barrier(DIE, 2);
  }
}

TEST_P(NetTableTest, RemoteInsert) {
  enum Subprocesses {ROOT, A};
  enum Barriers {INIT, A_JOINED, A_ADDED, DIE};
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    Chunk* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    IPC::barrier(INIT, 1);

    chunk->requestParticipation();
    IPC::push(chunk->id().hexString());
    IPC::barrier(A_JOINED, 1);
    IPC::barrier(A_ADDED, 1);

    EXPECT_EQ(1, count());
    IPC::barrier(DIE, 1);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    IPC::barrier(A_JOINED, 1);
    Id chunk_id = popId();
    insert(42, table_->getChunk(chunk_id));

    IPC::barrier(A_ADDED, 1);
    IPC::barrier(DIE, 1);
  }
}

TEST_P(NetTableTest, RemoteUpdate) {
  if (!GetParam()) { // not updateable - just pass test
    return;
  }
  enum Subprocesses {ROOT, A};
  enum Barriers {INIT, A_JOINED, A_UPDATED, DIE};
  std::unordered_map<Id, std::shared_ptr<Revision> > results;
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    Chunk* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    insert(42, chunk);
    table_->dumpCache(LogicalTime::sample(), &results);
    EXPECT_EQ(1, results.size());
    EXPECT_TRUE(results.begin()->second->verify(kFieldName, 42));
    IPC::barrier(INIT, 1);

    chunk->requestParticipation();
    IPC::barrier(A_JOINED, 1);
    IPC::barrier(A_UPDATED, 1);
    table_->dumpCache(LogicalTime::sample(), &results);
    EXPECT_EQ(1, results.size());
    EXPECT_TRUE(results.begin()->second->verify(kFieldName, 21));

    IPC::barrier(DIE, 1);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    IPC::barrier(A_JOINED, 1);
    table_->dumpCache(LogicalTime::sample(), &results);
    EXPECT_EQ(1, results.size());
    results.begin()->second->set(kFieldName, 21);
    EXPECT_TRUE(table_->update(results.begin()->second.get()));

    IPC::barrier(A_UPDATED, 1);
    IPC::barrier(DIE, 1);
  }
}

DEFINE_uint64(grind_processes, 10u,
              "Total amount of processes in ChunkTest.Grind");
DEFINE_uint64(grind_cycles, 10u,
              "Total amount of insert-update cycles in ChunkTest.Grind");

TEST_P(NetTableTest, Grind) {
  const int kInsertUpdateCycles = FLAGS_grind_cycles;
  const uint64_t kProcesses = FLAGS_grind_processes;
  enum Barriers {INIT, ID_SHARED, DIE};
  std::unordered_map<Id, std::shared_ptr<Revision> > results;
  if (getSubprocessId() == 0) {
    std::ostringstream extra_flags_ss;
    extra_flags_ss << "--grind_processes=" << FLAGS_grind_processes << " ";
    extra_flags_ss << "--grind_cycles=" << FLAGS_grind_cycles;
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i, extra_flags_ss.str());
    }
    Chunk* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    IPC::barrier(INIT, kProcesses - 1);
    chunk->requestParticipation();
    IPC::push(chunk->id().hexString());
    IPC::barrier(ID_SHARED, kProcesses - 1);
    IPC::barrier(DIE, kProcesses - 1);
    EXPECT_EQ(kInsertUpdateCycles * (kProcesses - 1), count());
  } else {
    IPC::barrier(INIT, kProcesses - 1);
    IPC::barrier(ID_SHARED, kProcesses - 1);
    Id chunk_id = popId();
    Chunk* chunk = table_->getChunk(chunk_id);
    for (int i = 0; i < kInsertUpdateCycles; ++i) {
      // insert
      insert(42, chunk);
      // update
      if (GetParam()){
        table_->dumpCache(LogicalTime::sample(), &results);
        results.begin()->second->set(kFieldName, 21);
        EXPECT_TRUE(table_->update(results.begin()->second.get()));
      }
    }
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_P(NetTableTest, ChunkTransactions) {
  const uint64_t kProcesses = FLAGS_grind_processes;
  enum Barriers {INIT, IDS_SHARED, DIE};
  std::unordered_map<Id, std::shared_ptr<Revision> > results;
  if (getSubprocessId() == 0) {
    std::ostringstream extra_flags_ss;
    extra_flags_ss << "--grind_processes=" << FLAGS_grind_processes << " ";
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i, extra_flags_ss.str());
    }
    Chunk* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    Id insert_id = insert(1, chunk);
    IPC::barrier(INIT, kProcesses - 1);

    chunk->requestParticipation();
    IPC::push(chunk->id().hexString());
    IPC::push(insert_id.hexString());
    IPC::barrier(IDS_SHARED, kProcesses - 1);

    IPC::barrier(DIE, kProcesses - 1);
    table_->dumpCache(LogicalTime::sample(), &results);
    EXPECT_EQ(kProcesses, results.size());
    std::unordered_map<Id, std::shared_ptr<Revision> >::iterator found =
        results.find(insert_id);
    if (found != results.end()) {
      int final_value;
      found->second->get(kFieldName, &final_value);
      if (GetParam()){
        EXPECT_EQ(kProcesses, final_value);
      } else {
        EXPECT_EQ(1, final_value);
      }
    } else {
      // still need a clean disconnect
      EXPECT_TRUE(false);
    }
  } else {
    IPC::barrier(INIT, kProcesses - 1);
    IPC::barrier(IDS_SHARED, kProcesses - 1);
    Id chunk_id = popId(), item_id = popId();
    Chunk* chunk = table_->getChunk(chunk_id);
    ASSERT_TRUE(chunk);
    std::shared_ptr<ChunkTransaction> transaction;
    while (true) {
      transaction = chunk->newTransaction();
      // insert
      insert(42, transaction.get());
      // update
      if (GetParam()){
        int transient_value;
        std::shared_ptr<Revision> to_update = transaction->getById(item_id);
        to_update->get(kFieldName, &transient_value);
        ++transient_value;
        to_update->set(kFieldName, transient_value);
        transaction->update(to_update);
      }
      if (chunk->commit(*transaction)){
        break;
      }
    }
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_P(NetTableTest, ChunkTransactionsConflictConditions) {
  if (GetParam()) {
    return; // No need to test this for updateable tables as well
  }
  const uint64_t kProcesses = FLAGS_grind_processes;
  const int kUniqueItems = 10;
  enum Barriers {INIT, ID_SHARED, DIE};
  CRTable::RevisionMap results;
  if (getSubprocessId() == 0) {
    std::ostringstream extra_flags_ss;
    extra_flags_ss << "--grind_processes=" << FLAGS_grind_processes << " ";
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i, extra_flags_ss.str());
    }
    Chunk* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    IPC::barrier(INIT, kProcesses - 1);

    chunk->requestParticipation();
    IPC::push(chunk->id().hexString());
    IPC::barrier(ID_SHARED, kProcesses - 1);

    IPC::barrier(DIE, kProcesses - 1);
    table_->dumpCache(LogicalTime::sample(), &results);
    EXPECT_EQ(kUniqueItems, results.size());
    std::set<int> unique_results;
    for (const CRTable::RevisionMap::value_type & item : results) {
      int result;
      item.second->get(kFieldName, &result);
      unique_results.insert(result);
    }
    EXPECT_EQ(kUniqueItems, unique_results.size());
    int i = 0;
    for (int unique_result : unique_results) {
      EXPECT_EQ(i, unique_result);
      ++i;
    }
  } else {
    IPC::barrier(INIT, kProcesses - 1);
    IPC::barrier(ID_SHARED, kProcesses - 1);
    Id chunk_id = popId();
    Chunk* chunk = table_->getChunk(chunk_id);
    ASSERT_TRUE(chunk);
    std::shared_ptr<ChunkTransaction> transaction;
    for (int i = 0; i < kUniqueItems; ++i) {
      transaction = chunk->newTransaction();
      insert(i, transaction.get());
      transaction->addConflictCondition(kFieldName, i);
      chunk->commit(*transaction);
    }
    IPC::barrier(DIE, kProcesses - 1);
  }
}

MULTIAGENT_MAPPING_UNITTEST_ENTRYPOINT
