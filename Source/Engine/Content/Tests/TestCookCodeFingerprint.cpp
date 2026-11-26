// CookCodeFingerprintTest — 쿠킹 키에 들어가는 코드 지문은 빌드가 산출한 표에서 온다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/CookCodeFingerprint.h"

using namespace sw;

/** @brief [CookCodeFingerprintTest] 쿠커마다 지문이 있고 서로 다르다. 목록에 없는 이름은 0 이다 */
SW_TEST_CASE( CookCodeFingerprintTest, EveryCookerHasItsOwnFingerprint )
{
    vector<uint64> listSeen;
    for ( const utf8* pName : { "Scene", "ScenePreload", "SceneHLODMesh", "Prefab", "NavMesh", "VertexAnimation", "Pack", "ShaderCompiler" } )
    {
        const uint64 fingerprint = CookCodeFingerprint::find( pName );
        SW_EXPECT_TRUE_MSG( fingerprint != 0, pName );
        for ( const uint64 seen : listSeen )
        {
            SW_EXPECT_TRUE_MSG( seen != fingerprint, pName );
        }
        listSeen.push_back( fingerprint );
    }
    SW_EXPECT_EQUAL( uint64( 0 ), CookCodeFingerprint::find( "Nothing" ) );
}

/**
 * @brief [CookCodeFingerprintTest] 셰이더 도구 버전은 vcpkg 설치 기록에서 온 `directx-dxc <판>; dawn <판>` 이다
 * @details 예전에는 DXC 판만 실어 Tint(dawn) 를 올려도 WGSL 셰이더 DDC · 쿠킹본이 낡지 않았다.
 */
SW_TEST_CASE( CookCodeFingerprintTest, ShaderCompilerVersionComesFromTheInstallRecord )
{
    const string version = CookCodeFingerprint::getShaderCompilerVersion();
    SW_EXPECT_TRUE( version.find( "directx-dxc " ) == 0 );
    const size_t dawnAt = version.find( "; dawn " );
    SW_REQUIRE_TRUE( dawnAt != string::npos );
    SW_EXPECT_TRUE( dawnAt > string_view( "directx-dxc " ).size() );
    SW_EXPECT_TRUE( version.size() > dawnAt + string_view( "; dawn " ).size() );
}
