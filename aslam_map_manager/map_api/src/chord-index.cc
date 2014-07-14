#include "map-api/chord-index.h"

#include <type_traits>

#include <glog/logging.h>

#include <Poco/DigestStream.h>
#include <Poco/MD5Engine.h>

namespace map_api {

ChordIndex::~ChordIndex() {}

PeerId ChordIndex::handleFindSuccessor(const Key& key) {
  CHECK(initialized_);
  return findSuccessor(key);
}

PeerId ChordIndex::handleGetPredecessor() {
  CHECK(initialized_);
  std::lock_guard<std::mutex> lock(peer_access_);
  return predecessor_->id;
}

void ChordIndex::handleNotify(const PeerId& peer_id) {
  std::lock_guard<std::mutex> lock(peer_access_);
  if (peers_.find(peer_id) != peers_.end()) {
    // already aware of the node
    return;
  }
  std::shared_ptr<ChordPeer> peer(new ChordPeer(peer_id));
  // fix fingers
  for (size_t i = 0; i < M; ++i) {
    if (isIn(peer->key, fingers_[i].base_key, fingers_[i].peer->key)) {
      fingers_[i].peer = peer;
      // no break intended: multiple fingers can have same peer
    }
  }
  // fix successors TODO(tcies) later
  /*
  for (size_t i = 0; i < kSuccessorListSize; ++i) {
    bool condition = false;
    if (i == 0 && isIn(peer->key, own_key_, successors_[0]->key)) {
      condition = true;
    }
    if (i != 0 && isIn(peer->key, successors_[i-1]->key, successors_[i]->key)) {
      condition = true;
    }
    if (condition) {
      for (size_t j = kSuccessorListSize - 1; j > i; j--) {
        successors_[j] = successors_[j - 1];
      }
      successors_[i] = peer;
      break;
    }
  }
   */
  if (isIn(peer->key, own_key_, successor_->key)) {
    successor_ = peer;
  }
  // fix predecessor
  if (isIn(peer->key, predecessor_->key, own_key_)) {
    predecessor_ = peer;
  }
  // save peer to peer map only if information has been useful anywhere
  if (peer.use_count() > 1) {
    peers_[peer_id] = std::weak_ptr<ChordPeer>(peer);
    // TODO(tcies) how will it be removed?
  }
}

PeerId ChordIndex::findSuccessor(const Key& key) {
  if (isIn(key, own_key_, successor_->key)) {
    return successor_->id;
  } else {
    LOG(INFO) << 1;
    std::shared_ptr<ChordPeer> closest_preceding = closestPrecedingFinger(key);
    LOG(INFO) << 1;
    PeerId result;
    LOG(INFO) << 1;
    // TODO(tcies) handle closest preceding doesn't respond
    CHECK(findSuccessorRpc(closest_preceding->id, key, &result));
    LOG(INFO) << 1;
    return result;
  }
}

void ChordIndex::create() {
  init();
  for (size_t i = 0; i < M; ++i) {
    fingers_[i].peer = self_;
  }
  successor_ = self_;
  predecessor_ = self_;
  initialized_ = true;
}

void ChordIndex::join(const PeerId& other) {
  init();
  for (size_t i = 0; i < M; ++i) {
    PeerId finger;
    CHECK(findSuccessorRpc(other, fingers_[i].base_key, &finger));
    registerPeer(finger, &fingers_[i].peer);
  }
  successor_ = fingers_[0].peer;
  PeerId predecessor;
  CHECK(getPredecessorRpc(successor_->id, &predecessor));
  Key predecessor_key = hash(predecessor);
  CHECK(predecessor_key != own_key_);
  registerPeer(predecessor, &predecessor_);

  initialized_ = true;
  CHECK(notifyRpc(predecessor_->id, PeerId::self()));
  CHECK(notifyRpc(successor_->id, PeerId::self()));
}

void ChordIndex::leave() {
  terminate_ = true;
  // TODO(tcies) move data to successor
  initialized_ = false;
}

std::shared_ptr<ChordIndex::ChordPeer> ChordIndex::closestPrecedingFinger(
    const Key& key) const {
  // TODO(tcies) verify corner cases
  LOG(WARNING) << "Corner cases not verified";
  for (size_t i = 0; i < M; ++i) {
    size_t index = M - 1 - i;
    Key actual_key = fingers_[index].peer->key;
    if (isIn(actual_key, own_key_, key)) {
      return fingers_[index].peer;
    }
  }
  LOG(FATAL) << "Called closest preceding finger on key which is smaller " <<
      "than successor key";
  return std::shared_ptr<ChordIndex::ChordPeer>();
}

ChordIndex::Key ChordIndex::hash(const PeerId& id) {
  // TODO(tcies) better method?
  Poco::MD5Engine md5;
  Poco::DigestOutputStream digest_stream(md5);
  digest_stream << id;
  digest_stream.flush();
  const Poco::DigestEngine::Digest& digest = md5.digest();
  bool diges_still_uchar_vec =
      std::is_same<
      Poco::DigestEngine::Digest, std::vector<unsigned char> >::value;
  CHECK(diges_still_uchar_vec) <<
      "Underlying type of Digest changed since Poco 1.3.6";
  union KeyUnion {
    Key key;
    unsigned char bytes[sizeof(Key)];
  };
  CHECK_EQ(sizeof(Key), sizeof(KeyUnion));
  KeyUnion return_value;
  for (size_t i = 0; i < sizeof(Key); ++i) {
    return_value.bytes[i] = digest[i];
  }
  return return_value.key;
}

void ChordIndex::init() {
  LOG(INFO) << "Initializing chord for " << PeerId::self();
  own_key_ = hash(PeerId::self());
  self_.reset(new ChordPeer(PeerId::self()));
  LOG(INFO) << "Self key is " << self_->key;
  for (size_t i = 0; i < M; ++i) {
    fingers_[i].base_key = own_key_ + (1 << i); // overflow intended
  }
}

void ChordIndex::registerPeer(
    const PeerId& peer, std::shared_ptr<ChordPeer>* target) {
  CHECK_NOTNULL(target);
  PeerMap::iterator found = peers_.find(peer);
  if (found == peers_.end()){
    target->reset(new ChordPeer(peer));
    peers_[peer] = std::weak_ptr<ChordPeer>(*target);
  } else {
    std::shared_ptr<ChordPeer> existing = found->second.lock();
    CHECK(existing);
    *target = existing;
  }
}

bool ChordIndex::isIn(
    const Key& key, const Key& from_inclusive, const Key& to_exclusive) const {
  if (key == from_inclusive) {
    return true;
  }
  if (to_exclusive == from_inclusive) {
    return true;
  }
  if (from_inclusive <= to_exclusive) { // case doesn't pass 0
    return (from_inclusive < key && key < to_exclusive);
  } else { // case passes 0
    return (from_inclusive < key || key < to_exclusive);
  }
}

} /* namespace map_api */
