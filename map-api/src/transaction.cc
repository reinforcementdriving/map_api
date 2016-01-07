#include <map-api/legacy-chunk.h>
#include "map-api/transaction.h"

#include <algorithm>
#include <future>

#include <multiagent-mapping-common/backtrace.h>
#include <timing/timer.h>

#include "map-api/cache-base.h"
#include "map-api/chunk-manager.h"
#include "map-api/conflicts.h"
#include "map-api/net-table.h"
#include "map-api/net-table-manager.h"
#include "map-api/net-table-transaction.h"
#include "map-api/revision.h"
#include "map-api/trackee-multimap.h"
#include "map-api/workspace.h"
#include "./core.pb.h"
#include "./raft.pb.h"

DECLARE_bool(cache_blame_dirty);
DECLARE_bool(cache_blame_insert);
DEFINE_bool(blame_commit, false, "Print stack trace for every commit");

namespace map_api {

Transaction::Transaction(const std::shared_ptr<Workspace>& workspace,
                         const LogicalTime& begin_time)
    : workspace_(workspace),
      begin_time_(begin_time),
      chunk_tracking_disabled_(false) {
  CHECK(begin_time < LogicalTime::sample());
}
Transaction::Transaction()
    : Transaction(std::shared_ptr<Workspace>(new Workspace),
                  LogicalTime::sample()) {}
Transaction::Transaction(const std::shared_ptr<Workspace>& workspace)
    : Transaction(workspace, LogicalTime::sample()) {}
Transaction::Transaction(const LogicalTime& begin_time)
    : Transaction(std::shared_ptr<Workspace>(new Workspace), begin_time) {}

void Transaction::dumpChunk(NetTable* table, ChunkBase* chunk,
                            ConstRevisionMap* result) {
  CHECK_NOTNULL(table);
  CHECK_NOTNULL(chunk);
  CHECK_NOTNULL(result);
  if (!workspace_->contains(table, chunk->id())) {
    result->clear();
  } else {
    transactionOf(table)->dumpChunk(chunk, result);
  }
}

void Transaction::dumpActiveChunks(NetTable* table, ConstRevisionMap* result) {
  CHECK_NOTNULL(table);
  CHECK_NOTNULL(result);
  if (!workspace_->contains(table)) {
    result->clear();
  } else {
    transactionOf(table)->dumpActiveChunks(result);
  }
}

bool Transaction::fetchAllChunksTrackedByItemsInTable(NetTable* const table) {
  CHECK_NOTNULL(table);
  std::vector<common::Id> item_ids;
  enableDirectAccess();
  getAvailableIds(table, &item_ids);

  bool success = true;
  for (const common::Id& item_id : item_ids) {
    if (!getById(item_id, table)->fetchTrackedChunks()) {
      success = false;
    }
  }
  disableDirectAccess();
  refreshIdToChunkIdMaps();
  // Id to chunk id maps must be refreshed first, otherwise getAvailableIds will
  // nor work.
  refreshAvailableIdsInCaches();
  return success;
}

void Transaction::insert(NetTable* table, ChunkBase* chunk,
                         std::shared_ptr<Revision> revision) {
  CHECK_NOTNULL(table);
  CHECK_NOTNULL(chunk);
  transactionOf(table)->insert(chunk, revision);
}

void Transaction::insert(ChunkManagerBase* chunk_manager,
                         std::shared_ptr<Revision> revision) {
  CHECK_NOTNULL(chunk_manager);
  CHECK(revision != nullptr);
  NetTable* table = chunk_manager->getUnderlyingTable();
  CHECK_NOTNULL(table);
  ChunkBase* chunk = chunk_manager->getChunkForItem(*revision);
  CHECK_NOTNULL(chunk);
  insert(table, chunk, revision);
}

void Transaction::update(NetTable* table, std::shared_ptr<Revision> revision) {
  CHECK_NOTNULL(table);
  transactionOf(table)->update(revision);
}

void Transaction::remove(NetTable* table, std::shared_ptr<Revision> revision) {
  transactionOf(CHECK_NOTNULL(table))->remove(revision);
}

void Transaction::prepareForCommit() {
  if (FLAGS_blame_commit) {
    LOG(INFO) << "Transaction committed from:\n" << common::backtrace();
  }
  for (const CacheMap::value_type& cache_pair : caches_) {
    if (FLAGS_cache_blame_dirty || FLAGS_cache_blame_insert) {
      std::cout << cache_pair.first->name() << " cache:" << std::endl;
    }
    cache_pair.second->prepareForCommit();
  }
  enableDirectAccess();
  pushNewChunkIdsToTrackers();
  disableDirectAccess();
}

// Deadlocks are prevented by imposing a global ordering on
// net_table_transactions_, and have the individual chunk locks acquired in
// that order (resource hierarchy solution).
bool Transaction::commit() {
  if (FLAGS_use_raft) {
    return raftChunkCommit();
  } else {
    return legacyChunkCommit();
  }
}

void Transaction::unlockAllChunks(bool is_success) {
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    if (FLAGS_use_raft) {
      net_table_transaction.second->unlock(is_success);
    } else {
      net_table_transaction.second->unlock();
    }
  }
}

void Transaction::prepareMultiChunkTransactionInfo(
    proto::MultiChunkTransactionInfo* info) {
  CHECK(FLAGS_use_raft);
  common::Id transaction_id;
  common::generateId(&transaction_id);
  transaction_id.serialize(info->mutable_transaction_id());
  info->set_begin_time(begin_time_.serialize());
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    net_table_transaction.second->prepareMultiChunkTransactionInfo(info);
  }
}

void Transaction::merge(const std::shared_ptr<Transaction>& merge_transaction,
                        ConflictMap* conflicts) {
  CHECK(merge_transaction.get() != nullptr) << "Merge requires an initiated "
                                               "transaction";
  CHECK_NOTNULL(conflicts);
  conflicts->clear();
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    std::shared_ptr<NetTableTransaction> merge_net_table_transaction(
        new NetTableTransaction(merge_transaction->begin_time_,
                                net_table_transaction.first, *workspace_));
    Conflicts sub_conflicts;
    net_table_transaction.second->merge(merge_net_table_transaction,
                                        &sub_conflicts);
    CHECK_EQ(
        net_table_transaction.second->numChangedItems(),
        merge_net_table_transaction->numChangedItems() + sub_conflicts.size());
    if (merge_net_table_transaction->numChangedItems() > 0u) {
      merge_transaction->net_table_transactions_.insert(std::make_pair(
          net_table_transaction.first, merge_net_table_transaction));
    }
    if (!sub_conflicts.empty()) {
      std::pair<ConflictMap::iterator, bool> insert_result = conflicts->insert(
          std::make_pair(net_table_transaction.first, Conflicts()));
      CHECK(insert_result.second);
      insert_result.first->second.swap(sub_conflicts);
    }
  }
}

size_t Transaction::numChangedItems() const {
  size_t count = 0u;
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    count += net_table_transaction.second->numChangedItems();
  }
  return count;
}

void Transaction::refreshIdToChunkIdMaps() {
  for (TransactionPair& net_table_transaction : net_table_transactions_) {
    net_table_transaction.second->refreshIdToChunkIdMap();
  }
}

void Transaction::refreshAvailableIdsInCaches() {
  for (const CacheMap::value_type& cache_pair : caches_) {
    cache_pair.second->refreshAvailableIds();
  }
}

void Transaction::enableDirectAccess() {
  std::lock_guard<std::mutex> lock(access_type_mutex_);
  CHECK(cache_access_override_.insert(std::this_thread::get_id()).second);
}

void Transaction::disableDirectAccess() {
  std::lock_guard<std::mutex> lock(access_type_mutex_);
  CHECK_EQ(1u, cache_access_override_.erase(std::this_thread::get_id()));
}

NetTableTransaction* Transaction::transactionOf(NetTable* table) const {
  CHECK_NOTNULL(table);
  ensureAccessIsDirect(table);
  std::lock_guard<std::mutex> lock(net_table_transactions_mutex_);
  TransactionMap::const_iterator net_table_transaction =
      net_table_transactions_.find(table);
  if (net_table_transaction == net_table_transactions_.end()) {
    std::shared_ptr<NetTableTransaction> transaction(
        new NetTableTransaction(begin_time_, table, *workspace_));
    std::pair<TransactionMap::iterator, bool> inserted =
        net_table_transactions_.insert(std::make_pair(table, transaction));
    CHECK(inserted.second);
    net_table_transaction = inserted.first;
  }
  return net_table_transaction->second.get();
}

void Transaction::ensureAccessIsCache(NetTable* table) const {
  std::lock_guard<std::mutex> lock(access_mode_mutex_);
  TableAccessModeMap::iterator found = access_mode_.find(table);
  if (found == access_mode_.end()) {
    access_mode_[table] = TableAccessMode::kCache;
  } else {
    CHECK(found->second == TableAccessMode::kCache)
        << "Access mode for table " << table->name() << " is already direct, "
                                                        "may not attach cache.";
  }
}

void Transaction::ensureAccessIsDirect(NetTable* table) const {
  std::unique_lock<std::mutex> lock(access_mode_mutex_);
  TableAccessModeMap::iterator found = access_mode_.find(table);
  if (found == access_mode_.end()) {
    access_mode_[table] = TableAccessMode::kDirect;
  } else {
    if (found->second != TableAccessMode::kDirect) {
      lock.unlock();
      std::lock_guard<std::mutex> lock(access_type_mutex_);
      CHECK(cache_access_override_.find(std::this_thread::get_id()) !=
            cache_access_override_.end())
          << "Access mode for table " << table->name()
          << " is already by cache, may not access directly.";
    }
  }
}

void Transaction::pushNewChunkIdsToTrackers() {
  if (chunk_tracking_disabled_) {
    return;
  }
  // tracked table -> tracked chunks -> tracking table -> tracking item
  typedef std::unordered_map<NetTable*,
                             NetTableTransaction::TrackedChunkToTrackersMap>
      TrackeeToTrackerMap;
  TrackeeToTrackerMap net_table_chunk_trackers;
  for (const TransactionMap::value_type& table_transaction :
       net_table_transactions_) {
    table_transaction.second->getChunkTrackers(
        &net_table_chunk_trackers[table_transaction.first]);
  }
  // tracking item -> tracked table -> tracked chunks
  typedef std::unordered_map<common::Id, TrackeeMultimap> ItemToTrackeeMap;
  // tracking table -> tracking item -> tracked table -> tracked chunks
  typedef std::unordered_map<NetTable*, ItemToTrackeeMap> TrackerToTrackeeMap;
  TrackerToTrackeeMap table_item_chunks_to_push;
  for (const TrackeeToTrackerMap::value_type& net_table_trackers :
       net_table_chunk_trackers) {
    for (const NetTableTransaction::TrackedChunkToTrackersMap::value_type&
             chunk_trackers : net_table_trackers.second) {
      for (const ChunkTransaction::TableToIdMultiMap::value_type& tracker :
           chunk_trackers.second) {
        table_item_chunks_to_push[tracker.first][tracker.second]
                                 [net_table_trackers.first]
                                     .emplace(chunk_trackers.first);
      }
    }
  }

  for (const TrackerToTrackeeMap::value_type& table_chunks_to_push :
       table_item_chunks_to_push) {
    for (const ItemToTrackeeMap::value_type& item_chunks_to_push :
         table_chunks_to_push.second) {
      CHECK(item_chunks_to_push.first.isValid())
          << "Invalid tracker ID for trackee from "
          << "table " << table_chunks_to_push.first->name();
      std::shared_ptr<const Revision> original_tracker =
          getById(item_chunks_to_push.first, table_chunks_to_push.first);

      TrackeeMultimap trackee_multimap;
      trackee_multimap.deserialize(*original_tracker->underlying_revision_);
      // Update only if set of trackees has changed.
      if (trackee_multimap.merge(item_chunks_to_push.second)) {
        std::shared_ptr<Revision> updated_tracker;
        original_tracker->copyForWrite(&updated_tracker);
        trackee_multimap.serialize(updated_tracker->underlying_revision_.get());
        update(table_chunks_to_push.first, updated_tracker);
      }
    }
  }
}

bool Transaction::prepareOrUnlockAll() {
  prepareForCommit();

  proto::MultiChunkTransactionInfo commit_info;
  prepareMultiChunkTransactionInfo(&commit_info);
  timing::Timer timer("map_api::Transaction::commit - lock");
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    net_table_transaction.second->lock();
    bool result = net_table_transaction.second->sendMultiChunkTransactionInfo(
        commit_info);
    if (!result) {
      LOG(WARNING) << "Aborting multiChunkCommit because info commit failed";
      unlockAllChunks(false);
      return false;
    }
  }
  timer.Stop();
  return true;
}

bool Transaction::checkOrUnlockAll() {
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    if (!net_table_transaction.second->check()) {
      LOG(WARNING) << "Aborting multiChunkCommit because check failed";
      unlockAllChunks(false);
      return false;
    }
  }
  return true;
}

bool Transaction::commitRevisionsOrUnlockAll() {
  commit_time_ = LogicalTime::sample();
  VLOG(3) << "Commit from " << begin_time_ << " to " << commit_time_;
  std::vector<std::future<bool>> responses;
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    std::future<bool> success =
        std::async(std::launch::async, &NetTableTransaction::checkedCommit,
                   net_table_transaction.second, commit_time_);
    responses.push_back(std::move(success));
  }
  for (std::future<bool>& response : responses) {
    if (!response.get()) {
      LOG(WARNING)
          << "Aborting multiChunkCommit because sending revisions failed";
      unlockAllChunks(false);
      return false;
    }
  }
  return true;
}

bool Transaction::legacyChunkCommit() {
  prepareForCommit();

  // This must happen after chunk tracker resolution, since chunk tracker
  // resolution might access the cache in read-mode, but we won't be able to
  // fetch the proper metadata until after the commit!
  for (const CacheMap::value_type& cache_pair : caches_) {
    cache_pair.second->discardCachedInsertions();
  }

  timing::Timer timer("map_api::Transaction::commit - lock");
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    net_table_transaction.second->lock();
  }
  timer.Stop();
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    if (!net_table_transaction.second->check()) {
      unlockAllChunks(false);
      return false;
    }
  }
  commit_time_ = LogicalTime::sample();
  VLOG(3) << "Commit from " << begin_time_ << " to " << commit_time_;
  for (const TransactionPair& net_table_transaction : net_table_transactions_) {
    bool success = net_table_transaction.second->checkedCommit(commit_time_);
    if (FLAGS_use_raft) {
      net_table_transaction.second->unlock(success);
    } else {
      net_table_transaction.second->unlock();
    }
  }
  return true;
}

bool Transaction::raftChunkCommit() {
  CHECK(FLAGS_use_raft);

  if (!prepareOrUnlockAll()) {
    return false;
  }

  // This must happen after chunk tracker resolution, since chunk tracker
  // resolution might access the cache in read-mode, but we won't be able to
  // fetch the proper metadata until after the commit!
  for (const CacheMap::value_type& cache_pair : caches_) {
    cache_pair.second->discardCachedInsertions();
  }

  if (!checkOrUnlockAll()) {
    return false;
  }
  if (!commitRevisionsOrUnlockAll()) {
    return false;
  }

  // At this point, all chunks have received all their respective transactions.
  // Any peer receiving unlock implies all other chunks are ready to commit.
  // If the committing peer (this peer) fails at this point, the chunks can
  // attempt to take the transaction forward themselves.
  unlockAllChunks(true);
  return true;
}

}  // namespace map_api */
