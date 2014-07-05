#ifndef MAP_API_CHORD_INDEX_H_
#define MAP_API_CHORD_INDEX_H_

#include "map-api/message.h"
#include "map-api/peer-id.h"

namespace map_api {

/**
 * This is the base class for all distributed indices. It implements distributed
 * key lookup via the Chord protocol, with m fixed to 16 TODO(tcies) flex?.
 * Because multiple indices will be used throughout map api, each index will
 * need to send chord RPC's in a way that the recipients will know what index
 * an RPC belongs to. Consequently, the implementation of RPCs is left to
 * derived classes. Similarly, the holder of derived classes must also ensure
 * proper routing to the handlers.
 * Finally, the first implementation assumes no sporadic loss of connectivity.
 * Consequently, the robustness functions and maintenance tasks (stabilize,
 * notify, fix_fingers, check_predecessor) of chord are left out and replaced
 * with simpler mechanisms (findSuccessorAndFixFinger(), leave()).
 */
class ChordIndex {
 public:
  typedef uint16_t Key; //TODO(tcies) in the long term, public functions
  // shouldn't expose these kinds of typedefs unless e.g. a serialization
  // method is given as well

  // ========
  // HANDLERS
  // ========
  Key handleFindSuccessor(const Key& key);
  PeerId handleGetPredecessor();
  Key handleFindSuccessorAndFixFinger(const Key& query, const Key& finger_base,
                                      PeerId* actual_finger_node);
  void handleLeave(const PeerId& leaver, const PeerId&leaver_predecessor,
                   const PeerId& leaver_successor);

 protected:
  static constexpr size_t M = sizeof(Key) * 8;
  /**
   * Find successor to key, i.e. who holds the information associated with key
   */
  PeerId findSuccessor(const Key& key);

  void create();

  /**
   * Differs from chord in that the successor will directly inform its
   * predecessor about the newly joined node, and will inform the newly joined
   * node about its predecessor.
   */
  void join(const PeerId& other);

  /**
   * Differs from chord in that a leave message is sent around the circle,
   * such that all nodes can remove bad links from their fingers directly.
   */
  void leave();

 private:
  // ======================
  // REQUIRE IMPLEMENTATION
  // ======================
  virtual Key findSuccessorRpc(const PeerId& to, const Key& argument) = 0;
  virtual void getPredecessorRpc(const PeerId& to, PeerId* predecessor) = 0;
  virtual Key findSuccessorAndFixFingerRpc(
      const PeerId& to, const Key& query, const Key& finger_base,
      PeerId* actual_finger_node) = 0;
  virtual bool leaveRpc(
      const PeerId& to, const PeerId& leaver, const PeerId&leaver_predecessor,
      const PeerId& leaver_successor);

  /**
   * Returns index of finger which is counterclockwise closest to key.
   */
  int closestPrecedingFinger(const Key& key) const;
  /**
   * Slight departure from original chord protocol, linked to assumption of no
   * loss of connectivity: findSuccessor and finger fixing in one RPC.
   * Returns finger_[finger_index].node->findSuccessor(query) while checking
   * that finger_[finger_index].node->predecessor.key <
   * finger_[finger_index].key and replacing finger_[finger_index].node with
   * the predecessor otherwise.
   */
  PeerId findSuccessorAndFixFinger(int finger_index, const Key& query);

  bool initialized_ = false;
  std::pair<Key, PeerId> fingers_[M];
};

} /* namespace map_api */

#endif /* MAP_API_CHORD_INDEX_H_ */
