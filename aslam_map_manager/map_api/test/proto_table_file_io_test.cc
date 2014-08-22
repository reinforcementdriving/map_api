#include <fstream>  // NOLINT
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <multiagent_mapping_common/test/testing_entrypoint.h>

#include "map-api/ipc.h"
#include "map-api/net-table-transaction.h"
#include "map-api/transaction.h"
#include "map-api/proto-table-file-io.h"

#include "net_table_test_fixture.cc"

using namespace map_api;  // NOLINT

TEST_P(NetTableTest, SaveAndRestoreFromFile) {
  Chunk* chunk = table_->newChunk();
  CHECK_NOTNULL(chunk);
  Id chunk_id = chunk->id();
  Id item_1_id = Id::generate();
  Id item_2_id = Id::generate();
  {
    Transaction transaction;
    std::shared_ptr<Revision> to_insert_1 = table_->getTemplate();
    to_insert_1->set(CRTable::kIdField, item_1_id);
    to_insert_1->set(kFieldName, 42);
    std::shared_ptr<Revision> to_insert_2 = table_->getTemplate();
    to_insert_2->set(CRTable::kIdField, item_2_id);
    to_insert_2->set(kFieldName, 21);
    transaction.insert(table_, chunk, to_insert_1);
    transaction.insert(table_, chunk, to_insert_2);
    ASSERT_TRUE(transaction.commit());
    CRTable::RevisionMap retrieved;
    LogicalTime dumptime = LogicalTime::sample();
    chunk->dumpItems(dumptime, &retrieved);
    ASSERT_EQ(2, retrieved.size());
    CRTable::RevisionMap::iterator it = retrieved.find(item_1_id);
    ASSERT_FALSE(it == retrieved.end());
    LogicalTime time_1, time_2;
    int item_1, item_2;
    it->second->get(CRTable::kInsertTimeField, &time_1);
    it->second->get(kFieldName, &item_1);
    it = retrieved.find(item_2_id);
    ASSERT_FALSE(it == retrieved.end());
    it->second->get(CRTable::kInsertTimeField, &time_2);
    it->second->get(kFieldName, &item_2);
    EXPECT_EQ(time_1.serialize(), time_2.serialize());
    EXPECT_EQ(item_1, 42);
    EXPECT_EQ(item_2, 21);
  }

  const std::string test_filename = "./test_dump.table";
  // Drop all existing contents.
  std::fstream file;
  file.open(test_filename, std::fstream::binary | std::fstream::in |
                               std::fstream::out | std::fstream::trunc);

  {
    ProtoTableFileIO file_io(test_filename, table_);
    EXPECT_TRUE(file_io.StoreTableContents(LogicalTime::sample()));
  }

  // Reset the state of the database.
  TearDown();
  SetUp();

  {
    ProtoTableFileIO file_io(test_filename, table_);
    ASSERT_TRUE(file_io.RestoreTableContents());
  }

  {
    chunk = table_->getChunk(chunk_id);
    ASSERT_FALSE(chunk == nullptr);
    CRTable::RevisionMap retrieved;
    LogicalTime time_1, time_2;
    int item_1, item_2;
    LogicalTime dumptime = LogicalTime::sample();
    chunk->dumpItems(dumptime, &retrieved);
    ASSERT_EQ(2, retrieved.size());
    CRTable::RevisionMap::iterator it = retrieved.find(item_1_id);
    ASSERT_FALSE(it == retrieved.end());
    it->second->get(CRTable::kInsertTimeField, &time_1);
    it->second->get(kFieldName, &item_1);
    EXPECT_EQ(item_1, 42);

    it = retrieved.find(item_2_id);
    ASSERT_FALSE(it == retrieved.end());
    it->second->get(CRTable::kInsertTimeField, &time_2);
    it->second->get(kFieldName, &item_2);
    EXPECT_EQ(item_2, 21);

    EXPECT_EQ(time_1.serialize(), time_2.serialize());
  }
}

MULTIAGENT_MAPPING_UNITTEST_ENTRYPOINT