#include "pch.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/BindlessIndexValidator.h"

#include "EngineTest/RHIFakeDevice.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// Engine/Renderer/Frame/BindlessIndexValidator — 바인드 시점의 bindless 인덱스 생존 검사(Debug · Dev 전용, Shipping 에는 없다).
// 생존 판정은 가짜 디바이스(FakeRHIDevice 의 살아 있는 번호 목록)로 대신한다. 실제 백엔드의 판정은 RHIDeviceTest.BindlessIndexLivenessFollowsRegistration.
#if !defined( SW_SHIPPING )
/**
 * @brief [BindlessIndexValidatorTest] 무효 번호 · 살아 있는 번호는 통과, 죽은 번호만 걸린다
 * @details `kInvalidDescriptorIndex` 는 "안 걸렸다" 는 정상 값이라(셰이더가 폴백한다) 묻지도 않는다. 해제한 번호와 다른 공간의 번호가 걸린다.
 */
SW_TEST_CASE( BindlessIndexValidatorTest, OnlyDeadIndicesFail )
{
    sw::BindlessIndexValidator::clearReports();

    test::FakeRHIDevice device;
    device._listLiveTextureIndex.push_back( 3 );
    device._listLiveBufferIndex.push_back( 7 );

    const sw::BindlessIndexCheckSite site{ &device, sw::hashed_string( "ForwardTransparent" ) };
    const sw::hashed_string          shadowField{ "g_ShadowMapIndex" };
    const sw::hashed_string          lightField{ "g_SwLightsIndex" };

    SW_EXPECT_TRUE( sw::BindlessIndexValidator::validate( site, sw::RHIBindlessIndexSpace::Texture, sw::kInvalidDescriptorIndex, shadowField ) );
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::validate( site, sw::RHIBindlessIndexSpace::Texture, 3, shadowField ) );
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::validate( site, sw::RHIBindlessIndexSpace::Buffer, 7, lightField ) );
    SW_EXPECT_EQUAL( 0u, sw::BindlessIndexValidator::getReportCount() );

    // 해제된(목록에 없는) 번호, 그리고 버퍼 번호를 텍스처 자리에 넣은 것.
    SW_EXPECT_FALSE( sw::BindlessIndexValidator::validate( site, sw::RHIBindlessIndexSpace::Texture, 4, shadowField ) );
    SW_EXPECT_FALSE( sw::BindlessIndexValidator::validate( site, sw::RHIBindlessIndexSpace::Texture, 7, lightField ) );
    SW_EXPECT_EQUAL( 2u, sw::BindlessIndexValidator::getReportCount() );

    // 디바이스가 없는 자리는 검사하지 않는다(부르는 쪽이 디바이스를 못 준 경로).
    const sw::BindlessIndexCheckSite noDevice{};
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::validate( noDevice, sw::RHIBindlessIndexSpace::Texture, 4, shadowField ) );

    sw::BindlessIndexValidator::clearReports();
}

/**
 * @brief [BindlessIndexValidatorTest] 같은 자리(패스 · 필드 · 원소)는 한 번만 보고한다
 * @details 드로우마다 같은 죽은 번호가 걸리므로 자리마다 한 줄이어야 로그가 읽힌다. 원소(머티리얼 텍스처 서수)가 다르면 다른 자리다.
 */
SW_TEST_CASE( BindlessIndexValidatorTest, ReportsEachSiteOnce )
{
    sw::BindlessIndexValidator::clearReports();

    const sw::hashed_string passName{ "ForwardTransparent" };
    const sw::hashed_string otherPass{ "ForwardOpaque" };
    const sw::hashed_string field{ "MaterialTexture" };

    SW_EXPECT_TRUE( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 9, passName, field, 0 ) );
    SW_EXPECT_FALSE( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 9, passName, field, 0 ) );
    SW_EXPECT_FALSE_MSG( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 10, passName, field, 0 ),
                         "a different value at the same site must not log again" );
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 9, passName, field, 1 ) );
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 9, otherPass, field, 0 ) );
    SW_EXPECT_EQUAL( 3u, sw::BindlessIndexValidator::getReportCount() );

    sw::BindlessIndexValidator::clearReports();
    SW_EXPECT_EQUAL( 0u, sw::BindlessIndexValidator::getReportCount() );
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 9, passName, field, 0 ) );
    sw::BindlessIndexValidator::clearReports();
}

/**
 * @brief [BindlessIndexValidatorTest] "없음" 번호의 자리 기록은 죽은 번호 보고와 따로 세고, 자리마다 한 번이다
 * @details `-gv_validateBindlessIndex=2` 가 남기는 정보 로그다. 오류(죽은 번호) 수에 섞이면 "검사가 결함을 찾았다" 를 셀 수 없다.
 */
SW_TEST_CASE( BindlessIndexValidatorTest, AbsentNotesAreCountedApart )
{
    sw::BindlessIndexValidator::clearReports();

    const sw::hashed_string material{ "GlassMaterial" };
    const sw::hashed_string field{ "albedoMap" };
    SW_EXPECT_TRUE( sw::BindlessIndexValidator::noteAbsent( material, field, 0 ) );
    SW_EXPECT_FALSE( sw::BindlessIndexValidator::noteAbsent( material, field, 0 ) );
    SW_EXPECT_EQUAL( 1u, sw::BindlessIndexValidator::getAbsentNoteCount() );
    SW_EXPECT_EQUAL( 0u, sw::BindlessIndexValidator::getReportCount() );

    sw::BindlessIndexValidator::clearReports();
    SW_EXPECT_EQUAL( 0u, sw::BindlessIndexValidator::getAbsentNoteCount() );
}

/**
 * @brief [BindlessIndexValidatorTest] 병렬 기록 스레드가 같은 자리를 동시에 보고해도 로그는 한 줄이다
 * @details 패스는 여러 워커에서 동시에 기록된다. 보고 기록이 경합하면 같은 줄이 여러 번 나오거나 기록이 깨진다.
 */
SW_TEST_CASE( BindlessIndexValidatorTest, ConcurrentReportsLogOnce )
{
    sw::BindlessIndexValidator::clearReports();

    constexpr uint32        kThreadCount = 8;
    const sw::hashed_string passName{ "ShadowAtlas" };
    const sw::hashed_string field{ "g_ShadowMapIndex" };
    sw::atomic<uint32>      firstCount{ 0 };
    sw::vector<std::thread> listThread;
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&firstCount, &passName, &field]()
        {
            for ( uint32 repeat = 0; repeat < 64; ++repeat )
            {
                if ( sw::BindlessIndexValidator::report( sw::RHIBindlessIndexSpace::Texture, 42, passName, field, 0 ) )
                    firstCount.fetch_add( 1 );
            }
        } );
    }
    for ( std::thread& thread : listThread )
    {
        thread.join();
    }

    SW_EXPECT_EQUAL( 1u, firstCount.load() );
    SW_EXPECT_EQUAL( 1u, sw::BindlessIndexValidator::getReportCount() );
    sw::BindlessIndexValidator::clearReports();
}
#endif
