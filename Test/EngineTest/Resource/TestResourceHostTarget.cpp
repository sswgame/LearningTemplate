#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/Resource/Image/DDSLoader.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/HostTargetTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    constexpr const utf8* kTexturePath      = "engine/textures/perlin.dds";
    constexpr const utf8* kShaderBinaryPath = "engine/shaders/bin/dx12/deferredlighting_ps.dxil";
    constexpr const utf8* kPipelinePath     = "engine/pipeline/forwardpipeline.xml";
} // namespace

/**
 * @brief [ResourceHostTargetTest] 전용 서버 호스트는 텍스처 · 셰이더 바이너리를 없는 것으로 친다 — 읽기는 조용히 false, 경고 · 오류가 없다
 * @details 서버 패키지에는 그 종류가 없다(쿠킹 표 target_excluded_asset_kinds — CookAssets --build-target Server). 서버 로더가 그것을 찾으면
 *          "파일 없음" 오류가 난다 — 그래서 런타임도 같은 표로 그 종류를 요청 단계에서 막는다. `readResourceCommon` 의 호스트 검사를 빼면 이 시험이 진다
 *          (디스크의 파일이 읽힌다).
 */
SW_TEST_CASE( ResourceHostTargetTest, DedicatedServerDoesNotReadExcludedKinds )
{
    if ( test::HostTargetTestUtil::isLeftOutOfServerPackage( kTexturePath ) )
        SW_TEST_SKIP( "the dedicated server package leaves textures, shader binaries and audio out (CookContract target_excluded_asset_kinds)" );
    SW_ASSERT_TRUE_MSG( sw::ResourceUtil::hasResource( kTexturePath ), kTexturePath );
    SW_ASSERT_TRUE_MSG( sw::ResourceUtil::hasResource( kShaderBinaryPath ), kShaderBinaryPath );

    test::ScopedLogCollector     logCollector;
    const test::ScopedHostTarget hostTarget{ "Server" };
    SW_EXPECT_TRUE( sw::ResourceUtil::isExcludedForHost( kTexturePath ) );
    SW_EXPECT_TRUE( sw::ResourceUtil::isExcludedForHost( kShaderBinaryPath ) );
    SW_EXPECT_TRUE( sw::ResourceUtil::isExcludedForHost( "game/abilityarena/sounds/error_004.ogg" ) );
    SW_EXPECT_FALSE( sw::ResourceUtil::isExcludedForHost( kPipelinePath ) );
    SW_EXPECT_FALSE( sw::ResourceUtil::isExcludedForHost( "game/empty/models/crate.mesh" ) );
    SW_EXPECT_FALSE( sw::ResourceUtil::isExcludedForHost( "engine/myshaders/binary/x.txt" ) ); // 폴더 조각은 경계에서만 맞는다

    sw::vector<uint8> bytes;
    SW_EXPECT_FALSE( sw::ResourceUtil::hasResource( kTexturePath ) );
    SW_EXPECT_FALSE( sw::ResourceUtil::readBinaryResource( kTexturePath, bytes ) );
    SW_EXPECT_FALSE( sw::ResourceUtil::readBinaryResource( kShaderBinaryPath, bytes ) );
    SW_EXPECT_TRUE( sw::ResourceUtil::readBinaryResource( kPipelinePath, bytes ) );
    sw::DDSImageData image; // 로더도 조용히 진다(서버가 지형 스플랫 · 머티리얼 텍스처를 찾을 때)
    SW_EXPECT_FALSE( sw::DDSLoader::loadFromResource( kTexturePath, image ) );
    SW_EXPECT_TRUE_MSG( logCollector.joined().empty(), logCollector.joined().c_str() );
}

/**
 * @brief [ResourceHostTargetTest] 클라이언트 호스트(표에 빼는 종류가 없다)와 시험 하네스는 모든 종류를 읽는다
 */
SW_TEST_CASE( ResourceHostTargetTest, ClientReadsEveryKind )
{
    if ( test::HostTargetTestUtil::isLeftOutOfServerPackage( kTexturePath ) )
        SW_TEST_SKIP( "the dedicated server package leaves textures, shader binaries and audio out (CookContract target_excluded_asset_kinds)" );
    const test::ScopedHostTarget hostTarget{ "Client" };
    sw::vector<uint8>            bytes;
    SW_EXPECT_FALSE( sw::ResourceUtil::isExcludedForHost( kTexturePath ) );
    SW_EXPECT_TRUE( sw::ResourceUtil::readBinaryResource( kTexturePath, bytes ) );
    SW_EXPECT_TRUE( sw::ResourceUtil::readBinaryResource( kShaderBinaryPath, bytes ) );
}
