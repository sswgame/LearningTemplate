#include "pch.h"

#include "Core/Math/SphericalHarmonics.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"

namespace sw
{
    namespace
    {
        /** @brief 2 × 3 × 2 격자에 프로브마다 다른 계수를 채운 데이터입니다. */
        LightProbeGrid makeLightProbeGridForTest()
        {
            LightProbeGrid grid{};
            grid._layout._origin  = float3{ -2.0f, 0.5f, 3.0f };
            grid._layout._spacing = float3{ 2.0f, 1.5f, 4.0f };
            grid._layout._countX  = 2;
            grid._layout._countY  = 3;
            grid._layout._countZ  = 2;
            grid._listProbe.resize( grid._layout.getProbeCount() );
            for ( uint32 probeIndex = 0; probeIndex < static_cast<uint32>( grid._listProbe.size() ); ++probeIndex )
            {
                for ( uint32 index = 0; index < SH9Color::kCoefficientCount; ++index )
                {
                    grid._listProbe[probeIndex]._arrCoefficient[index] = static_cast<float32>( probeIndex * 100 + index ) * 0.01f;
                }
            }
            return grid;
        }
    } // namespace
} // namespace sw

/**
 * @brief [LightProbeGridTest] `.probes` 바이트는 왕복해도 모양과 계수가 같다
 */
SW_TEST_CASE( LightProbeGridTest, BytesRoundTrip )
{
    const sw::LightProbeGrid source = sw::makeLightProbeGridForTest();
    SW_REQUIRE_TRUE( source.validate() );
    sw::vector<uint8> bytes;
    source.makeBytes( bytes );

    sw::LightProbeGrid loaded{};
    SW_REQUIRE_TRUE( loaded.readFromBytes( bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( loaded._layout.matches( source._layout, 0.0f ) );
    SW_REQUIRE_EQUAL( loaded._listProbe.size(), source._listProbe.size() );
    for ( size_t probeIndex = 0; probeIndex < source._listProbe.size(); ++probeIndex )
    {
        for ( uint32 index = 0; index < sw::SH9Color::kCoefficientCount; ++index )
        {
            SW_EXPECT_EQUAL( loaded._listProbe[probeIndex]._arrCoefficient[index], source._listProbe[probeIndex]._arrCoefficient[index] );
        }
    }
}

/**
 * @brief [LightProbeGridTest] 잘린 파일 · 다른 매직 · 다른 버전은 읽지 않고 내용을 비운다
 */
SW_TEST_CASE( LightProbeGridTest, RejectsBrokenFiles )
{
    const sw::LightProbeGrid source = sw::makeLightProbeGridForTest();
    sw::vector<uint8>        bytes;
    source.makeBytes( bytes );

    sw::LightProbeGrid loaded{};
    SW_EXPECT_FALSE( loaded.readFromBytes( bytes.data(), bytes.size() - 4 ) );
    SW_EXPECT_TRUE( loaded._listProbe.empty() );

    sw::vector<uint8> wrongMagic = bytes;
    wrongMagic[0]                = 'X';
    SW_EXPECT_FALSE( loaded.readFromBytes( wrongMagic.data(), wrongMagic.size() ) );

    sw::vector<uint8> wrongVersion = bytes;
    wrongVersion[4]                = static_cast<uint8>( sw::LightProbeGrid::kVersion + 1 );
    SW_EXPECT_FALSE( loaded.readFromBytes( wrongVersion.data(), wrongVersion.size() ) );
}

/**
 * @brief [LightProbeGridTest] 어느 길이로 잘려도 거절하고, 곱이 64 비트로 넘치는 격자 · NaN 도 거절한다(`.probes` 는 신뢰 경계다)
 */
SW_TEST_CASE( LightProbeGridTest, TruncationForgedCountsAndNonFiniteAreRefused )
{
    const sw::LightProbeGrid source = sw::makeLightProbeGridForTest();
    sw::vector<uint8>        bytes;
    source.makeBytes( bytes );

    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        sw::LightProbeGrid loaded{};
        SW_EXPECT_FALSE( loaded.readFromBytes( bytes.data(), length ) );
        SW_EXPECT_TRUE( loaded._listProbe.empty() );
    }

    // 바이트 자리: 매직(0) · 버전(4) · 원점(8) · 간격(20) · 축마다 수(32 · 36 · 40) · 계수(44).
    const auto refused = []( sw::vector<uint8> forged )
    {
        sw::LightProbeGrid loaded{};
        SW_EXPECT_FALSE( loaded.readFromBytes( forged.data(), forged.size() ) );
        SW_EXPECT_TRUE( loaded._listProbe.empty() );
    };
    const auto patched = [&]( size_t offset, uint32 value )
    {
        sw::vector<uint8> forged = bytes;
        sw::Memory::copy( forged.data() + offset, &value, sizeof( value ) );
        return forged;
    };
    // 2^31 × 2^31 × 4 = 2^64 → 그대로 곱하면 0 이라 머리만 있는 파일이 통과한다.
    sw::vector<uint8> wrapped( bytes.begin(), bytes.begin() + 44 );
    const uint32      arrWrapCount[3] = { 0x80000000u, 0x80000000u, 4u };
    sw::Memory::copy( wrapped.data() + 32, arrWrapCount, sizeof( arrWrapCount ) );
    refused( wrapped );
    refused( patched( 32, 0xFFFFFFFFu ) );
    refused( patched( 8, 0x7FC00000u ) );  // NaN 원점
    refused( patched( 20, 0x7F800000u ) ); // 무한대 간격
    refused( patched( 44, 0x7FC00000u ) ); // NaN 계수

    sw::vector<uint8> longer = bytes;
    longer.push_back( 0 );
    refused( longer );
}

/**
 * @brief [LightProbeGridTest] 프로브 번호와 위치는 (z × Y + y) × X + x 순서다 — 베이크와 셰이더가 같은 번호로 찾는다
 */
SW_TEST_CASE( LightProbeGridTest, ProbeIndexAndPositionAgree )
{
    const sw::LightProbeGrid grid  = sw::makeLightProbeGridForTest();
    const uint32             index = grid._layout.computeProbeIndex( 1, 2, 1 );
    SW_EXPECT_EQUAL( index, 11u );
    const sw::float3 position = grid._layout.computeProbePosition( index );
    SW_EXPECT_NEAR( 0.0f, position._x, 1e-5f );
    SW_EXPECT_NEAR( 3.5f, position._y, 1e-5f );
    SW_EXPECT_NEAR( 7.0f, position._z, 1e-5f );
}
