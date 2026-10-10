#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleBuildID.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_LINUX )
    #include <gnu/libc-version.h>
#endif

// 모듈 빌드 id — 실행 파일과 다른 모듈(OS 라이브러리)의 빌드 id 를 올라온 이미지에서 읽는다. 심볼 서버 열쇠의 모양(16진)을 지킨다.

namespace
{
    struct ModuleBuildIDTestInternal
    {
        static bool isHex( sw::string_view text )
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
    };
} // namespace

/**
 * @brief [ModuleBuildIDTest] 실행 파일의 빌드 id 는 16진 열쇠이고(Windows 는 GUID 32 자리 + age, PDB 경로 포함) 같은 모듈이면 같은 값이다
 */
SW_TEST_CASE( ModuleBuildIDTest, ExecutableHasAStableSymbolKey )
{
    const sw::ModuleBuildID executable = sw::ModuleBuildID::find( nullptr );
    SW_EXPECT_FALSE( executable._modulePath.empty() );
    // 모든 구성이 서명을 적는다 — Windows 는 /DEBUG 의 PE CodeView RSDS, 리눅스는 --build-id 의 ELF 노트(cmake/Modules/Compiler/Clang.cmake).
    SW_ASSERT_TRUE_MSG( executable.isValid(), "every configuration links with a build id (Windows /DEBUG RSDS, Linux --build-id)" );
    SW_EXPECT_TRUE_MSG( ModuleBuildIDTestInternal::isHex( executable._id ), executable._id.c_str() );
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_TRUE( executable._id.size() >= 33 );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::endsWith( executable._debugFile, ".pdb", true ), executable._debugFile.c_str() );
    #if defined( SW_RELEASE )
    // Release 는 PDB 이름만 적는다(/PDBALTPATH:%_PDB%) — 빌드 기계의 경로가 배포물에 새지 않는다.
    SW_EXPECT_TRUE_MSG( sw::FileUtil::getFileNamePart( executable._debugFile ) == executable._debugFile, executable._debugFile.c_str() );
    #endif
#endif
    // 이 함수의 주소도 실행 파일 안이다(Core 는 시험 실행 파일에 들어 있다).
    const sw::ModuleBuildID self = sw::ModuleBuildID::find( reinterpret_cast<const void*>( &ModuleBuildIDTestInternal::isHex ) );
    SW_EXPECT_TRUE( self._id == executable._id );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( self._modulePath, executable._modulePath ) );
}

/**
 * @brief [ModuleBuildIDTest] 다른 모듈의 주소는 그 모듈의 빌드 id 를 준다(OS 라이브러리)
 */
SW_TEST_CASE( ModuleBuildIDTest, AddressInsideAnotherModuleNamesThatModule )
{
    const sw::ModuleBuildID executable = sw::ModuleBuildID::find( nullptr );
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ModuleBuildID system = sw::ModuleBuildID::find( reinterpret_cast<const void*>( &GetTickCount64 ) );
    SW_ASSERT_TRUE( system.isValid() );
    SW_EXPECT_TRUE( system._id != executable._id );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( system._modulePath, ".dll", true ), system._modulePath.c_str() );
#else
    // libc 함수 중 새니타이저가 가로채지 않는 것을 고른다 — ASan · TSan 은 fflush · malloc 같은 함수를 실행 파일에 정적으로 든 런타임에서
    // 가로채므로, 그 주소는 libc 가 아니라 실행 파일 안이다.
    const sw::ModuleBuildID system = sw::ModuleBuildID::find( reinterpret_cast<const void*>( &gnu_get_libc_version ) );
    SW_EXPECT_FALSE( system._modulePath.empty() );
    SW_EXPECT_FALSE( sw::FileUtil::pathsEqualNormalized( system._modulePath, executable._modulePath ) );
#endif
}
