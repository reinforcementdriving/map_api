// Copyright (C) 2014-2017 Titus Cieslewski, ASL, ETH Zurich, Switzerland
// You can contact the author at <titus at ifi dot uzh dot ch>
// Copyright (C) 2014-2015 Simon Lynen, ASL, ETH Zurich, Switzerland
// Copyright (c) 2014-2015, Marcin Dymczyk, ASL, ETH Zurich, Switzerland
// Copyright (c) 2014, Stéphane Magnenat, ASL, ETH Zurich, Switzerland
//
// This file is part of Map API.
//
// Map API is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Map API is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Map API. If not, see <http://www.gnu.org/licenses/>.

#include "map-api/net-table-transaction.h"

#include "map-api/conflicts.h"
#include "map-api/internal/commit-future.h"

DEFINE_bool(map_api_dump_available_chunk_contents, false,
            "Will print all available ids if enabled.");

DEFINE_bool(map_api_blame_updates, false,
            "Print update counts per chunk per table.");

namespace map_api {

NetTableTransaction::NetTableTransaction(const LogicalTime& begin_time,
                                         const Workspace& workspace,
                                         const CommitFutureTree* commit_futures,
                                         NetTable* table)
    : begin_time_(begin_time),
      table_(table),
      workspace_(Workspace::TableInterface(workspace, table)),
      finalized_(false) {
  CHECK_NOTNULL(table);
  CHECK(begin_time < LogicalTime::sample());

  if (commit_futures != nullptr) {
    for (const CommitFutureTree::value_type& chunk_commit_futures :
         *commit_futures) {
      chunk_transactions_[chunk_commit_futures.first] =
          std::shared_ptr<ChunkTransaction>(new ChunkTransaction(
              begin_time, chunk_commit_futures.second.get(),
              chunk_commit_futures.first, table));
    }
  }

  refreshIdToChunkIdMap();
}

void NetTableTransaction::dumpChunk(const ChunkBase* chunk,
                                    ConstRevisionMap* result) {
  CHECK_NOTNULL(chunk);
  if (workspace_.contains(chunk->id())) {
    transactionOf(chunk)->dumpChunk(result);
  } else {
    result->clear();
  }
}

void NetTableTransaction::dumpActiveChunks(ConstRevisionMap* result) {
  CHECK_NOTNULL(result);
  workspace_.forEachChunk([&, this](const ChunkBase& chunk) {
    ConstRevisionMap chunk_revisions;
    dumpChunk(&chunk, &chunk_revisions);
    result->insert(chunk_revisions.begin(), chunk_revisions.end());
  });
}

void NetTableTransaction::insert(ChunkBase* chunk,
                                 std::shared_ptr<Revision> revision) {
  CHECK_NOTNULL(chunk);
  CHECK(!finalized_);
  transactionOf(chunk)->insert(revision);
  CHECK(item_id_to_chunk_id_map_.emplace(revision->getId<map_api_common::Id>(),
                                         chunk->id()).second);
}

void NetTableTransaction::update(std::shared_ptr<Revision> revision) {
  CHECK(!finalized_);
  map_api_common::Id id = revision->getId<map_api_common::Id>();
  CHECK(id.isValid());
  ChunkBase* chunk = chunkOf(id);
  CHECK_NOTNULL(chunk);
  if (revision->getChunkId().isValid()) {
    CHECK_EQ(chunk->id(), revision->getChunkId());
  }
  transactionOf(chunk)->update(revision);
}

void NetTableTransaction::remove(std::shared_ptr<Revision> revision) {
  CHECK(!finalized_);
  ChunkBase* chunk = chunkOf(revision->getId<map_api_common::Id>());
  CHECK_NOTNULL(chunk);
  CHECK_EQ(chunk->id(), revision->getChunkId());
  transactionOf(chunk)->remove(revision);
}

bool NetTableTransaction::commit() {
  lock();
  if (!hasNoConflicts()) {
    unlock();
    return false;
  }
  checkedCommit(LogicalTime::sample());
  unlock();
  return true;
}

void NetTableTransaction::checkedCommit(const LogicalTime& time) {
  if (FLAGS_map_api_blame_updates) {
    std::cout << "Updates in table " << table_->name() << ":" << std::endl;
  }
  for (const TransactionPair& chunk_transaction : chunk_transactions_) {
    chunk_transaction.second->checkedCommit(time);
  }
}

// Deadlocks in lock() are prevented by imposing a global ordering on chunks,
// and have the locks acquired in that order (resource hierarchy solution)
void NetTableTransaction::lock() {
  size_t i = 0u;
  for (const TransactionPair& chunk_transaction : chunk_transactions_) {
    chunk_transaction.first->writeLock();
    ++i;
  }
}

void NetTableTransaction::unlock() {
  for (const TransactionPair& chunk_transaction : chunk_transactions_) {
    chunk_transaction.first->unlock();
  }
}

bool NetTableTransaction::hasNoConflicts() {
  for (const TransactionPair& chunk_transaction : chunk_transactions_) {
    if (!chunk_transaction.second->hasNoConflicts()) {
      return false;
    }
  }
  return true;
}

void NetTableTransaction::merge(
    const std::shared_ptr<NetTableTransaction>& merge_transaction,
    Conflicts* conflicts) {
  CHECK_NOTNULL(merge_transaction.get());
  CHECK_NOTNULL(conflicts);
  conflicts->clear();
  for (const TransactionPair& chunk_transaction : chunk_transactions_) {
    std::shared_ptr<ChunkTransaction> merge_chunk_transaction(
        new ChunkTransaction(merge_transaction->begin_time_, nullptr,
                             chunk_transaction.first, table_));
    Conflicts sub_conflicts;
    chunk_transaction.second->merge(merge_chunk_transaction, &sub_conflicts);
    CHECK_EQ(chunk_transaction.second->numChangedItems(),
             merge_chunk_transaction->numChangedItems() + sub_conflicts.size());
    if (merge_chunk_transaction->numChangedItems() > 0u) {
      merge_transaction->chunk_transactions_.insert(
          std::make_pair(chunk_transaction.first, merge_chunk_transaction));
    }
    if (!sub_conflicts.empty()) {
      conflicts->splice(conflicts->end(), sub_conflicts);
    }
  }
}

size_t NetTableTransaction::numChangedItems() const {
  size_t result = 0;
  for (const TransactionPair& chunk_transaction : chunk_transactions_) {
    result += chunk_transaction.second->numChangedItems();
  }
  return result;
}

void NetTableTransaction::finalize() {
  finalized_ = true;
  for (TransactionPair& chunk_transaction : chunk_transactions_) {
    chunk_transaction.second->finalize();
  }
}

void NetTableTransaction::buildCommitFutureTree(CommitFutureTree* result) {
  CHECK_NOTNULL(result)->clear();
  for (const TransactionMap::value_type& chunk_transaction :
       chunk_transactions_) {
    (*result)[chunk_transaction.first]
        .reset(new internal::CommitFuture(*chunk_transaction.second));
  }
}

void NetTableTransaction::detachFutures() {
  for (TransactionPair& chunk_transaction : chunk_transactions_) {
    chunk_transaction.second->detachFuture();
  }
}

ChunkTransaction* NetTableTransaction::transactionOf(const ChunkBase* chunk)
    const {
  CHECK_NOTNULL(chunk);
  // Const cast needed, as transactions map has non-const key.
  ChunkBase* mutable_chunk = const_cast<ChunkBase*>(chunk);
  TransactionMap::iterator chunk_transaction =
      chunk_transactions_.find(mutable_chunk);
  if (chunk_transaction == chunk_transactions_.end()) {
    CHECK(!finalized_);
    std::shared_ptr<ChunkTransaction> transaction(
        new ChunkTransaction(begin_time_, nullptr, mutable_chunk, table_));
    std::pair<TransactionMap::iterator, bool> inserted =
        chunk_transactions_.insert(std::make_pair(mutable_chunk, transaction));
    CHECK(inserted.second);
    chunk_transaction = inserted.first;
  }
  return chunk_transaction->second.get();
}

void NetTableTransaction::refreshIdToChunkIdMap() {
  CHECK(!finalized_);
  item_id_to_chunk_id_map_.clear();
  if (FLAGS_map_api_dump_available_chunk_contents) {
    std::cout << table_->name() << " chunk contents:" << std::endl;
  }
  workspace_.forEachChunk([&, this](const ChunkBase& chunk) {
    std::vector<map_api_common::Id> chunk_result;
    chunk.constData()->getAvailableIds(begin_time_, &chunk_result);
    if (FLAGS_map_api_dump_available_chunk_contents) {
      std::cout << "\tChunk " << chunk.id().hexString() << ":" << std::endl;
    }
    for (const map_api_common::Id& item_id : chunk_result) {
      CHECK(item_id_to_chunk_id_map_.emplace(item_id, chunk.id()).second)
          << table_->name() << " has redundant item id " << item_id;
      if (FLAGS_map_api_dump_available_chunk_contents) {
        std::cout << "\t\tItem " << item_id.hexString() << std::endl;
      }
    }
  });
}

void NetTableTransaction::getChunkTrackers(
    TrackedChunkToTrackersMap* chunk_trackers) const {
  CHECK_NOTNULL(chunk_trackers);
  for (const TransactionMap::value_type& chunk_transaction :
       chunk_transactions_) {
    chunk_transaction.second->getTrackers(
        push_new_chunk_ids_to_tracker_overrides_,
        &(*chunk_trackers)[chunk_transaction.first->id()]);
  }
}

} // namespace map_api
