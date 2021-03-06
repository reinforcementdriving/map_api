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

#ifndef MAP_API_REVISION_H_
#define MAP_API_REVISION_H_

#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "./core.pb.h"
#include "map-api/logical-time.h"
#include <map-api-common/unique-id.h>

namespace map_api {
class TrackeeMultimap;

// Friending parametrized templated test cases seems to miss from gtest_prod.h.
namespace gtest_case_ProtoSTLStream_ {
template <typename gtest_TypeParam_>
class ProtoAutoSerializationWorks;
}  // gtest_case_ProtoSTLStream_

class Revision {
  friend class LegacyChunk;
  friend class ChunkDataContainerBase;
  friend class LegacyChunkDataContainerBase;
  template <int BlockSize>
  friend class STXXLRevisionStore;
  friend class TrackeeMultimap;
  friend class Transaction;

  // Friending parametrized templated test cases seems to miss from
  // gtest_prod.h.
  template <typename gtest_TypeParam_>
  friend class gtest_case_ProtoSTLStream_::ProtoAutoSerializationWorks;

 public:
  typedef std::vector<char> Blob;
  typedef std::shared_ptr<Revision> Ptr;
  typedef std::shared_ptr<const Revision> ConstPtr;

  Revision& operator=(const Revision& other) = delete;

  // Constructor and assignment replacements.
  void copyForWrite(std::shared_ptr<Revision>* result) const;
  // You need to use std::move() for the unique_ptr of the following.
  static void fromProto(const std::shared_ptr<proto::Revision>& revision_proto,
                        std::shared_ptr<Revision>* result);
  static void fromProto(const std::shared_ptr<proto::Revision>& revision_proto,
                        std::shared_ptr<const Revision>* result);
  static std::shared_ptr<Revision> fromProtoString(
      const std::string& revision_proto_string);

  // Defaults to blob in order to be easy to use for arbitrary protobufs.
  template <typename FieldType>
  static proto::Type getProtobufTypeEnum() {
    return proto::Type::BLOB;
  }

  void addField(int index, proto::Type type);
  template <typename FieldType>
  void addField(int index);
  void removeLastField();

  /**
   * Does not check type - type is checked with get/set. Nothing that can be
   * done if type doesn't match anyways.
   */
  bool hasField(int index) const;

  proto::Type getFieldType(int index) const;

  template <typename FieldType>
  bool set(int index, const FieldType& value);

  template <typename FieldType>
  bool get(int index, FieldType* value) const;

  void clearCustomFieldValues();

  inline LogicalTime getInsertTime() const {
    return LogicalTime(underlying_revision_->insert_time());
  }
  inline LogicalTime getUpdateTime() const {
    return LogicalTime(underlying_revision_->update_time());
  }
  inline bool hasBeenUpdated() const {
    return underlying_revision_->update_time() >
           underlying_revision_->insert_time();
  }
  inline LogicalTime getModificationTime() const {
    return (underlying_revision_->has_update_time()) ? getUpdateTime()
                                                     : getInsertTime();
  }
  inline map_api_common::Id getChunkId() const {
    if (underlying_revision_->has_chunk_id()) {
      return map_api_common::Id(underlying_revision_->chunk_id());
    } else {
      return map_api_common::Id();
    }
  }
  template <typename IdType>
  inline IdType getId() const {
    if (underlying_revision_->has_id()) {
      return IdType(underlying_revision_->id());
    } else {
      return IdType();
    }
  }
  template <typename IdType>
  inline void setId(const IdType& id) {
    id.serialize(underlying_revision_->mutable_id());
  }
  inline bool isRemoved() const {
    return underlying_revision_->has_removed() &&
           underlying_revision_->removed();
  }

  template <typename ExpectedType>
  bool verifyEqual(int index, const ExpectedType& expected) const;

  /**
   * Returns true if Revision contains same fields as other
   */
  bool structureMatch(const Revision& reference) const;

  /**
   * Returns true if value at key is same as with other
   */
  bool fieldMatch(const Revision& other, int index) const;
  bool areAllCustomFieldsEqual(const Revision& other) const;

  std::string dumpToString() const;

  inline std::string serializeUnderlying() const {
    return underlying_revision_->SerializeAsString();
  }

  inline bool SerializeToCodedStream(
      google::protobuf::io::CodedOutputStream* output) const {
    CHECK_NOTNULL(output);
    return underlying_revision_->SerializeToCodedStream(output);
  }

  inline int byteSize() const { return underlying_revision_->ByteSize(); }

  inline int customFieldCount() const {
    return underlying_revision_->custom_field_values_size();
  }

  bool operator==(const Revision& other) const;
  inline bool operator!=(const Revision& other) const {
    return !operator==(other);
  }

  void getTrackedChunks(TrackeeMultimap* result) const;
  bool fetchTrackedChunks() const;

  // Returns true if merge succeeded. If false is returned, revision_at_hand
  // must be unchanged! TODO(tcies) enforce by design?
  typedef std::function<bool(const Revision& conflicting_revision,  // NOLINT
                             const Revision& original_revision,
                             Revision* revision_at_hand)> AutoMergePolicy;
  static bool defaultAutoMergePolicy(const Revision& conflicting_revision,
                                     const Revision& original_revision,
                                     Revision* revision_at_hand);
  // Succeeds if either the default merge policy or any of the custom merge
  // policies succeed.
  bool tryAutoMerge(const Revision& conflicting_revision,
                    const Revision& original_revision,
                    const std::vector<AutoMergePolicy>& custom_merge_policies);

 private:
  Revision() = default;

  inline void setInsertTime(const LogicalTime& time) {
    underlying_revision_->set_insert_time(time.serialize());
  }
  inline void setUpdateTime(const LogicalTime& time) {
    underlying_revision_->set_update_time(time.serialize());
  }
  inline void setChunkId(const map_api_common::Id& id) {
    id.serialize(underlying_revision_->mutable_chunk_id());
  }
  inline void setRemoved() { underlying_revision_->set_removed(true); }

  // Exception to parameter ordering: The standard way would make the function
  // call ambiguous if FieldType = int.
  // The default implementation assumes that the type is a protobuf.
  template <typename FieldType>
  bool set(proto::TableField* field, const FieldType& value);
  template <typename FieldType>
  bool get(const proto::TableField& field, FieldType* value) const;

  std::shared_ptr<proto::Revision> underlying_revision_;
};

/**
 * Convenience macros to specialize the above templates in one line.
 */
#define MAP_API_DECLARE_TYPE_ENUM(TYPE) \
  template <>                        \
  proto::Type Revision::getProtobufTypeEnum<TYPE>()

#define MAP_API_TYPE_ENUM(TYPE, ENUM)                    \
  template <>                                         \
  proto::Type Revision::getProtobufTypeEnum<TYPE>() { \
    return ENUM;                                      \
  }                                                   \
  extern void revEnum##__FILE__##__LINE__(void)

#define MAP_API_REVISION_SET(TYPE) \
  template <>                   \
  bool Revision::set<TYPE>(proto::TableField* field, const TYPE& value)

#define MAP_API_REVISION_GET(TYPE) \
  template <>                   \
  bool Revision::get<TYPE>(const proto::TableField& field, TYPE* value) const

#define MAP_API_DECLARE_TYPE_SUPPORT(TypeName) \
  MAP_API_DECLARE_TYPE_ENUM(TypeName);         \
  MAP_API_REVISION_SET(TypeName);              \
  MAP_API_REVISION_GET(TypeName)

#define MAP_API_REVISION_UNIQUE_ID(TypeName)                             \
  MAP_API_TYPE_ENUM(TypeName, ::map_api::proto::Type::HASH128);             \
  MAP_API_REVISION_SET(TypeName) {                                       \
    CHECK_NOTNULL(field)->set_string_value(value.hexString());        \
    return true;                                                      \
  }                                                                   \
  MAP_API_REVISION_GET(TypeName) {                                       \
    return CHECK_NOTNULL(value)->fromHexString(field.string_value()); \
  }                                                                   \
  extern void __FILE__##__LINE__(void)

/**
 * A generic, blob-y field type for testing blob insertion
 */
class testBlob : public map_api::proto::TableField {
 public:
  inline bool operator==(const testBlob& other) const {
    if (!this->has_type()) return !other.has_type();
    return type() == other.type();
  }
};

}  // namespace map_api

#include "./revision-inl.h"

#endif  // MAP_API_REVISION_H_
