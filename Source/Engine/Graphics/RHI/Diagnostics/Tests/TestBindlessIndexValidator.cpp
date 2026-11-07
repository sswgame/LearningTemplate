/**
 * @file TestBindlessIndexValidator.cpp
 * @brief 바인드 시점의 bindless 인덱스 생존 검사(`BindlessIndexValidator`, Debug · Dev 전용) 테스트입니다.
 * @details 생존 판정은 가짜 디바이스(살아 있는 번호 목록)로 대신한다. 보고 기록은 전역이므로 픽스처가 앞뒤로 비운다.
 */
#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

#include <thread>

using namespace sw;

namespace
{
    /** @brief 전역 보고 기록을 케이스 앞뒤로 비웁니다 — 순서가 섞여도 다른 케이스의 기록을 보지 않습니다. */
    struct BindlessIndexValidatorTest
    {
        BindlessIndexValidatorTest() { BindlessIndexValidator::clearReports(); }
        ~BindlessIndexValidatorTest() { BindlessIndexValidator::clearReports(); }
    };
} // namespace

/**
 * @brief [BindlessIndexValidatorTest] 무효 번호 · 살아 있는 번호는 통과, 죽은 번호만 걸린다
 * @details `kInvalidDescriptorIndex` 는 "안 걸렸다" 는 정상 값이라(셰이더가 폴백한다) 묻지도 않는다. 해제한 번호와 다른 공간의 번호가 걸린다.
 */
SW_TEST_CASE_F( BindlessIndexValidatorTest, OnlyDeadIndicesFail )
{
    SW_EXPECT_LOG( "RHI.BindlessIndexDead" );
    test::FakeRHIDevice device;
    device._listLiveTextureIndex.push_back( 3 );
    device._listLiveBufferIndex.push_back( 7 );

    const BindlessIndexCheckSite site{ &device, hashed_string( "ForwardTransparent" ) };
    const hashed_string          shadowField{ "g_ShadowMapIndex" };
    const hashed_string          lightField{ "g_SwLightsIndex" };

    SW_EXPECT_TRUE( BindlessIndexValidator::validate( site, RHIBindlessIndexSpace::Texture, kInvalidDescriptorIndex, shadowField ) );
    SW_EXPECT_TRUE( BindlessIndexValidator::validate( site, RHIBindlessIndexSpace::Texture, 3, shadowField ) );
    SW_EXPECT_TRUE( BindlessIndexValidator::validate( site, RHIBindlessIndexSpace::Buffer, 7, lightField ) );
    SW_EXPECT_EQUAL( 0u, BindlessIndexValidator::getReportCount() );

    // 해제된(목록에 없는) 번호, 그리고 버퍼 번호를 텍스처 자리에 넣은 것.
    SW_EXPECT_FALSE( BindlessIndexValidator::validate( site, RHIBindlessIndexSpace::Texture, 4, shadowField ) );
    SW_EXPECT_FALSE( BindlessIndexValidator::validate( site, RHIBindlessIndexSpace::Texture, 7, lightField ) );
    SW_EXPECT_EQUAL( 2u, BindlessIndexValidator::getReportCount() );

    // 디바이스가 없는 자리는 검사하지 않는다(부르는 쪽이 디바이스를 못 준 경로).
    const BindlessIndexCheckSite noDevice{};
    SW_EXPECT_TRUE( BindlessIndexValidator::validate( noDevice, RHIBindlessIndexSpace::Texture, 4, shadowField ) );
}

/**
 * @brief [BindlessIndexValidatorTest] 같은 자리(패스 · 필드 · 원소)는 한 번만 보고한다
 * @details 드로우마다 같은 죽은 번호가 걸리므로 자리마다 한 줄이어야 로그가 읽힌다. 원소(머티리얼 텍스처 서수)가 다르면 다른 자리다.
 */
SW_TEST_CASE_F( BindlessIndexValidatorTest, ReportsEachSiteOnce )
{
    SW_EXPECT_LOG( "RHI.BindlessIndexDead" );
    const hashed_string passName{ "ForwardTransparent" };
    const hashed_string otherPass{ "ForwardOpaque" };
    const hashed_string field{ "MaterialTexture" };

    SW_EXPECT_TRUE( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 9, passName, field, 0 ) );
    SW_EXPECT_FALSE( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 9, passName, field, 0 ) );
    SW_EXPECT_FALSE_MSG( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 10, passName, field, 0 ),
                         "a different value at the same site must not log again" );
    SW_EXPECT_TRUE( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 9, passName, field, 1 ) );
    SW_EXPECT_TRUE( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 9, otherPass, field, 0 ) );
    SW_EXPECT_EQUAL( 3u, BindlessIndexValidator::getReportCount() );

    BindlessIndexValidator::clearReports();
    SW_EXPECT_EQUAL( 0u, BindlessIndexValidator::getReportCount() );
    SW_EXPECT_TRUE( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 9, passName, field, 0 ) );
}

/**
 * @brief [BindlessIndexValidatorTest] "없음" 번호의 자리 기록은 죽은 번호 보고와 따로 세고, 자리마다 한 번이다
 * @details `-gv_validateBindlessIndex=2` 가 남기는 정보 로그다. 오류(죽은 번호) 수에 섞이면 "검사가 결함을 찾았다" 를 셀 수 없다.
 */
SW_TEST_CASE_F( BindlessIndexValidatorTest, AbsentNotesAreCountedApart )
{
    const hashed_string material{ "GlassMaterial" };
    const hashed_string field{ "_albedoMap" };
    SW_EXPECT_TRUE( BindlessIndexValidator::recordAbsent( material, field, 0 ) );
    SW_EXPECT_FALSE( BindlessIndexValidator::recordAbsent( material, field, 0 ) );
    SW_EXPECT_EQUAL( 1u, BindlessIndexValidator::getAbsentNoteCount() );
    SW_EXPECT_EQUAL( 0u, BindlessIndexValidator::getReportCount() );

    BindlessIndexValidator::clearReports();
    SW_EXPECT_EQUAL( 0u, BindlessIndexValidator::getAbsentNoteCount() );
}

/**
 * @brief [BindlessIndexValidatorTest] 병렬 기록 스레드가 같은 자리를 동시에 보고해도 로그는 한 줄이다
 * @details 패스는 여러 워커에서 동시에 기록된다. 보고 기록이 경합하면 같은 줄이 여러 번 나오거나 기록이 깨진다.
 */
SW_TEST_CASE_F( BindlessIndexValidatorTest, ConcurrentReportsLogOnce )
{
    SW_EXPECT_LOG( "RHI.BindlessIndexDead" );
    constexpr uint32    kThreadCount = 8;
    const hashed_string passName{ "ShadowAtlas" };
    const hashed_string field{ "g_ShadowMapIndex" };
    atomic<uint32>      firstCount{ 0 };
    vector<std::thread> listThread;
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&firstCount, &passName, &field]()
        {
            for ( uint32 repeat = 0; repeat < 64; ++repeat )
            {
                if ( BindlessIndexValidator::report( RHIBindlessIndexSpace::Texture, 42, passName, field, 0 ) )
                    firstCount.fetch_add( 1 );
            }
        } );
    }
    for ( std::thread& thread : listThread )
    {
        thread.join();
    }

    SW_EXPECT_EQUAL( 1u, firstCount.load() );
    SW_EXPECT_EQUAL( 1u, BindlessIndexValidator::getReportCount() );
}
