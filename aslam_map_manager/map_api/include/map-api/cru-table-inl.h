#ifndef MAP_API_CRU_TABLE_INL_H_
#define MAP_API_CRU_TABLE_INL_H_

#include <string>

namespace map_api {

template <typename ValueType>
void CRUTable::findHistory(int key, const ValueType& value,
                           const LogicalTime& time, HistoryMap* dest) {
  std::shared_ptr<Revision> valueHolder = this->getTemplate();
  if (key >= 0) {
    valueHolder->set(key, value);
  }
  return this->findHistoryByRevision(key, *valueHolder, time, dest);
}

template <typename IdType>
void CRUTable::itemHistory(const IdType& id, const LogicalTime& time,
                           History* dest) {
  Id map_api_id;
  sm::HashId hash_id;
  id.toHashId(&hash_id);
  map_api_id.fromHashId(hash_id);
  itemHistoryCRUDerived(map_api_id, time, dest);
}

template <typename IdType>
void CRUTable::remove(const LogicalTime& time, const IdType& id) {
  std::shared_ptr<Revision> latest = getById(id, time);
  remove(time, *latest);
}

}  // namespace map_api

#endif  // MAP_API_CRU_TABLE_INL_H_
