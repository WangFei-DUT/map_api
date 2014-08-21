#include "map-api/chunk-transaction.h"

#include <unordered_set>

#include "map-api/cru-table.h"

namespace map_api {

ChunkTransaction::ChunkTransaction(Chunk* chunk)
    : ChunkTransaction(LogicalTime::sample(), chunk) {}

ChunkTransaction::ChunkTransaction(const LogicalTime& begin_time, Chunk* chunk)
    : begin_time_(begin_time), chunk_(CHECK_NOTNULL(chunk)) {
  CHECK(begin_time < LogicalTime::sample());
  insertions_.clear();
  updates_.clear();
  structure_reference_ = chunk_->underlying_table_->getTemplate();
}

std::shared_ptr<Revision> ChunkTransaction::getById(const Id& id) {
  std::shared_ptr<Revision> result = getByIdFromUncommitted(id);
  if (result != nullptr) {
    return result;
  }
  chunk_->readLock();
  result = chunk_->underlying_table_->getById(id, begin_time_);
  chunk_->unlock();
  return result;
}

std::shared_ptr<Revision> ChunkTransaction::getByIdFromUncommitted(const Id& id)
    const {
  UpdateMap::const_iterator updated = updates_.find(id);
  if (updated != updates_.end()) {
    return updated->second;
  }
  InsertMap::const_iterator inserted = insertions_.find(id);
  if (inserted != insertions_.end()) {
    return inserted->second;
  }
  return std::shared_ptr<Revision>();
}

CRTable::RevisionMap ChunkTransaction::dumpChunk() {
  CRTable::RevisionMap result;
  chunk_->dumpItems(begin_time_, &result);
  return result;
}

void ChunkTransaction::insert(std::shared_ptr<Revision> revision) {
  CHECK_NOTNULL(revision.get());
  CHECK(revision->structureMatch(*structure_reference_));
  Id id;
  revision->get(CRTable::kIdField, &id);
  CHECK(insertions_.insert(std::make_pair(id, revision)).second);
}

void ChunkTransaction::update(std::shared_ptr<Revision> revision) {
  CHECK_NOTNULL(revision.get());
  CHECK(revision->structureMatch(*structure_reference_));
  CHECK(chunk_->underlying_table_->type() == CRTable::Type::CRU);
  Id id;
  revision->get(CRTable::kIdField, &id);
  CHECK(updates_.insert(std::make_pair(id, revision)).second);
}

bool ChunkTransaction::commit() {
  chunk_->writeLock();
  if (!check()) {
    chunk_->unlock();
    return false;
  }
  checkedCommit(LogicalTime::sample());
  chunk_->unlock();
  return true;
}

bool ChunkTransaction::check() {
  CHECK(chunk_->isLocked());
  std::unordered_map<Id, LogicalTime> stamps;
  prepareCheck(&stamps);
  // The following check may be left out if too costly
  for (const std::pair<const Id, std::shared_ptr<Revision> >& item :
       insertions_) {
    if (stamps.find(item.first) != stamps.end()) {
      LOG(ERROR) << "Table " << chunk_->underlying_table_->name()
                 << " already contains id " << item.first;
      return false;
    }
  }
  for (const std::pair<const Id, std::shared_ptr<Revision> >& item : updates_) {
    if (stamps[item.first] >= begin_time_) {
      return false;
    }
  }
  for (const ChunkTransaction::ConflictCondition& item : conflict_conditions_) {
    CRTable::RevisionMap dummy;
    if (chunk_->underlying_table_->findByRevision(
            item.key, *item.value_holder, LogicalTime::sample(), &dummy) > 0) {
      return false;
    }
  }
  return true;
}

void ChunkTransaction::checkedCommit(const LogicalTime& time) {
  chunk_->bulkInsertLocked(insertions_, time);
  for (const std::pair<const Id, std::shared_ptr<Revision> >& item : updates_) {
    chunk_->updateLocked(time, item.second.get());
  }
}

void ChunkTransaction::merge(
    const LogicalTime& time,
    std::shared_ptr<ChunkTransaction>* merge_transaction,
    Conflicts* conflicts) {
  CHECK_NOTNULL(merge_transaction);
  CHECK_NOTNULL(conflicts);
  CHECK(conflict_conditions_.empty()) << "merge not compatible with conflict "
                                         "conditions";
  merge_transaction->reset(new ChunkTransaction(time, chunk_));
  conflicts->clear();
  CHECK(false);
  // TODO(tcies) the following code is similar to check, but I don't see how to
  // make it lambda-able, since check can return inside a loop
  chunk_->readLock();
  std::unordered_map<Id, LogicalTime> stamps;
  prepareCheck(&stamps);
  // The following check may be left out if too costly
  for (const std::pair<const Id, std::shared_ptr<Revision> >& item :
       insertions_) {
    CHECK(stamps.find(item.first) == stamps.end()) << "Insert conflict!";
    merge_transaction->get()->insertions_.insert(item);
  }
  for (const std::pair<const Id, std::shared_ptr<Revision> >& item : updates_) {
    if (stamps[item.first] >= begin_time_) {
      conflicts->push_back(
          {merge_transaction->get()->getById(item.first), item.second});
    } else {
      merge_transaction->get()->updates_.insert(item);
    }
  }
  chunk_->unlock();
}

size_t ChunkTransaction::changeCount() const {
  CHECK(conflict_conditions_.empty()) << "changeCount not compatible with "
                                         "conflict conditions";
  return insertions_.size() + updates_.size();
}

void ChunkTransaction::prepareCheck(
    std::unordered_map<Id, LogicalTime>* chunk_stamp) {
  CHECK_NOTNULL(chunk_stamp);
  chunk_stamp->clear();
  CRTable::RevisionMap contents;
  chunk_->dumpItems(LogicalTime::sample(), &contents);
  LogicalTime time;
  if (!updates_.empty()) {
    CHECK(chunk_->underlying_table_->type() == CRTable::Type::CRU);
    for (const CRTable::RevisionMap::value_type& item : contents) {
      item.second->get(CRUTable::kUpdateTimeField, &time);
      chunk_stamp->insert(std::make_pair(item.first, time));
    }
  } else {
    for (const CRTable::RevisionMap::value_type& item : contents) {
      chunk_stamp->insert(std::make_pair(item.first, time));
    }
  }
}

} /* namespace map_api */
