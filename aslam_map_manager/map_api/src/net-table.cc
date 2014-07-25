#include "map-api/net-table.h"

#include <glog/logging.h>

#include "map-api/cr-table-ram-cache.h"
#include "map-api/cru-table-ram-cache.h"
#include "map-api/map-api-core.h"
#include "map-api/net-table-manager.h"

namespace map_api {

const std::string NetTable::kChunkIdField = "chunk_id";

bool NetTable::init(
    CRTable::Type type, std::unique_ptr<TableDescriptor>* descriptor) {
  type_ = type;
  (*descriptor)->addField<Id>(kChunkIdField);
  switch (type) {
    case CRTable::Type::CR:
      cache_.reset(new CRTableRAMCache);
      break;
    case CRTable::Type::CRU:
      cache_.reset(new CRUTableRAMCache);
      break;
  }
  CHECK(cache_->init(descriptor));
  return true;
}

void NetTable::createIndex() {
  CHECK(index_.get() == nullptr);
  index_.reset(new NetTableIndex(name()));
  index_->create();
}

void NetTable::joinIndex(const PeerId& entry_point) {
  CHECK(index_.get() == nullptr);
  index_.reset(new NetTableIndex(name()));
  index_->join(entry_point);
}

const std::string& NetTable::name() const {
  return cache_->name();
}

std::shared_ptr<Revision> NetTable::getTemplate() const {
  return cache_->getTemplate();
}

Chunk* NetTable::newChunk() {
  Id chunk_id = Id::generate();
  return newChunk(chunk_id);
}

Chunk* NetTable::newChunk(const Id& chunk_id) {
  std::unique_ptr<Chunk> chunk = std::unique_ptr<Chunk>(new Chunk);
  CHECK(chunk->init(chunk_id, cache_.get()));
  active_chunks_lock_.writeLock();
  std::pair<ChunkMap::iterator, bool> inserted =
      active_chunks_.insert(std::make_pair(chunk_id, std::unique_ptr<Chunk>()));
  CHECK(inserted.second);
  inserted.first->second = std::move(chunk);
  active_chunks_lock_.unlock();
  // add self to chunk posessors in index
  CHECK_NOTNULL(index_.get());
  index_->announcePosession(chunk_id);
  return inserted.first->second.get();
}

Chunk* NetTable::getChunk(const Id& chunk_id) {
  CHECK_NOTNULL(index_.get());
  active_chunks_lock_.readLock();
  ChunkMap::iterator found = active_chunks_.find(chunk_id);
  if (found == active_chunks_.end()) {
    // look in index and connect to peers that claim to have the data
    // (for now metatable only)
    std::unordered_set<PeerId> peers;
    index_->seekPeers(chunk_id, &peers);
    CHECK_EQ(1u, peers.size()) << "Current implementation expects root only";
    active_chunks_lock_.unlock();
    connectTo(chunk_id, *peers.begin());
    active_chunks_lock_.readLock();
    found = active_chunks_.find(chunk_id);
    CHECK(found != active_chunks_.end());
  }
  Chunk* result = found->second.get();
  active_chunks_lock_.unlock();
  return result;
}

bool NetTable::insert(Chunk* chunk, Revision* query) {
  CHECK_NOTNULL(chunk);
  CHECK_NOTNULL(query);
  CHECK(chunk->insert(query));
  return true;
}

bool NetTable::update(Revision* query) {
  CHECK_NOTNULL(query);
  CHECK(type_ == CRTable::Type::CRU);
  Id chunk_id;
  query->get(kChunkIdField, &chunk_id);
  CHECK_NOTNULL(getChunk(chunk_id))->update(query);
  return true;
}

// TODO(tcies) net lookup
std::shared_ptr<Revision> NetTable::getById(const Id& id,
                                            const LogicalTime& time) {
  return cache_->getById(id, time);
}

void NetTable::dumpCache(
    const LogicalTime& time,
    std::unordered_map<Id, std::shared_ptr<Revision> >* destination) {
  CHECK_NOTNULL(destination);
  // TODO(tcies) lock cache access
  cache_->dump(time, destination);
}

bool NetTable::has(const Id& chunk_id) {
  bool result;
  active_chunks_lock_.readLock();
  result = (active_chunks_.find(chunk_id) != active_chunks_.end());
  active_chunks_lock_.unlock();
  return result;
}

Chunk* NetTable::connectTo(const Id& chunk_id,
                           const PeerId& peer) {
  Message request, response;
  // sends request of chunk info to peer
  proto::ChunkRequestMetadata metadata;
  metadata.set_table(cache_->name());
  metadata.set_chunk_id(chunk_id.hexString());
  request.impose<Chunk::kConnectRequest>(metadata);
  // TODO(tcies) add to local peer subset as well?
  MapApiHub::instance().request(peer, &request, &response);
  CHECK(response.isType<Message::kAck>());
  // wait for connect handle thread of other peer to succeed
  ChunkMap::iterator found;
  while (true) {
    active_chunks_lock_.readLock();
    found = active_chunks_.find(chunk_id);
    if (found != active_chunks_.end()) {
      active_chunks_lock_.unlock();
      break;
    }
    active_chunks_lock_.unlock();
    usleep(1000);
  }
  return found->second.get();
}

size_t NetTable::activeChunksSize() const {
  return active_chunks_.size();
}

size_t NetTable::cachedItemsSize() {
  CRTable::RevisionMap result;
  dumpCache(LogicalTime::sample(), &result);
  return result.size();
}

void NetTable::shareAllChunks() {
  active_chunks_lock_.readLock();
  for (const std::pair<const Id, std::unique_ptr<Chunk> >& chunk :
      active_chunks_) {
    chunk.second->requestParticipation();
  }
  active_chunks_lock_.unlock();
}

void NetTable::kill() {
  leaveAllChunks();
  if (index_.get() != nullptr) {
    index_->leave();
    index_.reset();
  }
}

void NetTable::leaveAllChunks() {
  active_chunks_lock_.readLock();
  for (const std::pair<const Id, std::unique_ptr<Chunk> >& chunk :
      active_chunks_) {
    chunk.second->leave();
  }
  active_chunks_lock_.unlock();
  active_chunks_lock_.writeLock();
  active_chunks_.clear();
  active_chunks_lock_.unlock();
}

std::string NetTable::getStatistics() {
  std::stringstream ss;

  // TODO(tcies) more lightweight item count method
  ss << name() << ": " << activeChunksSize() << " chunks and " <<
      cachedItemsSize() << " items.";
  return ss.str();
}

void NetTable::handleConnectRequest(const Id& chunk_id, const PeerId& peer,
                                    Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.readLock();
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleConnectRequest(peer, response);
  }
  active_chunks_lock_.unlock();
}

void NetTable::handleInitRequest(
    const proto::InitRequest& request, const PeerId& sender,
    Message* response) {
  CHECK_NOTNULL(response);
  Id chunk_id;
  CHECK(chunk_id.fromHexString(request.metadata().chunk_id()));
  std::unique_ptr<Chunk> chunk = std::unique_ptr<Chunk>(new Chunk);
  CHECK(chunk->init(chunk_id, request, sender, cache_.get()));
  active_chunks_lock_.writeLock();
  std::pair<ChunkMap::iterator, bool> inserted =
      active_chunks_.insert(std::make_pair(chunk_id, std::unique_ptr<Chunk>()));
  CHECK(inserted.second);
  inserted.first->second = std::move(chunk);
  active_chunks_lock_.unlock();
  response->ack();
}

void NetTable::handleInsertRequest(
    const Id& chunk_id, const Revision& item, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.readLock();
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleInsertRequest(item, response);
  }
  active_chunks_lock_.unlock();
}

void NetTable::handleLeaveRequest(
    const Id& chunk_id, const PeerId& leaver, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.readLock();
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleLeaveRequest(leaver, response);
  }
  active_chunks_lock_.unlock();
}

void NetTable::handleLockRequest(
    const Id& chunk_id, const PeerId& locker, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.readLock();
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleLockRequest(locker, response);
  }
  active_chunks_lock_.unlock();
}

void NetTable::handleNewPeerRequest(
    const Id& chunk_id, const PeerId& peer, const PeerId& sender,
    Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.readLock();
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleNewPeerRequest(peer, sender, response);
  }
  active_chunks_lock_.unlock();
}

void NetTable::handleUnlockRequest(
    const Id& chunk_id, const PeerId& locker, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.readLock();
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleUnlockRequest(locker, response);
  }
  active_chunks_lock_.unlock();
}

void NetTable::handleUpdateRequest(
    const Id& chunk_id, const Revision& item, const PeerId& sender,
    Message* response) {
  ChunkMap::iterator found;
  if (routingBasics(chunk_id, response, &found)) {
    found->second->handleUpdateRequest(item, sender, response);
  }
}

void NetTable::handleRoutedChordRequests(
    const Message& request, Message* response) {
  CHECK_NOTNULL(index_.get());
  index_->handleRoutedRequest(request, response);
}

bool NetTable::routingBasics(
    const Id& chunk_id, Message* response, ChunkMap::iterator* found) {
  CHECK_NOTNULL(response);
  CHECK_NOTNULL(found);
  *found = active_chunks_.find(chunk_id);
  if (*found == active_chunks_.end()) {
    response->impose<Message::kDecline>();
    return false;
  }
  return true;
}

} // namespace map_api
