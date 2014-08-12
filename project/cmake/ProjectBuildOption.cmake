# 默认配置选项
#####################################################################
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs)." OFF)

# atbus 选项
set(ATBUS_MACRO_BUSID_TYPE "uint64_t" CACHE STRING "atbus的busid类型")
set(ATBUS_MACRO_DATA_NODE_SIZE 128 CACHE STRING "atbus的内存通道node大小（必须是2的倍数）")
set(ATBUS_MACRO_DATA_ALIGN_TYPE "uint64_t" CACHE STRING "atbus的内存内存块对齐类型（用于优化memcpy和crc校验）")


# 测试配置选项
set(GTEST_ROOT "" CACHE STRING "GTest root directory")
set(BOOST_ROOT "" CACHE STRING "Boost root directory")
option(PROJECT_TEST_ENABLE_BOOST_UNIT_TEST "Enable boost unit test." OFF)

