// 메시 모디파이어 텍스처 표본기 — 텍셀 중심의 원색, 이중선형 중간값, 반복 래핑, 없는 파일 · 빈 경로, 캐시 비우기 뒤 다시 읽기.
#include "pch.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierTextureSampler.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TextureSamplerTestInternal
    {
        /** @brief 비압축 32 비트 TGA(위가 첫 행) 2×2 를 씁니다. 텍셀 순서는 (0,0) (1,0) (0,1) (1,1), 각 RGBA 입니다. */
        [[nodiscard]] static bool writeTga2x2( const string& path, const uint8 ( &listRgba )[16] )
        {
            vector<uint8> bytes( 18, 0 );
            bytes[2]  = 2;    // 비압축 트루컬러
            bytes[12] = 2;    // 가로
            bytes[14] = 2;    // 세로
            bytes[16] = 32;   // 텍셀당 비트
            bytes[17] = 0x28; // 알파 8 비트 + 위가 원점
            for ( uint32 texelIndex = 0; texelIndex < 4; ++texelIndex )
            {
                bytes.push_back( listRgba[texelIndex * 4 + 2] ); // TGA 는 BGRA 순서
                bytes.push_back( listRgba[texelIndex * 4 + 1] );
                bytes.push_back( listRgba[texelIndex * 4 + 0] );
                bytes.push_back( listRgba[texelIndex * 4 + 3] );
            }
            return FileUtil::writeFile( path, bytes.data(), bytes.size() );
        }

        static void expectColor( const float4& expected, const float4& actual )
        {
            SW_EXPECT_NEAR_EQUAL( expected._x, actual._x, 0.01f );
            SW_EXPECT_NEAR_EQUAL( expected._y, actual._y, 0.01f );
            SW_EXPECT_NEAR_EQUAL( expected._z, actual._z, 0.01f );
            SW_EXPECT_NEAR_EQUAL( expected._w, actual._w, 0.01f );
        }

        /** @brief 빨강 · 초록(윗줄), 파랑 · 흰색(아랫줄)입니다. */
        static constexpr uint8 kListPrimary[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
    };
} // namespace

SW_TEST_CASE( MeshModifierTextureSamplerTest, TexelCentersBilinearAndRepeat )
{
    MeshModifierTextureSampler sampler; // 캐시는 표본기 인스턴스의 것이라 테스트마다 비어 시작한다
    const string               path = test::makeTempDirectory( "meshmodifier_texture_sampler" ) + "/primary.tga";
    SW_EXPECT_TRUE( TextureSamplerTestInternal::writeTga2x2( path, TextureSamplerTestInternal::kListPrimary ) );

    float4 color{};
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.25f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 1.0f, 0.0f, 0.0f, 1.0f }, color );
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.75f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 0.0f, 1.0f, 0.0f, 1.0f }, color );
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.25f, 0.75f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 0.0f, 0.0f, 1.0f, 1.0f }, color );
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.75f, 0.75f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 1.0f, 1.0f, 1.0f, 1.0f }, color );

    // 빨강과 초록 사이 한가운데.
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.5f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 0.5f, 0.5f, 0.0f, 1.0f }, color );
    // 네 텍셀의 가운데.
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.5f, 0.5f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 0.5f, 0.5f, 0.5f, 1.0f }, color );

    // 반복: 한 바퀴 밖은 같은 텍셀, 가장자리 u = 0 은 반대쪽 끝 텍셀과 섞인다.
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 1.25f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 1.0f, 0.0f, 0.0f, 1.0f }, color );
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ -0.25f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 0.0f, 1.0f, 0.0f, 1.0f }, color );
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.0f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 0.5f, 0.5f, 0.0f, 1.0f }, color );
}

SW_TEST_CASE( MeshModifierTextureSamplerTest, MissingFileAndEmptyPathAreFalse )
{
    MeshModifierTextureSampler sampler; // 캐시는 표본기 인스턴스의 것이라 테스트마다 비어 시작한다
    const float4               untouched{ 0.1f, 0.2f, 0.3f, 0.4f };
    float4                     color = untouched;
    SW_EXPECT_FALSE( sampler.trySample( "", float2{ 0.5f, 0.5f }, color ) );
    const string missing = test::makeTempDirectory( "meshmodifier_texture_missing" ) + "/missing.tga";
    SW_EXPECT_FALSE( sampler.trySample( missing, float2{ 0.5f, 0.5f }, color ) );
    // 두 번째 호출은 캐시된 실패를 쓴다 — 결과는 같다.
    SW_EXPECT_FALSE( sampler.trySample( missing, float2{ 0.5f, 0.5f }, color ) );
    TextureSamplerTestInternal::expectColor( untouched, color );
}

SW_TEST_CASE( MeshModifierTextureSamplerTest, ClearCacheRereadsChangedFile )
{
    MeshModifierTextureSampler sampler; // 캐시는 표본기 인스턴스의 것이라 테스트마다 비어 시작한다
    const string               path = test::makeTempDirectory( "meshmodifier_texture_reload" ) + "/reload.tga";
    SW_EXPECT_TRUE( TextureSamplerTestInternal::writeTga2x2( path, TextureSamplerTestInternal::kListPrimary ) );

    float4 color{};
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.25f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 1.0f, 0.0f, 0.0f, 1.0f }, color );

    constexpr uint8 kListGray[16] = { 128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255 };
    SW_EXPECT_TRUE( TextureSamplerTestInternal::writeTga2x2( path, kListGray ) );
    sampler.clearCache();
    SW_EXPECT_TRUE( sampler.trySample( path, float2{ 0.25f, 0.25f }, color ) );
    TextureSamplerTestInternal::expectColor( float4{ 128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f }, color );
}
