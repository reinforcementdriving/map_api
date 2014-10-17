#ifndef MAP_API_STXXL_REVISION_STORE_H_
#define MAP_API_STXXL_REVISION_STORE_H_
#include <memory>
#include <mutex>

#include <stxxl.h>

#include <map-api/proto-stl-stream.h>
#include <map-api/revision.h>

namespace map_api {
struct RevisionInformation {
  MemoryBlockInformation memory_block_;
  LogicalTime insert_time_;
};

template<int BlockSize>
class STXXLRevisionStore {
 public:
  inline bool storeRevision(const Revision& revision,
                            RevisionInformation* revision_info) {
    CHECK_NOTNULL(revision_info);

    std::unique_lock<std::mutex> lock(mutex_);
    revision_info->insert_time_ = revision.getInsertTime();
    STLContainerOutputStream<BlockSize, ContainerType> output_stream(
        &proto_revision_pool_);

    MemoryBlockInformation& block_information = revision_info->memory_block_;
    bool status = output_stream.WriteMessage(*revision.underlying_revision_,
                                             &block_information);
    return status;
  }

  inline bool retrieveRevision(
      const RevisionInformation& revision_info,
      std::shared_ptr<const Revision>* revision) const {
    CHECK_NOTNULL(revision);
    std::unique_lock<std::mutex> lock(mutex_);
    const MemoryBlockInformation& block_information =
        revision_info.memory_block_;

    STLContainerInputStream<BlockSize, ContainerType> input_stream(
        block_information.block_index, block_information.byte_offset,
        &proto_revision_pool_);
    std::shared_ptr<proto::Revision> proto_in(new proto::Revision);

    bool status = input_stream.ReadMessage(proto_in.get());

    revision->reset(new Revision(proto_in));
    CHECK_EQ(revision_info.insert_time_, (*revision)->getInsertTime());
    return status;
  }

 private:
  template<class T, class A> using ContainerType =
  typename stxxl::VECTOR_GENERATOR<T>::result;
  mutable MemoryBlockPool<BlockSize, ContainerType> proto_revision_pool_;
  mutable std::mutex mutex_;
};
}  // namespace map_api
#endif  // MAP_API_STXXL_REVISION_STORE_H_
