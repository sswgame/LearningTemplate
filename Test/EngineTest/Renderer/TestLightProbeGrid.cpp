#include "pch.h"

#include "Core/Math/SphericalHarmonics.h"

#include "Engine/Graphics/Lighting/LightProbeGrid.h"

#include "TestFramework/TestFramework.h"

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
    SW_ASSERT_TRUE( source.isValid() );
    sw::vector<uint8> bytes;
    source.makeBytes( bytes );

    sw::LightProbeGrid loaded{};
    SW_ASSERT_TRUE( loaded.readFromBytes( bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( loaded._layout.matches( source._layout, 0.0f ) );
    SW_ASSERT_EQUAL( loaded._listProbe.size(), source._listProbe.size() );
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
    sw::string         error;
    SW_EXPECT_FALSE( loaded.readFromBytes( bytes.data(), bytes.size() - 4, &error ) );
    SW_EXPECT_TRUE( loaded._listProbe.empty() );
    SW_EXPECT_FALSE( error.empty() );

    sw::vector<uint8> wrongMagic = bytes;
    wrongMagic[0]                = 'X';
    SW_EXPECT_FALSE( loaded.readFromBytes( wrongMagic.data(), wrongMagic.size() ) );

    sw::vector<uint8> wrongVersion = bytes;
    wrongVersion[4]                = static_cast<uint8>( sw::LightProbeGrid::kVersion + 1 );
    SW_EXPECT_FALSE( loaded.readFromBytes( wrongVersion.data(), wrongVersion.size() ) );
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
    SW_EXPECT_NEAR_EQUAL( 0.0f, position._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 3.5f, position._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, position._z, 1e-5f );
}
