#include <map-api/chunk-data-stxxl-container.h>

namespace map_api {

ChunkDataStxxlContainer::ChunkDataStxxlContainer()
    : revision_store_(new STXXLRevisionStore<kBlockSize>()) {}

ChunkDataStxxlContainer::~ChunkDataStxxlContainer() {}

bool ChunkDataStxxlContainer::initImpl() { return true; }

bool ChunkDataStxxlContainer::insertImpl(
    const std::shared_ptr<const Revision>& query) {
  CHECK(query != nullptr);
  common::Id id = query->getId<common::Id>();
  STXXLHistoryMap::iterator found = data_.find(id);
  if (found != data_.end()) {
    return false;
  }
  CRURevisionInformation revision_information;
  CHECK(revision_store_->storeRevision(*query, &revision_information));
  data_[id].push_front(revision_information);
  return true;
}

bool ChunkDataStxxlContainer::bulkInsertImpl(const MutableRevisionMap& query) {
  for (const MutableRevisionMap::value_type& pair : query) {
    if (data_.find(pair.first) != data_.end()) {
      return false;
    }
  }
  for (const MutableRevisionMap::value_type& pair : query) {
    CRURevisionInformation revision_information;
    CHECK(revision_store_->storeRevision(*pair.second, &revision_information));
    data_[pair.first].push_front(revision_information);
  }
  return true;
}

bool ChunkDataStxxlContainer::patchImpl(
    const std::shared_ptr<const Revision>& query) {
  CHECK(query != nullptr);
  common::Id id = query->getId<common::Id>();
  LogicalTime time = query->getUpdateTime();
  STXXLHistoryMap::iterator found = data_.find(id);
  if (found == data_.end()) {
    found = data_.insert(std::make_pair(id, STXXLHistory())).first;
  }
  CRURevisionInformation revision_information;
  CHECK(revision_store_->storeRevision(*query, &revision_information));
  for (STXXLHistory::iterator it = found->second.begin();
       it != found->second.end(); ++it) {
    if (it->update_time_ <= time) {
      CHECK_NE(time, it->update_time_);
      found->second.insert(it, revision_information);
      return true;
    }
    LOG(WARNING) << "Patching, not in front!";  // shouldn't usually be the case
  }
  found->second.push_back(revision_information);
  return true;
}

std::shared_ptr<const Revision> ChunkDataStxxlContainer::getByIdImpl(
    const common::Id& id, const LogicalTime& time) const {
  STXXLHistoryMap::const_iterator found = data_.find(id);
  if (found == data_.end()) {
    return std::shared_ptr<Revision>();
  }
  STXXLHistory::const_iterator latest = found->second.latestAt(time);
  if (latest == found->second.end()) {
    return std::shared_ptr<Revision>();
  }
  std::shared_ptr<const Revision> revision;
  CHECK(revision_store_->retrieveRevision(*latest, &revision));
  return revision;
}

void ChunkDataStxxlContainer::findByRevisionImpl(int key,
                                                 const Revision& value_holder,
                                                 const LogicalTime& time,
                                                 ConstRevisionMap* dest) const {
  CHECK_NOTNULL(dest);
  dest->clear();
  // TODO(tcies) Zero-copy const RevisionMap instead of copyForWrite?
  forEachItemFoundAtTime(key, value_holder, time,
                         [&dest](const common::Id& id, const Revision& item) {
    CHECK(dest->find(id) == dest->end());
    CHECK(dest->emplace(id, item.copyForWrite()).second);
  });
}

void ChunkDataStxxlContainer::getAvailableIdsImpl(
    const LogicalTime& time, std::vector<common::Id>* ids) const {
  CHECK_NOTNULL(ids);
  ids->clear();
  std::vector<std::pair<common::Id, CRURevisionInformation> > ids_and_info;
  ids_and_info.reserve(data_.size());
  for (const STXXLHistoryMap::value_type& pair : data_) {
    STXXLHistory::const_iterator latest = pair.second.latestAt(time);
    if (latest != pair.second.cend()) {
      if (!latest->is_removed_) {
        ids_and_info.emplace_back(pair.first, *latest);
      }
    }
  }
  std::sort(ids_and_info.begin(), ids_and_info.end(),
            [](const std::pair<common::Id, CRURevisionInformation>& lhs,
               const std::pair<common::Id, CRURevisionInformation>& rhs) {
    return lhs.second.memory_block_ < rhs.second.memory_block_;
  });
  ids->reserve(ids_and_info.size());
  for (const std::pair<common::Id, CRURevisionInformation>& pair :
       ids_and_info) {
    ids->emplace_back(pair.first);
  }
}

int ChunkDataStxxlContainer::countByRevisionImpl(
    int key, const Revision& value_holder, const LogicalTime& time) const {
  int count = 0;
  forEachItemFoundAtTime(key, value_holder, time,
                         [&count](const common::Id& /*id*/,
                                  const Revision& /*item*/) { ++count; });
  return count;
}

bool ChunkDataStxxlContainer::insertUpdatedImpl(
    const std::shared_ptr<Revision>& query) {
  return patchImpl(query);
}

void ChunkDataStxxlContainer::findHistoryByRevisionImpl(
    int key, const Revision& valueHolder, const LogicalTime& time,
    HistoryMap* dest) const {
  CHECK_NOTNULL(dest);
  dest->clear();
  for (const STXXLHistoryMap::value_type& pair : data_) {
    // using current state for filter
    std::shared_ptr<const Revision> revision;
    CHECK(revision_store_->retrieveRevision(*pair.second.begin(), &revision));
    if (key < 0 || valueHolder.fieldMatch(*revision, key)) {
      History history;
      for (const CRURevisionInformation& revision_information : pair.second) {
        std::shared_ptr<const Revision> history_entry;
        CHECK(revision_store_->retrieveRevision(revision_information,
                                                &history_entry));
        history.emplace_back(history_entry);
      }
      CHECK(dest->emplace(pair.first, history).second);
    }
  }
  trimToTime(time, dest);
}

void ChunkDataStxxlContainer::chunkHistory(const common::Id& chunk_id,
                                           const LogicalTime& time,
                                           HistoryMap* dest) const {
  CHECK_NOTNULL(dest)->clear();
  for (const STXXLHistoryMap::value_type& pair : data_) {
    if (pair.second.begin()->chunk_id_ == chunk_id) {
      History history;
      for (const CRURevisionInformation& revision_information : pair.second) {
        std::shared_ptr<const Revision> history_entry;
        CHECK(revision_store_->retrieveRevision(revision_information,
                                                &history_entry));
        history.emplace_back(history_entry);
      }
      CHECK(dest->emplace(std::make_pair(pair.first, history)).second);
    }
  }
  trimToTime(time, dest);
}

void ChunkDataStxxlContainer::itemHistoryImpl(const common::Id& id,
                                              const LogicalTime& time,
                                              History* dest) const {
  CHECK_NOTNULL(dest)->clear();
  STXXLHistoryMap::const_iterator found = data_.find(id);
  CHECK(found != data_.end());
  History& history = *dest;
  for (const CRURevisionInformation& revision_information : found->second) {
    std::shared_ptr<const Revision> history_entry;
    CHECK(revision_store_->retrieveRevision(revision_information,
                                            &history_entry));
    history.emplace_back(history_entry);
  }
  dest->remove_if([&time](const std::shared_ptr<const Revision>& item) {
    return item->getUpdateTime() > time;
  });
}

void ChunkDataStxxlContainer::clearImpl() {
  data_.clear();
  revision_store_.reset(new STXXLRevisionStore<kBlockSize>());
}

inline void ChunkDataStxxlContainer::forEachItemFoundAtTime(
    int key, const Revision& value_holder, const LogicalTime& time,
    const std::function<void(const common::Id& id, const Revision& item)>&
        action) const {
  for (const STXXLHistoryMap::value_type& pair : data_) {
    STXXLHistory::const_iterator latest = pair.second.latestAt(time);
    if (latest != pair.second.cend()) {
      std::shared_ptr<const Revision> revision;
      CHECK(revision_store_->retrieveRevision(*latest, &revision));
      if (key < 0 || value_holder.fieldMatch(*revision, key)) {
        if (!revision->isRemoved()) {
          action(pair.first, *revision);
        }
      }
    }
  }
}

inline void ChunkDataStxxlContainer::forChunkItemsAtTime(
    const common::Id& chunk_id, const LogicalTime& time,
    const std::function<void(const common::Id& id, const Revision& item)>&
        action) const {
  for (const STXXLHistoryMap::value_type& pair : data_) {
    if (pair.second.begin()->chunk_id_ == chunk_id) {
      STXXLHistory::const_iterator latest = pair.second.latestAt(time);
      if (latest != pair.second.cend()) {
        std::shared_ptr<const Revision> revision;
        CHECK(
            revision_store_->retrieveRevision(*pair.second.begin(), &revision));
        if (!revision->isRemoved()) {
          action(pair.first, *revision);
        }
      }
    }
  }
}

inline void ChunkDataStxxlContainer::trimToTime(const LogicalTime& time,
                                                HistoryMap* subject) const {
  CHECK_NOTNULL(subject);
  for (HistoryMap::value_type& pair : *subject) {
    pair.second.remove_if([&time](const std::shared_ptr<const Revision>& item) {
      return item->getUpdateTime() > time;
    });
  }
}

}  // namespace map_api