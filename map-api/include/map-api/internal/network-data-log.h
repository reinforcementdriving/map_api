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

#ifndef INTERNAL_NETWORK_DATA_LOG_H_
#define INTERNAL_NETWORK_DATA_LOG_H_

#include <fstream>  // NOLINT
#include <string>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

namespace map_api {
namespace internal {

class NetworkDataLog {
 public:
  explicit NetworkDataLog(const std::string& prefix);

  void log(const size_t bytes, const std::string& type);

  typedef std::unordered_map<std::string, Eigen::Matrix2Xd> TypeCumSums;
  static void getCumSums(const std::string& file_name, TypeCumSums* cum_sums);

 private:
  struct Line {
    double time, bytes;
    std::string type;
  };

  std::ofstream logger_;
};

}  // namespace internal
}  // namespace map_api

#endif  // INTERNAL_NETWORK_DATA_LOG_H_
