#include <map-api/raft-chunk.h>

#include <multiagent-mapping-common/conversions.h>

#include "./core.pb.h"
#include "./chunk.pb.h"
#include "map-api/legacy-chunk-data-ram-container.h"
#include "map-api/raft-node.h"
#include "map-api/hub.h"
#include "map-api/message.h"

namespace map_api {

RaftChunk::~RaftChunk() {
  raft_node_.stop();
}

bool RaftChunk::init(const common::Id& id,
                     std::shared_ptr<TableDescriptor> descriptor,
                     bool initialize) {
  id_ = id;
  // TODO(aqurai): init new data container here.
  data_container_.reset(new LegacyChunkDataRamContainer);
  CHECK(data_container_->init(descriptor));
  initialized_ = true;
  raft_node_.chunk_id_ = id_;
  raft_node_.table_name_ = descriptor->name();
  return true;
}

void RaftChunk::initializeNewImpl(
    const common::Id& id, const std::shared_ptr<TableDescriptor>& descriptor) {
  CHECK(init(id, descriptor, true));

  VLOG(1) << " INIT chunk at peer " << PeerId::self() << " in table "
          << raft_node_.table_name_;
  setStateFollowerAndStartRaft();
}

bool RaftChunk::init(const common::Id& id,
                     std::shared_ptr<TableDescriptor> descriptor) {
  CHECK(init(id, descriptor, true));
  setStateFollowerAndStartRaft();
  return true;
}

bool RaftChunk::init(const common::Id& id,
                     const proto::InitRequest& init_request,
                     std::shared_ptr<TableDescriptor> descriptor) {
  CHECK(init(id, descriptor, true));

  VLOG(1) << " INIT chunk at peer " << PeerId::self() << " in table "
          << raft_node_.table_name_;

  raft_node_.peer_list_.clear();
  raft_node_.log_entries_.clear();
  for (int i = 0; i < init_request.peer_address_size(); ++i) {
    raft_node_.peer_list_.insert(PeerId(init_request.peer_address(i)));
  }
  raft_node_.num_peers_ = raft_node_.peer_list_.size();
  for (int i = 0; i < init_request.serialized_items_size(); ++i) {
    std::shared_ptr<proto::RaftLogEntry> revision(new proto::RaftLogEntry);
    revision->ParseFromString(init_request.serialized_items(i));
    raft_node_.log_entries_.push_back(revision);
  }
  setStateFollowerAndStartRaft();
  return true;
}

void RaftChunk::dumpItems(const LogicalTime& time, ConstRevisionMap* items) const {
  CHECK_NOTNULL(items);
  data_container_->dump(time, items);
}

void RaftChunk::setStateFollowerAndStartRaft() {
  raft_node_.state_ = RaftNode::State::FOLLOWER;
  VLOG(1) << PeerId::self() << ": Starting Raft node as follower for chunk "
          << id_.printString();
  raft_node_.start();
}

bool RaftChunk::sendConnectRequest(const PeerId& peer,
                                   proto::ChunkRequestMetadata& metadata) {
  Message request, response;
  proto::ConnectResponse connect_response;
  connect_response.set_index(0);
  request.impose<RaftNode::kConnectRequest>(metadata);

  // TODO(aqurai): Avoid infinite loop. Use Chord index to get chunk holder
  // if request fails.
  PeerId request_peer = peer;
  while (connect_response.index() == 0) {
    if (!(Hub::instance().try_request(request_peer, &request, &response))) {
      break;
    }
    response.extract<RaftNode::kConnectResponse>(&connect_response);
    if (connect_response.index() > 0) {
      return true;
    } else if (connect_response.has_leader_id()) {
      request_peer = PeerId(connect_response.leader_id());
    }
    usleep(1000);
  }
  return false;
}

uint64_t RaftChunk::insertRequest(const Revision::ConstPtr& item) {
  CHECK(raft_node_.isRunning());
  return raft_node_.sendInsertRequest(item);
}

void RaftChunk::handleRaftConnectRequest(const PeerId& sender, Message* response) {
  CHECK_NOTNULL(response);
  raft_node_.handleConnectRequest(sender, response);
}

void RaftChunk::handleRaftAppendRequest(proto::AppendEntriesRequest* request,
                                        const PeerId& sender,
                                        Message* response) {
  CHECK_NOTNULL(response);
  raft_node_.handleAppendRequest(request, sender, response);
}

void RaftChunk::handleRaftInsertRequest(proto::InsertRequest* request,
                                        const PeerId& sender,
                                        Message* response) {
  CHECK_NOTNULL(response);
  raft_node_.handleInsertRequest(request, sender, response);
}


void RaftChunk::handleRaftRequestVote(const proto::VoteRequest& request,
                                      const PeerId& sender, Message* response) {
  CHECK_NOTNULL(response);
  raft_node_.handleRequestVote(request, sender, response);
}

void RaftChunk::handleRaftQueryState(const proto::QueryState& request,
                                     Message* response) {
  CHECK_NOTNULL(response);
  raft_node_.handleQueryState(request, response);
}

void RaftChunk::handleRaftJoinQuitRequest(const proto::JoinQuitRequest& request,
                                          const PeerId& sender,
                                          Message* response) {
  CHECK_NOTNULL(response);
  raft_node_.handleJoinQuitRequest(request, sender, response);
}

void RaftChunk::handleRaftNotifyJoinQuitSuccess(
    const proto::NotifyJoinQuitSuccess& request, Message* response) {
  raft_node_.handleNotifyJoinQuitSuccess(request, response);
}

}  // namespace map_api
