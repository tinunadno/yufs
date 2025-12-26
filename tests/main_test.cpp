#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <algorithm>

extern "C" {
#include "yufs_core.h"
}

const uint32_t ROOT_ID = 1000;

class YufsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(YUFSCore_init(), 0);
    }

    void TearDown() override {
        YUFSCore_destroy();
    }
};

TEST_F(YufsTest, RootExists) {
    struct YUFS_stat stat;

    int res = YUFSCore_getattr(ROOT_ID, &stat);
    ASSERT_EQ(res, 0);
    EXPECT_EQ(stat.id, ROOT_ID);
    EXPECT_TRUE((stat.mode & S_IFMT) == S_IFDIR);
}
TEST_F(YufsTest, CreateAndLookupFile) {
    struct YUFS_stat stat;


    int res = YUFSCore_create(ROOT_ID, "hello.txt", 0644 | S_IFREG, &stat);
    ASSERT_EQ(res, 0);
    uint32_t file_id = stat.id;
    EXPECT_NE(file_id, 0);


    struct YUFS_stat lookup_stat;
    res = YUFSCore_lookup(ROOT_ID, "hello.txt", &lookup_stat);
    ASSERT_EQ(res, 0);
    EXPECT_EQ(lookup_stat.id, file_id);


    res = YUFSCore_lookup(ROOT_ID, "missing.txt", &lookup_stat);
    EXPECT_NE(res, 0);
}


TEST_F(YufsTest, ReadWriteFile) {
    struct YUFS_stat stat;
    YUFSCore_create(ROOT_ID, "data.bin", 0644 | S_IFREG, &stat);
    uint32_t fid = stat.id;

    const char *text = "Hello, World!";
    size_t len = strlen(text);


    int written = YUFSCore_write(fid, text, len, 0);
    EXPECT_EQ(written, len);


    YUFSCore_getattr(fid, &stat);
    EXPECT_EQ(stat.size, len);


    char buf[100];
    memset(buf, 0, sizeof(buf));
    int read = YUFSCore_read(fid, buf, len, 0);
    EXPECT_EQ(read, len);
    EXPECT_STREQ(buf, text);


    const char *append = " YUFS";
    YUFSCore_write(fid, append, strlen(append), len);

    memset(buf, 0, sizeof(buf));
    YUFSCore_read(fid, buf, 100, 0);
    EXPECT_STREQ(buf, "Hello, World! YUFS");
}


bool test_filldir_callback(void *ctx, const char *name, int name_len, uint32_t id, umode_t type) {
    auto *vec = static_cast<std::vector<std::string> *>(ctx);
    vec->push_back(std::string(name));
    return true;
}

TEST_F(YufsTest, DirectoryHierarchyAndIteration) {

    struct YUFS_stat s_folder, s_file, s_nested;


    ASSERT_EQ(YUFSCore_create(ROOT_ID, "folder1", 0755 | S_IFDIR, &s_folder), 0);
    ASSERT_EQ(YUFSCore_create(ROOT_ID, "file_in_root.txt", 0644 | S_IFREG, &s_file), 0);
    ASSERT_EQ(YUFSCore_create(s_folder.id, "nested.txt", 0644 | S_IFREG, &s_nested), 0);


    std::vector<std::string> root_content;
    YUFSCore_iterate(ROOT_ID, test_filldir_callback, &root_content, 0);


    EXPECT_GE(root_content.size(), 4);
    EXPECT_TRUE(std::find(root_content.begin(), root_content.end(), "folder1") != root_content.end());
    EXPECT_TRUE(std::find(root_content.begin(), root_content.end(), "file_in_root.txt") != root_content.end());


    std::vector<std::string> folder_content;
    YUFSCore_iterate(s_folder.id, test_filldir_callback, &folder_content, 0);


    EXPECT_GE(folder_content.size(), 3);
    EXPECT_TRUE(std::find(folder_content.begin(), folder_content.end(), "nested.txt") != folder_content.end());


    struct YUFS_stat lookup_res;
    ASSERT_EQ(YUFSCore_lookup(s_folder.id, "nested.txt", &lookup_res), 0);
    EXPECT_EQ(lookup_res.id, s_nested.id);
}


TEST_F(YufsTest, DeleteLogic) {
    struct YUFS_stat s_dir, s_file;
    YUFSCore_create(ROOT_ID, "mydir", 0755 | S_IFDIR, &s_dir);
    YUFSCore_create(s_dir.id, "file.txt", 0644 | S_IFREG, &s_file);


    int res = YUFSCore_rmdir(ROOT_ID, "mydir");
    EXPECT_NE(res, 0);


    res = YUFSCore_unlink(s_dir.id, "file.txt");
    EXPECT_EQ(res, 0);


    struct YUFS_stat dummy;
    EXPECT_NE(YUFSCore_lookup(s_dir.id, "file.txt", &dummy), 0);


    res = YUFSCore_rmdir(ROOT_ID, "mydir");
    EXPECT_EQ(res, 0);


    EXPECT_NE(YUFSCore_lookup(ROOT_ID, "mydir", &dummy), 0);
}
