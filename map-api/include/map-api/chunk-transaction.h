#ifndef MAP_API_CHUNK_TRANSACTION_H_
#define MAP_API_CHUNK_TRANSACTION_H_

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include <gtest/gtest_prod.h>

#include "map-api/internal/chunk-view.h"
#include "map-api/internal/combined-view.h"
#include "map-api/internal/commit-history-view.h"
#include "map-api/internal/delta-view.h"
#include "map-api/logical-time.h"
#include "map-api/net-table.h"
#include "map-api/revision.h"
#include "map-api/revision-map.h"

namespace map_api {
class ChunkBase;
class Conflicts;

namespace internal {
class CommitFuture;
}  // namespace internal

/**
 * This class is somewhat weaker than the first transaction draft
 * (LocalTransaction, now deprecated) because conflict checking and
 * committing is handled in the Chunk class.
 */
class ChunkTransaction {
  friend class internal::CommitFuture;
  friend class NetTableTransaction;
  friend class Transaction;      // for internal typedefs
  friend class NetTableManager;  // metatable works directly with this
  friend class NetTableFixture;
  FRIEND_TEST(ChunkTest, ChunkTransactions);
  FRIEND_TEST(ChunkTest, ChunkTransactionsConflictConditions);

 private:
  ChunkTransaction(ChunkBase* chunk, NetTable* table);
  ChunkTransaction(const LogicalTime& begin_time, ChunkBase* chunk,
                   NetTable* table);

  // ====
  // READ
  // ====
  template <typename IdType>
  std::shared_ptr<const Revision> getById(const IdType& id) const;
  template <typename ValueType>
  std::shared_ptr<const Revision> findUnique(int key,
                                             const ValueType& value) const;
  void dumpChunk(ConstRevisionMap* result) const;
  template <typename IdType>
  void getAvailableIds(std::unordered_set<IdType>* ids) const;

  // =====
  // WRITE
  // =====
  void insert(std::shared_ptr<Revision> revision);
  void update(std::shared_ptr<Revision> revision);
  // The following function is very dangerous and shouldn't be used apart from
  // where it needs to be used in caches.
  template <typename IdType>
  void getMutableUpdateEntry(const IdType& id,
                             std::shared_ptr<const Revision>** result);
  void remove(std::shared_ptr<Revision> revision);
  template <typename ValueType>
  void addConflictCondition(int key, const ValueType& value);

  // ======================
  // TRANSACTION OPERATIONS
  // ======================
  bool commit();
  bool check();
  void checkedCommit(const LogicalTime& time);
  /**
   * Merging and changeCount are not compatible with conflict conditions.
   */
  void merge(const std::shared_ptr<ChunkTransaction>& merge_transaction,
             Conflicts* conflicts);
  size_t numChangedItems() const;
  inline void finalize() { finalized_ = true; }
  bool isFinalized() const { return finalized_; }

  // INTERNAL
  typedef std::unordered_multimap<NetTable*, common::Id> TableToIdMultiMap;
  void getTrackers(const NetTable::NewChunkTrackerMap& overrides,
                   TableToIdMultiMap* trackers) const;

  struct ConflictCondition {
    const int key;
    const std::shared_ptr<Revision> value_holder;
    ConflictCondition(int _key, const std::shared_ptr<Revision>& _value_holder)
        : key(_key), value_holder(_value_holder) {}
  };
  class ConflictVector : public std::vector<ConflictCondition> {};

  ConflictVector conflict_conditions_;

  LogicalTime begin_time_;

  ChunkBase* chunk_;
  NetTable* table_;
  const std::shared_ptr<const Revision> structure_reference_;

  internal::CommitHistoryView::History commit_history_;

  // The combined views are stacked as follows:
  internal::DeltaView delta_;
  internal::CommitHistoryView commit_history_view_;
  internal::ChunkView chunk_view_;

  internal::CombinedView view_before_delta_;
  internal::CombinedView combined_view_;

  bool finalized_;
};

}  // namespace map_api

#include "./chunk-transaction-inl.h"

#endif  // MAP_API_CHUNK_TRANSACTION_H_
