/**
 * @file TestRHIBufferUploadLog.cpp
 * @brief 다른 스레드의 버퍼 갱신 판정(`RHIBufferUploadLog`)과 그것을 먹이는 공통 입구 테스트입니다(nogpu).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIBufferUploadLog.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

using namespace sw;

namespace
{
    /** @brief @p log 의 프레임을 다른 스레드에서 열어, 이 스레드를 "프레임을 기록하지 않는 스레드" 로 만듭니다. */
    void beginFrameOnOtherThread( RHIBufferUploadLog& log )
    {
        std::thread frameThread{ [&log]()
        { log.beginFrame(); } };
        frameThread.join();
    }
} // namespace

/**
 * @brief [RHIBufferUploadLogTest] 앞 프레임까지 내용이 올라간 버퍼는 기록 중인 프레임이 쓴다고 답하고, 이번 프레임에 만든 버퍼의 첫 내용(나눠 올리는 것도)은 지나간다
 * @details 변이: 프레임 번호 비교를 `<=` 로 바꾸면 새 버퍼의 둘째 조각이, 비교를 빼면 첫 단언이 진다.
 */
SW_TEST_CASE( RHIBufferUploadLogTest, OldContentIsUsedAndNewContentIsNot )
{
    RHIBufferUploadLog        log;
    constexpr RHIBufferHandle kOld{ 0x100000001ull };
    constexpr RHIBufferHandle kNew{ 0x100000002ull };
    log.recordContent( kOld );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kOld ) ); // 프레임 밖이다

    beginFrameOnOtherThread( log );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( kOld ) );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kNew ) );
    log.recordContent( kNew );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kNew ) ); // 첫 내용의 둘째 조각

    log.endFrame();
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kOld ) );
    beginFrameOnOtherThread( log );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( kNew ) ); // 다음 프레임부터는 읽을 수 있는 버퍼다
    log.remove( kNew );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kNew ) );
}

/**
 * @brief [RHIBufferUploadLogTest] 이번 프레임이 전이 · 복사한 버퍼는 CPU 내용이 없어도(GPU 가 쓴 버퍼) 쓴다고 답한다. 프레임을 기록하는 스레드 자신의 갱신은 잘못이 아니다
 */
SW_TEST_CASE( RHIBufferUploadLogTest, RecordedUseCountsOnlyForOtherThreads )
{
    RHIBufferUploadLog        log;
    constexpr RHIBufferHandle kBuffer{ 0x100000003ull };
    beginFrameOnOtherThread( log );
    log.recordUse( kBuffer );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( kBuffer ) );

    log.beginFrame(); // 이제 이 스레드가 기록한다
    log.recordUse( kBuffer );
    log.recordContent( kBuffer );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kBuffer ) );
}

/**
 * @brief [RHIBufferUploadLogTest] 공통 입구가 사실을 적는다 — 팩토리의 첫 내용 · 갱신, 컨텍스트의 전이 · 복사, 장치의 해제
 * @details 변이: 입구 하나라도 적지 않으면 그 버퍼의 단언이 진다(백엔드는 이 사실을 따로 적지 않는다).
 */
SW_TEST_CASE( RHIBufferUploadLogTest, CommonEntryPointsFeedTheLog )
{
    test::FakeRHIBufferFactory  factory;
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    device._pResourceFactory = &factory;
    const RHICapabilities capabilities{};
    factory.bindDevice( device, ShaderTargetFormat{}, capabilities );
    RHIBufferUploadLog& log = device.getBufferUploadLog();

    const RHIBufferHandle     updated    = factory.makeBuffer();
    const RHIBufferHandle     geometry   = factory.makeBuffer();
    const RHIBufferHandle     empty      = factory.makeBuffer();
    const RHIBufferHandle     transition = factory.makeBuffer();
    const RHIBufferHandle     copied     = factory.makeBuffer();
    const uint32              value      = 1;
    const RHIBufferCopyRegion region{ 0, 0, sizeof( value ) };
    factory.updateStructuredBuffer( updated, &value, sizeof( value ) );
    factory.updateGeometryBufferRegions( geometry, &value, &region, 1 );

    beginFrameOnOtherThread( log );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( updated ) );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( geometry ) );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( empty ) );
    context.transitionBuffer( transition, RHIBufferState::ShaderResource );
    context.copyBufferRegion( copied, 0, empty, 0, sizeof( value ) );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( transition ) );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( copied ) );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( empty ) );

    device.releaseHandle( updated );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( updated ) );
}
