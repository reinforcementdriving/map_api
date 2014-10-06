#ifndef MAP_API_VISUAL_FRAME_RESOURCE_LOADER_H_
#define MAP_API_VISUAL_FRAME_RESOURCE_LOADER_H_

#include <list>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <multiagent_mapping_common/visual-frame-resource-loader-base.h>

#include "map-api/core.h"
#include "map-api/ipc.h"
#include "map-api/net-table.h"
#include "map-api/net-table-transaction.h"
#include "map-api/revision.h"
#include "map-api/revision-inl.h"
#include "map-api/transaction.h"
#include "map-api/unique-id.h"

namespace map_api {

class ResourceLoader : public common::ResourceLoaderBase {
 public:
  typedef common::ResourceLoaderBase::VisualFrameResourceType
      VisualFrameResourceType;

  explicit ResourceLoader(const std::string& resource_table_name);

  virtual ~ResourceLoader() {}

  bool loadResource(const std::string& resource_id_hex_string,
                    VisualFrameResourceType type,
                    std::shared_ptr<common::VisualFrameBase> visual_frame);

  void getResourceIdsOfType(const std::string& visual_frame_id_hex_string,
                            VisualFrameResourceType type,
                            std::unordered_set<std::string>* id_set);

 private:
  void releaseResourcesIfNecessary();
  int registerResource(
      VisualFrameResourceType type, const std::string& resource_id,
      std::shared_ptr<common::VisualFrameBase> visual_frame_ptr);
  int getNumberOfLoadedResources(VisualFrameResourceType type) const;
  int releaseNumberOfLoadedResources(VisualFrameResourceType type,
                                     int number_to_release);
  cv::Mat loadResourceFromUri(const std::string& uri,
                              VisualFrameResourceType type);

  typedef std::unordered_map<
      int, std::list<std::pair<std::shared_ptr<common::VisualFrameBase>,
                               std::string> > > ResourceMap;
  typedef std::list<std::pair<std::shared_ptr<common::VisualFrameBase>,
                              std::string> > ResourceList;
  typedef std::pair<std::shared_ptr<common::VisualFrameBase>, std::string>
      ResourceRecord;

  ResourceMap loadedResources_;
  NetTable* resourceTable_;

  static const int kMaxNumberOfResourcesPerType = 20;
};

}  // namespace map_api

#endif  // MAP_API_VISUAL_FRAME_RESOURCE_LOADER_H_
