// ModuleBuildID — 실행 파일 · 이 코드가 든 모듈 · OS 라이브러리의 빌드 id 를 올라온 이미지에서 읽고, 심볼 서버 열쇠의 모양(16진)을 지키는지.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleBuildID.h"
#include "Core/Testing/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"
#else
    #include <gnu/libc-version.h>
#endif

using namespace sw;

namespace
{
    bool isModuleBuildIDHex( string_view text )
    {
        for ( const utf8 character : text )
        {
            const bool bDigit = '0' <= character && character <= '9';
            const bool bLower = 'a' <= character && character <= 'f';
            const bool bUpper = 'A' <= character && character <= 'F';
            if ( ( bDigit || bLower || bUpper ) == false )
                return false;
        }
        return text.empty() == false;
    }
} // namespace

/**
 * @brief 실행 파일의 빌드 id 는 16진 열쇠다(Windows 는 GUID 32 자리 + age, PDB 경로 포함).
 * @details 모든 구성이 서명을 적는다 — Windows 는 /DEBUG 의 PE CodeView RSDS, 리눅스는 --build-id 의 ELF 노트(cmake/Modules/Compiler/Clang.cmake).
 */
SW_TEST_CASE( ModuleBuildIDTest, ExecutableHasASymbolKey )
{
    const ModuleBuildID executable = ModuleBuildID::find( nullptr );
    SW_EXPECT_FALSE( executable._modulePath.empty() );
    SW_REQUIRE_TRUE_MSG( executable.isValid(), "every configuration links with a build id (Windows /DEBUG RSDS, Linux --build-id)" );
    SW_EXPECT_TRUE_MSG( isModuleBuildIDHex( executable._id ), executable._id );
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_TRUE( executable._id.size() >= 33 );
    SW_EXPECT_TRUE_MSG( StringUtil::endsWith( executable._debugFile, ".pdb", true ), executable._debugFile );
    #if defined( SW_RELEASE )
    // Release 는 PDB 이름만 적는다(/PDBALTPATH:%_PDB%) — 빌드 기계의 경로가 배포물에 새지 않는다.
    SW_EXPECT_TRUE_MSG( FileUtil::getFileNamePart( executable._debugFile ) == executable._debugFile, executable._debugFile );
    #endif
#endif
}

/**
 * @brief 이 코드가 든 모듈 안의 두 주소는 같은 빌드 id · 경로를 준다.
 * @details Dev 는 Core 가 Engine.dll 에 들어 실행 파일과 다른 모듈이므로(옛 테스트는 실행 파일과 같다고 기대해 졌다) 같은 모듈 안 두 주소끼리 비교한다.
 */
SW_TEST_CASE( ModuleBuildIDTest, AddressesInsideOneModuleShareItsKey )
{
    const ModuleBuildID self  = ModuleBuildID::find( reinterpret_cast<const void*>( &isModuleBuildIDHex ) );
    const ModuleBuildID other = ModuleBuildID::find( reinterpret_cast<const void*>( &ModuleBuildID::find ) );
    SW_REQUIRE_TRUE( self.isValid() );
    SW_EXPECT_TRUE_MSG( isModuleBuildIDHex( self._id ), self._id );
    SW_EXPECT_EQUAL( self._id, other._id );
    SW_EXPECT_TRUE( FileUtil::pathsEqualNormalized( self._modulePath, other._modulePath ) );
}

/**
 * @brief 다른 모듈(OS 라이브러리)의 주소는 그 모듈의 빌드 id 를 준다.
 */
SW_TEST_CASE( ModuleBuildIDTest, AddressInsideAnotherModuleNamesThatModule )
{
    const ModuleBuildID executable = ModuleBuildID::find( nullptr );
#if defined( SW_PLATFORM_WINDOWS )
    const ModuleBuildID system = ModuleBuildID::find( reinterpret_cast<const void*>( &GetTickCount64 ) );
    SW_REQUIRE_TRUE( system.isValid() );
    SW_EXPECT_TRUE( system._id != executable._id );
    SW_EXPECT_TRUE_MSG( StringUtil::contains( system._modulePath, ".dll", true ), system._modulePath );
#else
    // libc 함수 중 새니타이저가 가로채지 않는 것을 고른다 — ASan · TSan 은 fflush · malloc 같은 함수를 실행 파일에 정적으로 든 런타임에서
    // 가로채므로, 그 주소는 libc 가 아니라 실행 파일 안이다.
    const ModuleBuildID system = ModuleBuildID::find( reinterpret_cast<const void*>( &gnu_get_libc_version ) );
    SW_EXPECT_FALSE( system._modulePath.empty() );
    SW_EXPECT_FALSE( FileUtil::pathsEqualNormalized( system._modulePath, executable._modulePath ) );
#endif
}
