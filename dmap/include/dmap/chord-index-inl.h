#ifndef DMAP_CHORD_INDEX_INL_H_
#define DMAP_CHORD_INDEX_INL_H_

#include <cstring>
#include <vector>

#include <Poco/MD5Engine.h>
#include <Poco/DigestStream.h>

namespace dmap {
template <typename DataType>
ChordIndex::Key ChordIndex::hash(const DataType& data) {
  static_assert(sizeof(Key) <= sizeof(size_t),
                "Key should be smaller than std::hash() output!");
  union KeyUnion {
    Key key;
    size_t hash_result;
  };

  KeyUnion key_union;
  key_union.hash_result = std::hash<DataType>()(data);
  return key_union.key;
}
}  // namespace dmap

#endif  // DMAP_CHORD_INDEX_INL_H_