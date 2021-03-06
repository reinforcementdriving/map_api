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

#include "map-api/legacy-chunk-data-container-base.h"

namespace map_api {

bool LegacyChunkDataContainerBase::insert(
    const LogicalTime& time, const std::shared_ptr<Revision>& query) {
  std::lock_guard<std::mutex> lock(access_mutex_);
  CHECK(query.get() != nullptr);
  CHECK(isInitialized()) << "Attempted to insert into non-initialized table";
  std::shared_ptr<Revision> reference = getTemplate();
  CHECK(query->structureMatch(*reference))
      << "Bad structure of insert revision";
  CHECK(query->getId<map_api_common::Id>().isValid())
      << "Attempted to insert element with invalid ID";
  query->setInsertTime(time);
  query->setUpdateTime(time);
  return insertImpl(query);
}

bool LegacyChunkDataContainerBase::bulkInsert(const LogicalTime& time,
                                              const MutableRevisionMap& query) {
  std::lock_guard<std::mutex> lock(access_mutex_);
  CHECK(isInitialized()) << "Attempted to insert into non-initialized table";
  std::shared_ptr<Revision> reference = getTemplate();
  map_api_common::Id id;
  for (const typename MutableRevisionMap::value_type& id_revision : query) {
    CHECK_NOTNULL(id_revision.second.get());
    CHECK(id_revision.second->structureMatch(*reference))
        << "Bad structure of insert revision";
    id = id_revision.second->getId<map_api_common::Id>();
    CHECK(id.isValid()) << "Attempted to insert element with invalid ID";
    CHECK(id == id_revision.first) << "ID in RevisionMap doesn't match";
    id_revision.second->setInsertTime(time);
    id_revision.second->setUpdateTime(time);
  }
  return bulkInsertImpl(query);
}

bool LegacyChunkDataContainerBase::patch(
    const std::shared_ptr<const Revision>& query) {
  std::lock_guard<std::mutex> lock(access_mutex_);
  CHECK(query != nullptr);
  CHECK(isInitialized()) << "Attempted to insert into non-initialized table";
  std::shared_ptr<Revision> reference = getTemplate();
  CHECK(query->structureMatch(*reference)) << "Bad structure of patch revision";
  CHECK(query->getId<map_api_common::Id>().isValid())
      << "Attempted to insert element with invalid ID";
  return patchImpl(query);
}

LegacyChunkDataContainerBase::History::~History() {}

void LegacyChunkDataContainerBase::findHistoryByRevision(
    int key, const Revision& valueHolder, const LogicalTime& time,
    HistoryMap* dest) const {
  CHECK(isInitialized()) << "Attempted to find in non-initialized table";
  CHECK_NOTNULL(dest);
  dest->clear();
  CHECK(time < LogicalTime::sample());
  return findHistoryByRevisionImpl(key, valueHolder, time, dest);
}

void LegacyChunkDataContainerBase::update(
    const LogicalTime& time, const std::shared_ptr<Revision>& query) {
  CHECK(query != nullptr);
  CHECK(isInitialized()) << "Attempted to update in non-initialized table";
  std::shared_ptr<Revision> reference = getTemplate();
  CHECK(query->structureMatch(*reference))
      << "Bad structure of update revision";
  CHECK(query->getId<map_api_common::Id>().isValid())
      << "Attempted to update element with invalid ID";
  LogicalTime update_time = time;
  query->setUpdateTime(update_time);
  CHECK(insertUpdatedImpl(query));
}

void LegacyChunkDataContainerBase::remove(
    const LogicalTime& time, const std::shared_ptr<Revision>& query) {
  CHECK(query != nullptr);
  CHECK(isInitialized());
  std::shared_ptr<Revision> reference = getTemplate();
  CHECK(query->structureMatch(*reference));
  CHECK_NE(query->getId<map_api_common::Id>(), map_api_common::Id());
  LogicalTime update_time = time;
  query->setUpdateTime(update_time);
  query->setRemoved();
  CHECK(insertUpdatedImpl(query));
}

void LegacyChunkDataContainerBase::clear() {
  std::lock_guard<std::mutex> lock(access_mutex_);
  clearImpl();
}

}  // namespace map_api
