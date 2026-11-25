/**
 * @file TestRHIUploadLog.cpp
 * @brief 버퍼 갱신 · 텍스처 전송 판정(`RHIUploadLog`)과 그것을 먹이는 공통 입구 테스트입니다(nogpu).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIUploadLog.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

using namespace sw;

namespace
{
    /** @brief @p log 의 프레임을 다른 스레드에서 열어, 이 스레드를 "프레임을 기록하지 않는 스레드" 로 만듭니다. */
    template <typename THandle>
    void beginFrameOnOtherThread( RHIUploadLog<THandle>& log )
    {
        std::thread frameThread{ [&log]()
        { log.beginFrame(); } };
        frameThread.join();
    }
} // namespace

/**
 * @brief [RHIUploadLogTest] 앞 프레임까지 내용이 올라간 버퍼는 기록 중인 프레임이 쓴다고 답하고, 이번 프레임에 만든 버퍼의 첫 내용(나눠 올리는 것도)은 지나간다
 * @details 변이: 프레임 번호 비교를 `<=` 로 바꾸면 새 버퍼의 둘째 조각이, 비교를 빼면 첫 단언이 진다.
 */
SW_TEST_CASE( RHIUploadLogTest, OldContentIsUsedAndNewContentIsNot )
{
    RHIUploadLog<RHIBufferHandle> log;
    constexpr RHIBufferHandle     kOld{ 0x100000001ull };
    constexpr RHIBufferHandle     kNew{ 0x100000002ull };
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
 * @brief [RHIUploadLogTest] 이번 프레임이 전이 · 복사한 버퍼는 CPU 내용이 없어도(GPU 가 쓴 버퍼) 쓴다고 답한다. 프레임을 기록하는 스레드 자신의 갱신은 잘못이 아니다
 */
SW_TEST_CASE( RHIUploadLogTest, RecordedUseCountsOnlyForOtherThreads )
{
    RHIUploadLog<RHIBufferHandle> log;
    constexpr RHIBufferHandle     kBuffer{ 0x100000003ull };
    beginFrameOnOtherThread( log );
    log.recordUse( kBuffer );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( kBuffer ) );

    log.beginFrame(); // 이제 이 스레드가 기록한다
    log.recordUse( kBuffer );
    log.recordContent( kBuffer );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kBuffer ) );
}

/**
 * @brief [RHIUploadLogTest] 공통 입구가 사실을 적는다 — 팩토리의 첫 내용 · 갱신, 컨텍스트의 전이 · 복사, 장치의 해제
 * @details 변이: 입구 하나라도 적지 않으면 그 버퍼의 단언이 진다(백엔드는 이 사실을 따로 적지 않는다).
 */
SW_TEST_CASE( RHIUploadLogTest, CommonEntryPointsFeedTheLog )
{
    test::FakeRHIBufferFactory  factory;
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    device._pResourceFactory = &factory;
    const RHICapabilities capabilities{};
    factory.bindDevice( device, ShaderTargetFormat{}, capabilities );
    RHIUploadLog<RHIBufferHandle>& log = device.getBufferUploadLog();

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

/**
 * @brief [RHIUploadLogTest] 텍스처도 공통 입구가 사실을 적는다 — 전송의 첫 내용, 컨텍스트의 전이 · 렌더 타깃, 장치의 해제. 기록한 쓰임은 기록하는 스레드 자신의 전송에도 잘못이다
 * @details 예전에는 미리보기 렌더러가 주 화면을 기록한 뒤 텍스처 구간을 올려, 주 화면이 전이를 기록한 텍스처의 전송이 기록 전 상태에서 실행됐다. 변이: 입구 하나라도 적지 않으면 그 텍스처의 단언이 진다.
 */
SW_TEST_CASE( RHIUploadLogTest, TextureEntryPointsFeedTheLog )
{
    test::FakeRHIBufferFactory  factory;
    test::FakeRHIDevice         device;
    test::FakeRHICommandContext context{ device };
    device._pResourceFactory = &factory;
    const RHICapabilities capabilities{};
    factory.bindDevice( device, ShaderTargetFormat{}, capabilities );
    RHIUploadLog<RHITextureHandle>& log = device.getTextureUploadLog();

    constexpr RHITextureHandle kUploaded{ 0x100000001ull };
    constexpr RHITextureHandle kRead{ 0x100000002ull };
    constexpr RHITextureHandle kTarget{ 0x100000003ull };
    constexpr RHITextureHandle kDepth{ 0x100000004ull };
    constexpr RHITextureHandle kUntouched{ 0x100000005ull };
    (void)factory.submitTextureUpload( kUploaded, RHITextureUploadDesc{} );

    log.beginFrame(); // 이 스레드가 기록한다
    SW_EXPECT_TRUE( log.hasEarlierContent( kUploaded ) );
    SW_EXPECT_FALSE( log.hasRecordedUse( kUploaded ) );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kUploaded ) );
    context.prepareTextureForShaderRead( kRead );
    context.beginRenderPass( RHIRenderPassBeginInfo::color( kTarget, RHIRenderPassLoadOp::Clear, float4{} ).setDepth( kDepth, RHIRenderPassLoadOp::Clear ) );
    context.endRenderPass();
    SW_EXPECT_TRUE( log.hasRecordedUse( kRead ) );
    SW_EXPECT_TRUE( log.hasRecordedUse( kTarget ) );
    SW_EXPECT_TRUE( log.hasRecordedUse( kDepth ) );
    SW_EXPECT_FALSE( log.hasRecordedUse( kUntouched ) );
    log.endFrame();
    SW_EXPECT_FALSE( log.hasRecordedUse( kRead ) ); // 프레임 밖이다 — 되읽기 · 전송을 낼 수 있다

    beginFrameOnOtherThread( log );
    SW_EXPECT_TRUE( log.isUsedByFrameOnOtherThread( kUploaded ) );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kUntouched ) );
    device.releaseHandle( kUploaded );
    SW_EXPECT_FALSE( log.isUsedByFrameOnOtherThread( kUploaded ) );
}
