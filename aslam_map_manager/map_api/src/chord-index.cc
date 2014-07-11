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
  std::shared_ptr<ChordPeer> peer(new ChordPeer(peer_id, hash(peer_id)));
  // fix fingers
  for (size_t i = 0; i < M; ++i) {
    if (isIn(peer->key, fingers_[i].base_key, fingers_[i].peer->key)) {
      fingers_[i].peer = peer;
      // no break intended: multiple fingers can have same peer
    }
  }
  // fix successors
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
  // fix predecessor
  if (isIn(peer->key, predecessor_->key, own_key_)) {
    predecessor_ = peer;
  }
  // save peer to peer map only if information has been useful anywhere
  if (peer.use_count() > 1) {
    peers_[peer_id] = std::weak_ptr<ChordPeer>(peer);
  }
}

PeerId ChordIndex::findSuccessor(const Key& key) {
  if (isIn(key, own_key_, successors_[0]->key)) {
    return successors_[0]->id;
  } else {
    PeerId closest_preceding = closestPrecedingFinger(key);
    PeerId result;
    // TODO(tcies) handle closest preceding doesn't respond
    CHECK(findSuccessorRpc(closest_preceding, key, &result));
    return result;
  }
}

void ChordIndex::create() {
  init();
  for (size_t i = 0; i < M; ++i) {
    fingers_[i].second = PeerId::self();
  }
  predecessor_ = std::make_pair(own_key_, PeerId::self());
  initialized_ = true;
}

void ChordIndex::join(const PeerId& other) {
  init();
  for (size_t i = 0; i < M; ++i) {
    PeerId finger = findSuccessorRpc(other, fingers_[i].first);
    fingers_[i].second = finger;
  }
  PeerId predecessor = getPredecessorRpc(successor_.second);
  Key predecessor_key = hash(predecessor);
  CHECK(predecessor_key != own_key_);
  predecessor_ = std::make_pair(predecessor_key, predecessor);

  initialized_ = true;
  notifyPredecessorRpc(predecessor_.second, PeerId::self());
  notifySuccessorRpc(successor_.second, PeerId::self());
}

void ChordIndex::leave() {
  leaving_ = true;
  leaveRpc(successor_.second, PeerId::self(), predecessor_.second,
           successor_.second);
  // TODO(tcies) move data to successor
  initialized_ = false;
}

PeerId ChordIndex::closestPrecedingFinger(const Key& key) const {
  // TODO(tcies) verify corner cases
  CHECK(false) << "Corner cases not verified";
  for (size_t i = 0; i < M; ++i) {
    size_t index = M - 1 - i;
    Key actual_key = key(fingers_[index].second);
    if (isIn(actual_key, own_key_, key)) {
      return index;
    }
  }
  LOG(FATAL) << "Called closest preceding finger on key which is smaller " <<
      "than successor key";
}

PeerId ChordIndex::findSuccessorAndFixFinger(
    int finger_index, const Key& query) {
  PeerId better_finger_node, response;
  response = findSuccessorAndFixFingerRpc(
      fingers_[finger_index].second, query, fingers_[finger_index].first,
      &better_finger_node);
  fingers_[finger_index].second = better_finger_node;
  return response;
}

ChordIndex::Key ChordIndex::key(const PeerId& id) {
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
  LOG(INFO) << PeerId::self();
  own_key_ = hash(PeerId::self());
  for (size_t i = 0; i < M; ++i) {
    fingers_[i].first = own_key_ + (1 << i); // overflow intended
  }
}

bool ChordIndex::isIn(
    const Key& key, const Key& from_inclusive, const Key& to_exclusive) const {
  if (key == from_inclusive) {
    return true;
  }
  if (from_inclusive <= to_exclusive) { // case doesn't pass 0
    return (from_inclusive < key && key < to_exclusive);
  } else { // case passes 0
    return (from_inclusive < key || key < to_exclusive);
  }
}

} /* namespace map_api */
