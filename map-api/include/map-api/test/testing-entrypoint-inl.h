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
// along with Map API.  If not, see <http://www.gnu.org/licenses/>.

#ifndef DMAP_TESTING_ENTRYPOINT_INL_H_
#define DMAP_TESTING_ENTRYPOINT_INL_H_

#include <fstream>  // NOLINT

#include <gflags/gflags.h>

#include <multiprocess-gtest/multiprocess-fixture.h>

#include "map-api/file-discovery.h"

namespace map_api {

MapApiTestEntryPoint::~MapApiTestEntryPoint() {}

void MapApiTestEntryPoint::customInit() {
  if (FLAGS_subprocess_id == 0) {
    std::ofstream truncator(FileDiscovery::kFileName,
                            std::ofstream::out | std::ofstream::trunc);
  }
}
}  // namespace map_api

#endif  // DMAP_TESTING_ENTRYPOINT_INL_H_
