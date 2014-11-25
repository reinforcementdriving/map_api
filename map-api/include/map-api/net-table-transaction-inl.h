#ifndef MAP_API_NET_TABLE_TRANSACTION_INL_H_
#define MAP_API_NET_TABLE_TRANSACTION_INL_H_
#include <string>
#include <vector>

namespace map_api {

template <typename IdType>
std::shared_ptr<const Revision> NetTableTransaction::getById(const IdType& id)
    const {
  std::shared_ptr<const Revision> uncommitted = getByIdFromUncommitted(id);
  if (uncommitted) {
    return uncommitted;
  }
  std::shared_ptr<const Revision> result;
  Chunk* chunk = chunkOf(id, &result);
  if (chunk) {
    LogicalTime inconsistent_time = result->getModificationTime();
    LogicalTime chunk_latest_commit = chunk->getLatestCommitTime();

    if (chunk_latest_commit <= inconsistent_time) {
      return result;
    } else {
      // TODO(tcies) another optimization possibility: item dug deep in
      // history anyways, so not affected be new updates
      return getById(id, chunk);
    }
  } else {
    LOG(ERROR) << "Item " << id << " from table " << table_->name()
               << " not present in active chunks";
    return std::shared_ptr<Revision>();
  }
}

template <typename IdType>
std::shared_ptr<const Revision> NetTableTransaction::getById(
    const IdType& id, Chunk* chunk) const {
  CHECK_NOTNULL(chunk);
  return transactionOf(chunk)->getById(id);
}

template <typename IdType>
std::shared_ptr<const Revision> NetTableTransaction::getByIdFromUncommitted(
    const IdType& id) const {
  for (const TransactionMap::value_type& chunk_transaction :
       chunk_transactions_) {
    std::shared_ptr<const Revision> result =
        chunk_transaction.second->getByIdFromUncommitted(id);
    if (result) {
      return result;
    }
  }
  return std::shared_ptr<const Revision>();
}

template <typename ValueType>
CRTable::RevisionMap NetTableTransaction::find(int key,
                                               const ValueType& value) {
  // TODO(tcies) uncommitted
  return table_->lockFind(key, value, begin_time_);
}

template <typename IdType>
void NetTableTransaction::getAvailableIds(std::vector<IdType>* ids) {
  CHECK_NOTNULL(ids);
  table_->getAvailableIds(begin_time_, ids);
}

template <typename IdType>
void NetTableTransaction::remove(const UniqueId<IdType>& id) {
  std::shared_ptr<const Revision> revision;
  Chunk* chunk = chunkOf(id, &revision);
  std::shared_ptr<Revision> remove_revision =
      std::make_shared<Revision>(*revision);
  transactionOf(CHECK_NOTNULL(chunk))->remove(remove_revision);
}

template <typename IdType>
Chunk* NetTableTransaction::chunkOf(
    const IdType& id, std::shared_ptr<const Revision>* inconsistent) const {
  CHECK_NOTNULL(inconsistent);
  // TODO(tcies) uncommitted
  *inconsistent = table_->getByIdInconsistent(id, begin_time_);
  if (!(*inconsistent)) {
    return nullptr;
  }
  return table_->getChunk((*inconsistent)->getChunkId());
}

}  // namespace map_api

#endif  // MAP_API_NET_TABLE_TRANSACTION_INL_H_
