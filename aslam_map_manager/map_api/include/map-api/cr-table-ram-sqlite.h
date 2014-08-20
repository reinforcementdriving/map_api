#ifndef MAP_API_CR_TABLE_RAM_SQLITE_H_
#define MAP_API_CR_TABLE_RAM_SQLITE_H_

#include <string>

#include "map-api/cr-table.h"
#include "map-api/sqlite-interface.h"

namespace map_api {

/**
 * TODO(tcies) make this a proper RAM cache (maps, not RAM sqlite) and create
 * a CRTableDiskCache for disk SQLite
 */
class CRTableRamSqlite final : public CRTable {
 public:
  virtual ~CRTableRamSqlite();

 private:
  virtual bool initCRDerived() final override;
  virtual bool insertCRDerived(Revision* query) final override;
  virtual bool bulkInsertCRDerived(const RevisionMap& query) final override;
  virtual bool patchCRDerived(const Revision& query) final override;
  virtual int findByRevisionCRDerived(
      const std::string& key, const Revision& valueHolder,
      const LogicalTime& time, CRTable::RevisionMap* dest) final override;
  virtual int countByRevisionCRDerived(const std::string& key,
                                       const Revision& valueHolder,
                                       const LogicalTime& time) final override;

 private:
  SqliteInterface sqlite_interface_;
};

}  // namespace map_api

#endif  // MAP_API_CR_TABLE_RAM_SQLITE_H_