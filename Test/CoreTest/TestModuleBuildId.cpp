#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/ModuleBuildId.h"
#include "Core/String/StringUtil.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_LINUX )
    #include <gnu/libc-version.h>
#endif

// 모듈 빌드 id — 실행 파일과 다른 모듈(OS 라이브러리)의 빌드 id 를 올라온 이미지에서 읽는다. 심볼 서버 열쇠의 모양(16진)을 지킨다.

namespace
{
    struct ModuleBuildIdTestInternal
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
 * @brief [ModuleBuildIdTest] 실행 파일의 빌드 id 는 16진 열쇠이고(Windows 는 GUID 32 자리 + age, PDB 경로 포함) 같은 모듈이면 같은 값이다
 */
SW_TEST_CASE( ModuleBuildIdTest, ExecutableHasAStableSymbolKey )
{
    const sw::ModuleBuildId executable = sw::ModuleBuildId::find( nullptr );
    SW_EXPECT_FALSE( executable._modulePath.empty() );
#if defined( SW_PLATFORM_WINDOWS ) && !defined( SW_SHIPPING )
    SW_ASSERT_TRUE_MSG( executable.isValid(), "the Dev test executable is linked with /DEBUG and must carry an RSDS record" );
    SW_EXPECT_TRUE( executable._id.size() >= 33 );
    SW_EXPECT_TRUE_MSG( ModuleBuildIdTestInternal::isHex( executable._id ), executable._id.c_str() );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::endsWith( executable._debugFile, ".pdb", true ), executable._debugFile.c_str() );
#else
    // 링커가 서명 · build-id 를 적지 않았으면 빈 값이다(지금 Shipping 은 /DEBUG 없이 링크한다 — PDB 가 없다).
    if ( executable.isValid() )
        SW_EXPECT_TRUE_MSG( ModuleBuildIdTestInternal::isHex( executable._id ), executable._id.c_str() );
#endif
    // 이 함수의 주소도 실행 파일 안이다(Core 는 시험 실행 파일에 들어 있다).
    const sw::ModuleBuildId self = sw::ModuleBuildId::find( reinterpret_cast<const void*>( &ModuleBuildIdTestInternal::isHex ) );
    SW_EXPECT_TRUE( self._id == executable._id );
    SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( self._modulePath, executable._modulePath ) );
}

/**
 * @brief [ModuleBuildIdTest] 다른 모듈의 주소는 그 모듈의 빌드 id 를 준다(OS 라이브러리)
 */
SW_TEST_CASE( ModuleBuildIdTest, AddressInsideAnotherModuleNamesThatModule )
{
    const sw::ModuleBuildId executable = sw::ModuleBuildId::find( nullptr );
#if defined( SW_PLATFORM_WINDOWS )
    const sw::ModuleBuildId system = sw::ModuleBuildId::find( reinterpret_cast<const void*>( &GetTickCount64 ) );
    SW_ASSERT_TRUE( system.isValid() );
    SW_EXPECT_TRUE( system._id != executable._id );
    SW_EXPECT_TRUE_MSG( sw::StringUtil::contains( system._modulePath, ".dll", true ), system._modulePath.c_str() );
#else
    // libc 함수 중 새니타이저가 가로채지 않는 것을 고른다 — ASan · TSan 은 fflush · malloc 같은 함수를 실행 파일에 정적으로 든 런타임에서
    // 가로채므로, 그 주소는 libc 가 아니라 실행 파일 안이다.
    const sw::ModuleBuildId system = sw::ModuleBuildId::find( reinterpret_cast<const void*>( &gnu_get_libc_version ) );
    SW_EXPECT_FALSE( system._modulePath.empty() );
    SW_EXPECT_FALSE( sw::FileUtil::pathsEqualNormalized( system._modulePath, executable._modulePath ) );
#endif
}
