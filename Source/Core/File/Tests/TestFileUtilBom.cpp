// FileUtil 의 UTF-8 BOM — 뷰 · 바이트 포인터에서 떼기와 텍스트 파일 읽기가 BOM 을 뗀다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [FileUtilBomTest] FileUtil::skipUTF8Bom 및 BOM 포함 텍스트 파일 읽기 검증
 */
SW_TEST_CASE( FileUtilBomTest, SkipsTheUTF8BomInViewsBytesAndTextFiles )
{
    // 1) string_view 기반 BOM 스킵 검증
    const utf8* pWithBom    = "\xEF\xBB\xBFHello UTF-8 BOM!";
    const utf8* pWithoutBom = "Hello Without BOM!";

    SW_EXPECT_EQUAL( std::string_view( "Hello UTF-8 BOM!" ), sw::FileUtil::skipUTF8Bom( pWithBom ) );
    SW_EXPECT_EQUAL( std::string_view( pWithoutBom ), sw::FileUtil::skipUTF8Bom( pWithoutBom ) );

    // 2) 포인터 및 크기 기반 BOM 스킵 검증
    const uint8* pBytes = reinterpret_cast<const uint8*>( pWithBom );
    size_t       size   = sw::StringUtil::strlen( pWithBom );
    sw::FileUtil::skipUTF8Bom( pBytes, size );
    SW_EXPECT_EQUAL( size_t{ sw::StringUtil::strlen( "Hello UTF-8 BOM!" ) }, size );
    SW_EXPECT_EQUAL( 'H', static_cast<utf8>( pBytes[0] ) );

    // 3) 파일 I/O 자동 BOM 제거 검증
    const sw::string bomFilePath = test::makeScratchPath( "test_bom.txt" );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( bomFilePath, reinterpret_cast<const uint8*>( pWithBom ), sw::StringUtil::strlen( pWithBom ) ) );

    sw::string readText;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( bomFilePath, readText ) );
    SW_EXPECT_EQUAL( sw::string_view( "Hello UTF-8 BOM!" ), sw::string_view( readText.c_str() ) );
}
