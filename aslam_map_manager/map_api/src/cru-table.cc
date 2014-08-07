#include "map-api/cru-table.h"

#include <cstdio>
#include <map>

#include <Poco/Data/Common.h>
#include <Poco/Data/Statement.h>
#include <Poco/Data/SQLite/Connector.h>
#include <Poco/Data/BLOB.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "map-api/map-api-core.h"
#include "map-api/local-transaction.h"
#include "core.pb.h"

DEFINE_bool(cru_linked, false, "Determines whether a revision has references "\
            "to the previous and next revision.");

namespace map_api {

CRUTable::~CRUTable() {}

bool CRUTable::update(Revision* query) {
  update(CHECK_NOTNULL(query), LogicalTime::sample());
  return true; // TODO(tcies) void
}

void CRUTable::update(Revision* query, const LogicalTime& time) {
  CHECK_NOTNULL(query);
  CHECK(isInitialized()) << "Attempted to update in non-initialized table";
  std::shared_ptr<Revision> reference = getTemplate();
  CHECK(reference->structureMatch(*query)) <<
      "Bad structure of update revision";
  Id id;
  query->get(kIdField, &id);
  CHECK_NE(id, Id()) << "Attempted to update element with invalid ID";
  LogicalTime update_time = time, previous_time;
  query->set(kUpdateTimeField, update_time);

  if (FLAGS_cru_linked) {
    std::shared_ptr<Revision> current = getById(id, time);
    // TODO(tcies) special cases if time << current time?
    CHECK(false) << "Check special cases";
    LogicalTime insert_time;
    query->get(kInsertTimeField, &insert_time);
    // this check would also be nice if CRU_linked = false, would however lose
    // the update performance benefit of not linking
    CHECK(current->verifyEqual(kInsertTimeField, insert_time));
    current->get(kUpdateTimeField, &previous_time);
    CHECK(previous_time < update_time);
    query->set(kPreviousTimeField, previous_time);
    query->set(kNextTimeField, LogicalTime());
  }

  CHECK(insertUpdatedCRUDerived(*query));

  if (FLAGS_cru_linked) {
    CHECK(updateCurrentReferToUpdatedCRUDerived(
        id, previous_time, update_time));
  }
}

bool CRUTable::getLatestUpdateTime(const Id& id, LogicalTime* time) {
  CHECK_NE(Id(), id);
  CHECK_NOTNULL(time);
  std::shared_ptr<Revision> row = getById(id, LogicalTime::sample());
  ItemDebugInfo itemInfo(name(), id);
  if (!row){
    LOG(ERROR) << itemInfo << "Failed to retrieve row";
    return false;
  }
  row->get(kUpdateTimeField, time);
  return true;
}

CRUTable::Type CRUTable::type() const {
  return Type::CRU;
}

const std::string CRUTable::kUpdateTimeField = "update_time";
const std::string CRUTable::kPreviousTimeField = "previous_time";
const std::string CRUTable::kNextTimeField = "next_time";

bool CRUTable::initCRDerived() {
  descriptor_->addField<LogicalTime>(kUpdateTimeField);
  if (FLAGS_cru_linked) {
    descriptor_->addField<LogicalTime>(kPreviousTimeField);
    descriptor_->addField<LogicalTime>(kNextTimeField);
  }
  initCRUDerived();
  return true;
}

bool CRUTable::insertCRDerived(Revision* query) {
  query->set(kUpdateTimeField, LogicalTime::sample());
  if (FLAGS_cru_linked) {
    query->set(kPreviousTimeField, LogicalTime());
    query->set(kNextTimeField, LogicalTime());
  }
  return insertCRUDerived(query);
}

bool CRUTable::bulkInsertCRDerived(const RevisionMap& query) {
  for (const RevisionMap::value_type& item : query) {
    item.second->set(kUpdateTimeField, LogicalTime::sample());
    if (FLAGS_cru_linked) {
      item.second->set(kPreviousTimeField, LogicalTime());
      item.second->set(kNextTimeField, LogicalTime());
    }
  }
  return bulkInsertCRUDerived(query);
}

int CRUTable::findByRevisionCRDerived(
    const std::string& key, const Revision& valueHolder,
    const LogicalTime& time, RevisionMap* dest) {
  return findByRevisionCRUDerived(key, valueHolder, time, dest);
}

}