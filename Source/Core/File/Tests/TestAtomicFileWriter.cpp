#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

// AtomicFileWriter — 조각으로 이어 쓰고 머리를 나중에 덮은 뒤 바꿔 끼우기, commit 없이 끝나면 원본이 그대로이고 임시 파일이 남지 않는다.

namespace
{
    /** @brief @p directory 안의 파일 수입니다(임시 파일이 남았는지 본다). */
    size_t countFiles( const sw::AbsolutePath& directory )
    {
        sw::vector<sw::AbsolutePath> listFile;
        (void)sw::FileUtil::collectFiles( directory, "", listFile, false ); // 비었으면 빈 목록
        return listFile.size();
    }
} // namespace

/** @brief [AtomicFileWriterTest] 이어 쓴 조각과 나중에 덮은 머리가 바꿔 끼운 파일에 그대로 있다. */
SW_TEST_CASE( AtomicFileWriterTest, AppendedChunksAndPatchedHeaderLand )
{
    const sw::AbsolutePath path = sw::test::makeScratchPath( "stream.bin" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( path, "old" ) );
    sw::AtomicFileWriter writer;
    SW_REQUIRE_TRUE( writer.open( path ).has_value() );
    const uint8 arrHeader[4]{ 0, 0, 0, 0 };
    const uint8 arrBody[3]{ 'a', 'b', 'c' };
    SW_REQUIRE_TRUE( writer.append( arrHeader, sizeof( arrHeader ) ).has_value() );
    SW_REQUIRE_TRUE( writer.append( arrBody, sizeof( arrBody ) ).has_value() );
    SW_REQUIRE_TRUE( writer.append( arrBody, sizeof( arrBody ) ).has_value() );
    const uint8 arrPatched[4]{ 'H', 'E', 'A', 'D' };
    SW_REQUIRE_TRUE( writer.writeAt( 0, arrPatched, sizeof( arrPatched ) ).has_value() );
    SW_EXPECT_FALSE( writer.writeAt( 8, arrPatched, sizeof( arrPatched ) ).has_value() ); // 쓴 끝(10)을 넘는다
    SW_EXPECT_EQUAL( writer.getSize(), static_cast<uint64>( 10 ) );
    SW_REQUIRE_TRUE( writer.commit().has_value() );

    sw::string text;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( path, text ) );
    SW_EXPECT_TRUE( text == "HEADabcabc" );
    SW_EXPECT_EQUAL( countFiles( path.getParent() ), size_t{ 1 } );
}

/** @brief [AtomicFileWriterTest] commit 없이 끝난 쓰기는 원본을 건드리지 않고 임시 파일을 지운다. */
SW_TEST_CASE( AtomicFileWriterTest, AbandonedWriteLeavesTheOriginal )
{
    const sw::AbsolutePath path = sw::test::makeScratchPath( "keep.bin" );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( path, "original" ) );
    {
        sw::AtomicFileWriter writer;
        SW_REQUIRE_TRUE( writer.open( path ).has_value() );
        const uint8 arrBody[3]{ 'x', 'y', 'z' };
        SW_REQUIRE_TRUE( writer.append( arrBody, sizeof( arrBody ) ).has_value() );
    }
    sw::string text;
    SW_REQUIRE_TRUE( sw::FileUtil::readTextFile( path, text ) );
    SW_EXPECT_TRUE( text == "original" );
    SW_EXPECT_EQUAL( countFiles( path.getParent() ), size_t{ 1 } );
}
