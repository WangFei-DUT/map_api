#include <type_traits>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <multiagent_mapping_common/test/testing_entrypoint.h>

#include <Poco/Data/Common.h>
#include <Poco/Data/BLOB.h>
#include <Poco/Data/Statement.h>

#include "map-api/cru-table.h"
#include "map-api/id.h"
#include "map-api/map-api-core.h"
#include "map-api/time.h"

#include "test_table.cpp"

using namespace map_api;

template <typename TableType>
class ExpectedFieldCount {
 public:
  static int get();
};

template<>
int ExpectedFieldCount<CRTable>::get() {
  return 2;
}

template<>
int ExpectedFieldCount<CRUTable>::get() {
  return 5;
}

template <typename TableType>
class TableInterfaceTest : public ::testing::Test {};

typedef ::testing::Types<CRTable, CRUTable> TableTypes;
TYPED_TEST_CASE(TableInterfaceTest, TableTypes);

TYPED_TEST(TableInterfaceTest, initEmpty) {
  EXPECT_TRUE(TestTable<TypeParam>::instance().init());
  std::shared_ptr<Revision> structure =
      TestTable<TypeParam>::instance().getTemplate();
  ASSERT_TRUE(static_cast<bool>(structure));
  EXPECT_EQ(ExpectedFieldCount<TypeParam>::get(),
            structure->fieldqueries_size());
  TestTable<TypeParam>::instance().kill();
}

/**
 **********************************************************
 * TEMPLATED TABLE WITH A SINGLE FIELD OF THE TEMPLATE TYPE
 **********************************************************
 */

template <typename _TableType, typename _DataType>
class TableDataTypes {
 public:
  typedef _TableType TableType;
  typedef _DataType DataType;
};

template <typename TableDataType>
class FieldTestTable : public TestTable<typename TableDataType::TableType> {
 public:
  static const std::string kTestField;
  virtual const std::string name() const override {
    return "field_test_table";
  }
 protected:
  virtual void define() {
    this->template addField<typename TableDataType::DataType>(kTestField);
  }
};

template <typename FieldType>
const std::string FieldTestTable<FieldType>::kTestField = "test_field";

template <typename TableDataType>
class InsertReadFieldTestTable : public FieldTestTable<TableDataType> {
 public:
  bool insertQuery(Revision& query) const {
    return this->rawInsert(query);
  }

  bool updateQuery(Revision& query) {
    return this->rawUpdate(query);
  }
};

/**
 **************************************
 * FIXTURES FOR TYPED TABLE FIELD TESTS
 **************************************
 */
template <typename TestedType>
class FieldTest : public ::testing::Test {
 protected:
  /**
   * Sample data for tests. MUST BE NON-DEFAULT!
   */
  TestedType sample_data_1();
  TestedType sample_data_2();
};

template <>
class FieldTest<std::string> : public ::testing::Test {
 protected:
  std::string sample_data_1() {
    return "Test_string_1";
  }
  std::string sample_data_2() {
    return "Test_string_2";
  }
};
template <>
class FieldTest<double> : public ::testing::Test {
 protected:
  double sample_data_1() {
    return 3.14;
  }
  double sample_data_2() {
    return -3.14;
  }
};
template <>
class FieldTest<int32_t> : public ::testing::Test {
 protected:
  int32_t sample_data_1() {
    return 42;
  }
  int32_t sample_data_2() {
    return -42;
  }
};
template <>
class FieldTest<map_api::Id> : public ::testing::Test {
 protected:
  map_api::Id sample_data_1() {
    map_api::Id id;
    id.fromHexString("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    return id;
  }
  map_api::Id sample_data_2() {
    map_api::Id id;
    id.fromHexString("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    return id;
  }
};
template <>
class FieldTest<int64_t> : public ::testing::Test {
 protected:
  int64_t sample_data_1() {
    return 9223372036854775807;
  }
  int64_t sample_data_2() {
    return -9223372036854775807;
  }
};
template <>
class FieldTest<map_api::Time> : public ::testing::Test {
 protected:
  Time sample_data_1() {
    return Time(9223372036854775807);
  }
  Time sample_data_2() {
    return Time(9223372036854775);
  }
};
template <>
class FieldTest<testBlob> : public ::testing::Test {
 protected:
  testBlob sample_data_1() {
    testBlob field;
    *field.mutable_nametype() = map_api::proto::TableFieldDescriptor();
    field.mutable_nametype()->set_name("A name");
    field.mutable_nametype()->
        set_type(map_api::proto::TableFieldDescriptor_Type_DOUBLE);
    field.set_doublevalue(3);
    return field;
  }
  testBlob sample_data_2() {
    testBlob field;
    *field.mutable_nametype() = map_api::proto::TableFieldDescriptor();
    field.mutable_nametype()->set_name("Another name");
    field.mutable_nametype()->
        set_type(map_api::proto::TableFieldDescriptor_Type_INT32);
    field.set_doublevalue(42);
    return field;
  }
};

template <typename TableDataType>
class FieldTestWithoutInit :
    public FieldTest<typename TableDataType::DataType> {
 protected:
  virtual void SetUp() {
    table_ = &InsertReadFieldTestTable<TableDataType>::instance();
  }

  std::shared_ptr<Revision> getTemplate() {
    query_ = this->table_->getTemplate();
    return query_;
  }

  Id fillRevision() {
    getTemplate();
    Id inserted = Id::random();
    query_->set(CRTable::kIdField, inserted);
    // to_insert_->set("owner", Id::random()); TODO(tcies) later, from core
    query_->set(InsertReadFieldTestTable<TableDataType>::kTestField,
                    this->sample_data_1());
    return inserted;
  }

  bool insertRevision() {
    return this->table_->insertQuery(*query_);
  }

  InsertReadFieldTestTable<TableDataType>* table_;
  std::shared_ptr<Revision> query_;
};

template <typename TableDataType>
class FieldTestWithInit : public FieldTestWithoutInit<TableDataType> {
 protected:
  virtual void SetUp() {
    this->table_.reset(new InsertReadFieldTestTable<TableDataType>);
    this->table_->init();
  }
  virtual void TearDown() {
    // TODO(tcies) kill table
  }
};

template <typename TableDataType>
class UpdateFieldTestWithInit : public FieldTestWithInit<TableDataType> {
 protected:
  bool updateRevision() {
    return this->table_->updateQuery(*this->query_);
  }

  void fillRevisionWithOtherData() {
    this->query_->set("test_field", this->sample_data_2());
  }
};

/**
 *************************
 * TYPED TABLE FIELD TESTS
 *************************
 */

#define ALL_DATA_TYPES(table_type) \
    TableDataTypes<table_type, testBlob>, \
    TableDataTypes<table_type, std::string>, \
    TableDataTypes<table_type, int32_t>, \
    TableDataTypes<table_type, double>, \
    TableDataTypes<table_type, map_api::Id>, \
    TableDataTypes<table_type, int64_t>, \
    TableDataTypes<table_type, map_api::Time>

typedef ::testing::Types<
    ALL_DATA_TYPES(CRTable),
    ALL_DATA_TYPES(CRUTable)> CrAndCruTypes;

typedef ::testing::Types<
    ALL_DATA_TYPES(CRUTable)> CruTypes;

TYPED_TEST_CASE(FieldTestWithoutInit, CrAndCruTypes);
TYPED_TEST_CASE(FieldTestWithInit, CrAndCruTypes);
TYPED_TEST_CASE(UpdateFieldTestWithInit, CruTypes);

TYPED_TEST(FieldTestWithInit, Init) {
  EXPECT_EQ(ExpectedFieldCount<typename TypeParam::TableType>::get() + 1,
            this->getTemplate()->fieldqueries_size());
}

TYPED_TEST(FieldTestWithoutInit, CreateBeforeInit) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_DEATH(this->fillRevision(),"^");
}

TYPED_TEST(FieldTestWithoutInit, ReadBeforeInit) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_DEATH(this->table_->rawGetById(Id::random(), Time()), "^");
}

TYPED_TEST(FieldTestWithInit, CreateRead) {
  Id inserted = this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  std::shared_ptr<Revision> rowFromTable =
      this->table_->rawGetById(inserted, Time());
  EXPECT_TRUE(static_cast<bool>(rowFromTable));
  typename TypeParam::DataType dataFromTable;
  rowFromTable->get(InsertReadFieldTestTable<TypeParam>::kTestField,
                    &dataFromTable);
  EXPECT_EQ(this->sample_data_1(), dataFromTable);
}

TYPED_TEST(FieldTestWithInit, ReadInexistentRow) {
  this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  Id other_id = Id::random();
  EXPECT_FALSE(this->table_->rawGetById(other_id, Time()));
}

TYPED_TEST(FieldTestWithInit, ReadInexistentRowData) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  Id inserted = this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  std::shared_ptr<Revision> rowFromTable =
      this->table_->rawGetById(inserted, Time());
  EXPECT_TRUE(static_cast<bool>(rowFromTable));
  typename TypeParam::DataType dataFromTable;
  EXPECT_DEATH(rowFromTable->get("some_other_field", &dataFromTable), "^");
}

TYPED_TEST(UpdateFieldTestWithInit, UpdateRead) {
  Id inserted = this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  std::shared_ptr<Revision> rowFromTable =
      this->table_->rawGetById(inserted, Time());
  EXPECT_TRUE(static_cast<bool>(rowFromTable));
  typename TypeParam::DataType dataFromTable;
  rowFromTable->get("test_field", &dataFromTable);
  EXPECT_EQ(this->sample_data_1(), dataFromTable);

  this->fillRevisionWithOtherData();
  this->updateRevision();
  rowFromTable = this->table_->rawGetById(inserted, Time());
  EXPECT_TRUE(static_cast<bool>(rowFromTable));
  rowFromTable->get("test_field", &dataFromTable);
  EXPECT_EQ(this->sample_data_2(), dataFromTable);
}

MULTIAGENT_MAPPING_UNITTEST_ENTRYPOINT
