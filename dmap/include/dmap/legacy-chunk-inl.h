#ifndef DMAP_LEGACY_CHUNK_INL_H_
#define DMAP_LEGACY_CHUNK_INL_H_

#include "dmap/chunk-data-container-base.h"

namespace dmap {

template <typename RequestType>
void LegacyChunk::fillMetadata(RequestType* destination) const {
  CHECK_NOTNULL(destination);
  destination->mutable_metadata()->set_table(this->data_container_->name());
  id().serialize(destination->mutable_metadata()->mutable_chunk_id());
}

inline void LegacyChunk::syncLatestCommitTime(const Revision& item) {
  LogicalTime commit_time = item.getModificationTime();
  if (commit_time > latest_commit_time_) {
    latest_commit_time_ = commit_time;
  }
}

}  // namespace dmap

#endif  // DMAP_LEGACY_CHUNK_INL_H_