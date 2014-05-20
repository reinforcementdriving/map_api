#ifndef WRITE_ONLY_TABLE_INTERFACE_H_
#define WRITE_ONLY_TABLE_INTERFACE_H_

#include <vector>
#include <memory>
#include <map>

#include <Poco/Data/Common.h>
#include <gflags/gflags.h>

#include "map-api/id.h"
#include "map-api/revision.h"
#include "map-api/cr-table-interface.h"
#include "core.pb.h"

namespace map_api {

class CRTableInterface : public proto::TableDescriptor {
 public:
  /**
   * Init routine, may be overriden by derived classes, in particular
   * CRUTableInterface. This function calls the pure virtual functions
   * tableName() and define()
   */
  virtual bool init();

  bool isInitialized() const;

  /**
   * ================================================
   * FUNCTIONS TO BE IMPLEMENTED BY THE DERIVED CLASS
   * ================================================
   */
  /**
   * This table name will appear in the database, so it must be chosen SQL
   * friendly: Letters and underscores only.
   */
  virtual const std::string tableName() const = 0;
  /**
   * Function to be implemented by derivations: Define table by repeated
   * calls to addField()
   */
  virtual bool define() = 0;
  virtual ~CRTableInterface();

  /**
   * Returns a table row template TODO(tcies) cache, in setup()
   */
  std::shared_ptr<Revision> getTemplate() const;
  /**
   * The following struct can be used to automatically supply table name and
   * item id to a glog message.
   */
  typedef struct ItemDebugInfo{
    std::string table;
    std::string id;
    ItemDebugInfo(const std::string& _table, const Id& _id) :
      table(_table), id(_id.hexString()) {}
  } ItemDebugInfo;

 protected:
  /**
   * Function to be called at definition:  Adds field to table. This only calls
   * the other addField function with the proper enum, see implementation
   * header.
   */
  template<typename Type>
  void addField(const std::string& name);
  void addField(const std::string& name,
                proto::TableFieldDescriptor_Type type);
  /**
   * Shared pointer to database session TODO(tcies) can this be set private
   * yet accessed from a test table?
   */
  std::shared_ptr<Poco::Data::Session> session_;

  /**
   * The following functions are to be used by transactions only. They pose a
   * very crude access straight to the database, without synchronization
   * and conflict checking - that is assumed to be done by the transaction.
   * History is another example at it is managed by the transaction.
   */
  friend class Transaction;
  friend class History;
  /**                                                                       CCCC
   *                                                                       C
   * Commits an insert query. ID has to be defined in the query, this is   C
   * responsability of the transaction.                                    C
   *                                                                        CCCC
   */
  bool rawInsertQuery(const Revision& query) const;
  /**                                                                      RRRR
   *                                                                       R   R
   * Fetches row by ID and returns it as revision                          RRRR
   *                                                                       R  R
   *                                                                       R   R
   */
  std::shared_ptr<Revision> rawGetRow(const Id& id) const;
  /**
   * Loads items where key = value, returns their count.
   * If "key" is an empty string, no filter will be applied (equivalent to
   * rawDump())
   * The non-templated override that uses a revision container for the value is
   * there so that class Transaction may store conflict requests, which call
   * this function upon commit, without the need to specialize, which would be
   * impractical for users who want to add custom field types.
   * Virtual, for TODO(tcies) CRUTableInterface will need its own implementation
   * TODO(discsuss) this is inconsistent with rawInsertQuery, which is not
   * virtual, but the difference between CR and CRU is handled in the
   * Transaction class. If possible, this would be better moved here, right?
   */
  template<typename ValueType>
  int rawFind(const std::string& key, const ValueType& value,
              std::vector<std::shared_ptr<Revision> >* dest) const;
  virtual int rawFindByRevision(
      const std::string& key, const Revision& valueHolder,
      std::vector<std::shared_ptr<Revision> >* dest)  const;
  /**
   * Same as rawFind(), but asserts that not more than one item is found
   */
  template<typename ValueType>
  std::shared_ptr<Revision> rawFindUnique(const std::string& key,
                                          const ValueType& value) const;
  /**
   * Fetches all the contents of the table
   */
  void rawDump(std::vector<std::shared_ptr<Revision> >* dest) const;
  /**
   * The PocoToProto class serves as intermediate between Poco and Protobuf:
   * Because Protobuf doesn't support pointers to numeric fields and Poco Data
   * can't handle blobs saved as std::strings (which is used in Protobuf),
   * this intermediate data structure is required to pass data from Poco::Data
   * to our protobuf objects.
   */
  class PocoToProto {
   public:
    /**
     * Associating with Table interface object to get template
     */
    PocoToProto(const CRTableInterface& table);
    /**
     * To be inserted between "SELECT" and "FROM": Bind database outputs to
     * own structure.
     */
    void into(Poco::Data::Statement& statement);
    /**
     * Applies the data obtained after statement execution onto a vector of
     * Protos. Returns the element count. This assumes the presence of an "ID"
     * field.
     */
    int toProto(std::vector<std::shared_ptr<Revision> >* dest);
   private:
    const CRTableInterface& table_;
    /**
     * Maps where the data is store intermediately
     */
    std::map<std::string, std::vector<double> > doubles_;
    std::map<std::string, std::vector<int32_t> > ints_;
    std::map<std::string, std::vector<int64_t> > longs_;
    std::map<std::string, std::vector<Poco::Data::BLOB> > blobs_;
    std::map<std::string, std::vector<std::string> > strings_;
    std::map<std::string, std::vector<std::string> > hashes_;
  };

 private:
  friend class CRUTableInterface;
  /**
   * Synchronize with cluster: Check if table already present in cluster
   * metatable, add user to distributed table. Virtual so that the metatable
   * may override this to do nothing in order to avoid infinite recursion.
   */
  virtual bool sync();
  /**
   * Parse and execute SQL query necessary to create the database
   */
  bool createQuery();

  bool initialized_ = false;
};

std::ostream& operator<< (std::ostream& stream, const
                          CRTableInterface::ItemDebugInfo& info);

} /* namespace map_api */

#include "map-api/cr-table-interface-inl.h"

#endif /* WRITE_ONLY_TABLE_INTERFACE_H_ */
