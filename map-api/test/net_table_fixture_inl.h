#ifndef MAP_API_NET_TABLE_FIXTURE_INL_H_
#define MAP_API_NET_TABLE_FIXTURE_INL_H_

#include <string>

#include <gtest/gtest.h>

#include <dmap/core.h>
#include <dmap/net-table.h>
#include <dmap/net-table-manager.h>
#include <dmap/net-table-transaction.h>
#include <dmap/transaction.h>
#include "./net_table_fixture.h"

namespace dmap {

void NetTableFixture::SetUp() {
  MapApiFixture::SetUp();
  std::shared_ptr<TableDescriptor> descriptor(new TableDescriptor);
  descriptor->setName(kTableName);
  descriptor->addField<int>(kFieldName);
  table_ = NetTableManager::instance().addTable(descriptor);
}

size_t NetTableFixture::count() {
  ConstRevisionMap results;
  table_->dumpActiveChunksAtCurrentTime(&results);
  return results.size();
}

void NetTableFixture::increment(const common::Id& id, ChunkBase* chunk,
                                NetTableTransaction* transaction) {
  CHECK_NOTNULL(chunk);
  CHECK_NOTNULL(transaction);
  ConstRevisionMap chunk_dump;
  transaction->dumpChunk(chunk, &chunk_dump);
  ConstRevisionMap::iterator found = chunk_dump.find(id);
  CHECK(found != chunk_dump.end());
  std::shared_ptr<Revision> to_update;
  found->second->copyForWrite(&to_update);
  int transient_value;
  to_update->get(kFieldName, &transient_value);
  ++transient_value;
  to_update->set(kFieldName, transient_value);
  transaction->update(to_update);
}

void NetTableFixture::increment(NetTable* table, const common::Id& id,
                                ChunkBase* chunk, Transaction* transaction) {
  CHECK_NOTNULL(table);
  CHECK_NOTNULL(chunk);
  CHECK_NOTNULL(transaction);
  ConstRevisionMap chunk_dump;
  transaction->dumpChunk(table, chunk, &chunk_dump);
  ConstRevisionMap::iterator found = chunk_dump.find(id);
  std::shared_ptr<Revision> to_update;
  found->second->copyForWrite(&to_update);
  int transient_value;
  to_update->get(kFieldName, &transient_value);
  ++transient_value;
  to_update->set(kFieldName, transient_value);
  transaction->update(table, to_update);
}

common::Id NetTableFixture::insert(int value, ChunkBase* chunk) {
  common::Id insert_id;
  generateId(&insert_id);
  std::shared_ptr<Revision> to_insert = table_->getTemplate();
  to_insert->setId(insert_id);
  to_insert->set(kFieldName, value);
  EXPECT_TRUE(table_->insert(LogicalTime::sample(), chunk, to_insert));
  return insert_id;
}

common::Id NetTableFixture::insert(int value, ChunkTransaction* transaction) {
  common::Id insert_id;
  generateId(&insert_id);
  std::shared_ptr<Revision> to_insert = table_->getTemplate();
  to_insert->setId(insert_id);
  to_insert->set(kFieldName, value);
  transaction->insert(to_insert);
  return insert_id;
}

void NetTableFixture::insert(int value, common::Id* id,
                             Transaction* transaction) {
  common::Id id_obj;
  if (!id) {
    id = &id_obj;
  }
  CHECK_NOTNULL(transaction);
  generateId(id);
  std::shared_ptr<Revision> to_insert = table_->getTemplate();
  to_insert->setId(*id);
  to_insert->set(kFieldName, value);
  transaction->insert(table_, chunk_, to_insert);
}

void NetTableFixture::insert(int value, const common::Id& id,
                             Transaction* transaction) {
  CHECK_NOTNULL(transaction);
  std::shared_ptr<Revision> to_insert = table_->getTemplate();
  to_insert->setId(id);
  to_insert->set(kFieldName, value);
  transaction->insert(table_, chunk_, to_insert);
}

template <typename IdType>
void NetTableFixture::update(int new_value, const IdType& id,
                             Transaction* transaction) {
  CHECK_NOTNULL(transaction);
  std::shared_ptr<const Revision> to_update =
      transaction->getById(id, table_, chunk_);
  ASSERT_TRUE(to_update != nullptr);
  std::shared_ptr<Revision> update;
  to_update->copyForWrite(&update);
  update->set(kFieldName, new_value);
  transaction->update(table_, update);
}

const std::string NetTableFixture::kTableName = "chunk_test_table";

}  // namespace dmap

#endif  // MAP_API_NET_TABLE_FIXTURE_INL_H_
