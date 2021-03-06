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

#include "map-api/internal/network-data-log.h"

#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <map-api-common/path-utility.h>

namespace map_api {
namespace internal {

NetworkDataLog::NetworkDataLog(const std::string& prefix)
    : logger_(prefix + "_" + map_api_common::GenerateDateStringFromCurrentTime() + "_" +
              std::to_string(getpid())) {}

void NetworkDataLog::log(const size_t bytes, const std::string& type) {
  typedef std::chrono::system_clock Clock;
  logger_ << std::fixed << std::chrono::duration_cast<std::chrono::milliseconds>(
                 Clock::now().time_since_epoch()).count() /
                 1000. << " " << bytes << " " << type << std::endl;
}

void NetworkDataLog::getCumSums(const std::string& file_name,
                                NetworkDataLog::TypeCumSums* cum_sums) {
  CHECK_NOTNULL(cum_sums)->clear();
  typedef std::unordered_map<std::string, std::vector<Line>> TypeLines;
  TypeLines lines;
  std::ifstream stream(file_name);
  CHECK(stream.is_open());
  while (true) {
    Line line;
    stream >> line.time >> line.bytes >> line.type;
    if (stream.fail()) {
      break;
    }
    lines[line.type].emplace_back(line);
  }

  for (const TypeLines::value_type& type : lines) {
    (*cum_sums)[type.first].resize(2, type.second.size());
    for (size_t i = 0u; i < type.second.size(); ++i) {
      (*cum_sums)[type.first].col(i) << type.second[i].time,
          type.second[i].bytes;
      if (i > 0u) {
        (*cum_sums)[type.first](1, i) += (*cum_sums)[type.first](1, i - 1);
      }
    }
  }
}

}  // namespace internal
}  // namespace map_api
