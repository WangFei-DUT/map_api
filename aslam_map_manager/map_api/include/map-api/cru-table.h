#ifndef MAP_API_CRU_TABLE_H_
#define MAP_API_CRU_TABLE_H_

#include <vector>
#include <list>
#include <memory>
#include <map>
#include <string>

#include <Poco/Data/Common.h>
#include <gflags/gflags.h>

#include "map-api/cr-table.h"
#include "map-api/revision.h"
#include "map-api/logical-time.h"
#include "./core.pb.h"

namespace map_api {

/**
 * Provides interface to map api tables.
 */
class CRUTable : public CRTable {
  friend class Chunk;

 public:
  // Latest at front
  class History : public std::list<Revision> {
   public:
    const_iterator latestAt(const LogicalTime& time) const;
    /**
     * Index_guess guesses the position of the update time field in the Revision
     * proto.
     */
    const_iterator latestAt(const LogicalTime& time, int index_guess) const;
  };
  typedef std::unordered_map<Id, History> HistoryMap;

  virtual ~CRUTable();
  /**
   * Field ID in revision must correspond to an already present item, revision
   * structure needs to match. Query may be modified according to the default
   * field policy.
   * Calls insertUpdatedCRUDerived and updateCurrentReferToUpdatedCRUDerived.
   * It is possible to specify update time for singular times of transactions.
   * TODO(tcies) make it the only possible way of setting time
   */
  bool update(Revision* query);  // TODO(tcies) void
  void update(Revision* query, const LogicalTime& time);
  bool getLatestUpdateTime(const Id& id, LogicalTime* time);

  template <typename ValueType>
  void findHistory(const std::string& key, const ValueType& value,
                   const LogicalTime& time, HistoryMap* dest);

  virtual void findHistoryByRevision(const std::string& key,
                                     const Revision& valueHolder,
                                     const LogicalTime& time,
                                     HistoryMap* dest) final;

  virtual Type type() const final override;

  /**
   * Default fields for internal management,
   */
  static const std::string kUpdateTimeField;
  static const std::string kPreviousTimeField;  // time of previous revision
  static const std::string kNextTimeField;      // time of next revision

 private:
  virtual bool initCRDerived() final override;
  virtual bool insertCRDerived(Revision* query) final override;
  virtual bool bulkInsertCRDerived(const RevisionMap& query) final override;
  virtual int findByRevisionCRDerived(
      const std::string& key, const Revision& valueHolder,
      const LogicalTime& time, CRTable::RevisionMap* dest) final override;
  virtual int countByRevisionCRDerived(const std::string& key,
                                       const Revision& valueHolder,
                                       const LogicalTime& time) final override;
  /**
   * ================================================
   * FUNCTIONS TO BE IMPLEMENTED BY THE DERIVED CLASS
   * ================================================
   * The CRTable class contains most documentation on these functions.
   */
  virtual bool initCRUDerived() = 0;
  virtual bool insertCRUDerived(Revision* query) = 0;
  virtual bool bulkInsertCRUDerived(const RevisionMap& query) = 0;
  virtual bool patchCRDerived(const Revision& query) override = 0;
  virtual int findByRevisionCRUDerived(
      const std::string& key, const Revision& valueHolder,
      const LogicalTime& time, CRTable::RevisionMap* dest) = 0;
  virtual int countByRevisionCRUDerived(const std::string& key,
                                        const Revision& valueHolder,
                                        const LogicalTime& time) = 0;

  /**
   * Implement insertion of the updated revision
   */
  virtual bool insertUpdatedCRUDerived(const Revision& query) = 0;
  virtual void findHistoryByRevisionCRUDerived(const std::string& key,
                                               const Revision& valueHolder,
                                               const LogicalTime& time,
                                               HistoryMap* dest) = 0;
};

}  // namespace map_api

#include "map-api/cru-table-inl.h"

#endif  // MAP_API_CRU_TABLE_H_
