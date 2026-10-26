#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/Capture/RenderTargetImageFile.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

#include "TestFramework/TestFramework.h"

// FrameRenderer 에서 뽑은 조각(GPU 패스 시간 보고 · 렌더 타깃 이미지 변환 · Present 캡처)을 GPU 없이 본다.

namespace
{
    /** @brief 모든 칸이 "안 적힘" 인 타임스탬프 프레임입니다. */
    sw::RHITimestampFrame makeEmptyTimestampFrameInternal()
    {
        sw::RHITimestampFrame frame;
        frame._listMicro.assign( sw::constant::kMaxGPUTimestampSlot, -1.0f );
        return frame;
    }

    /** @brief 이름만 채운 패스 서술 목록입니다. 보고는 패스 이름만 읽습니다. */
    sw::vector<sw::RenderGraphPassDesc> makePassListInternal()
    {
        sw::vector<sw::RenderGraphPassDesc> listPass( 3 );
        listPass[0]._name = "A";
        listPass[1]._name = "B";
        listPass[2]._name = "C";
        return listPass;
    }
} // namespace

/**
 * @brief [GPUPassTimelineTest] 주입한 프로파일러에 패스마다 `GPU.<패스>` 를, 패스 밖으로 `GPU.Compute` · `GPU.Frame` 을 넣고 안 적힌 패스는 뺀다
 * @details `GPU.Frame` 은 프레임 첫 칸부터 **가장 늦게 끝난** 패스까지다(패스 번호 순이 시간 순이 아니다).
 */
SW_TEST_CASE( GPUPassTimelineTest, ReportsPassComputeAndFrameSpansToInjectedProfiler )
{
    // 프로파일러는 구간 표가 커서(수백 KB) 힙에 둔다.
    const sw::unique_ptr<sw::FrameProfiler> pProfiler = sw::make_unique<sw::FrameProfiler>();
    pProfiler->setEnabled( true );

    sw::GPUPassTimeline timeline;
    SW_EXPECT_FALSE( timeline.isRecording() );
    timeline.bindProfiler( pProfiler.get() );
    SW_EXPECT_TRUE( timeline.isRecording() );

    sw::RHITimestampFrame frame                                            = makeEmptyTimestampFrameInternal();
    frame._listMicro[sw::FrameRendererUtil::kGPUTimestampSlotFrameBegin]   = 0.0f;
    frame._listMicro[sw::FrameRendererUtil::kGPUTimestampSlotComputeBegin] = 1.0f;
    frame._listMicro[sw::FrameRendererUtil::kGPUTimestampSlotComputeEnd]   = 5.0f;
    frame._listMicro[0]                                                    = 10.0f; // 패스 A
    frame._listMicro[1]                                                    = 20.0f;
    frame._listMicro[2]                                                    = 6.0f; // 패스 B
    frame._listMicro[3]                                                    = 9.0f;
    frame._listMicro[4]                                                    = 30.0f; // 패스 C — 끝 칸이 안 적혔다

    pProfiler->beginFrame();
    timeline.reportFrame( frame, makePassListInternal() );
    pProfiler->endFrame();

    sw::FrameProfiler& profiler = *pProfiler;
    SW_EXPECT_EQUAL( profiler.getLastFrameNanos( profiler.registerScope( "GPU.A" ) ), static_cast<uint64>( 10'000 ) );
    SW_EXPECT_EQUAL( profiler.getLastFrameNanos( profiler.registerScope( "GPU.B" ) ), static_cast<uint64>( 3'000 ) );
    SW_EXPECT_EQUAL( profiler.getLastFrameCount( profiler.registerScope( "GPU.C" ) ), static_cast<uint64>( 0 ) );
    SW_EXPECT_EQUAL( profiler.getLastFrameNanos( profiler.registerScope( "GPU.Compute" ) ), static_cast<uint64>( 4'000 ) );
    SW_EXPECT_EQUAL( profiler.getLastFrameNanos( profiler.registerScope( "GPU.Frame" ) ), static_cast<uint64>( 20'000 ) );
}

/**
 * @brief [GPUPassTimelineTest] 프로파일러가 없으면 쓰지도 보고하지도 않는다
 * @details 서비스 없이 렌더러만 쓰는 실행이 이 경로다. 프로파일러를 바꾸면 슬롯을 새 프로파일러에 다시 등록한다.
 */
SW_TEST_CASE( GPUPassTimelineTest, WithoutProfilerDoesNothingAndRebindRegistersAgain )
{
    sw::GPUPassTimeline   timeline;
    sw::RHITimestampFrame frame = makeEmptyTimestampFrameInternal();
    frame._listMicro[0]         = 1.0f;
    frame._listMicro[1]         = 2.0f;
    timeline.reportFrame( frame, makePassListInternal() ); // 프로파일러가 없어도 아무 일 없이 돌아온다.
    SW_EXPECT_FALSE( timeline.isRecording() );

    const sw::unique_ptr<sw::FrameProfiler> pFirst  = sw::make_unique<sw::FrameProfiler>();
    const sw::unique_ptr<sw::FrameProfiler> pSecond = sw::make_unique<sw::FrameProfiler>();
    pFirst->setEnabled( true );
    pSecond->setEnabled( true );
    // 두 번째 프로파일러에 다른 이름을 먼저 넣어 슬롯 번호가 첫 번째와 어긋나게 한다.
    (void)pSecond->registerScope( "Other" );

    timeline.bindProfiler( pFirst.get() );
    timeline.reportFrame( frame, makePassListInternal() );
    timeline.bindProfiler( pSecond.get() );
    pSecond->beginFrame();
    timeline.reportFrame( frame, makePassListInternal() );
    pSecond->endFrame();

    SW_EXPECT_EQUAL( pSecond->getLastFrameNanos( pSecond->registerScope( "GPU.A" ) ), static_cast<uint64>( 1'000 ) );
    SW_EXPECT_EQUAL( pSecond->getLastFrameCount( pSecond->registerScope( "Other" ) ), static_cast<uint64>( 0 ) );
}

/**
 * @brief [RenderTargetImageFileTest] BGRA 는 채널을 맞바꾸고, half 첨부는 바이트가 아니라 값으로 풀어 [0,1] 로 자른다
 * @details half 를 바이트로 읽으면 가수 하위 바이트가 색이 된다. 행 간격은 `_rowBytes` 를 따른다.
 */
SW_TEST_CASE( RenderTargetImageFileTest, UnpacksBgraAndHalfToRgba8 )
{
    sw::RHITextureMipSpan layout{};
    layout._width    = 1;
    layout._height   = 2;
    layout._rowBytes = 8; // 행 간격을 픽셀보다 넓게 둬 다음 행을 행 간격으로 찾는지 본다

    const sw::vector<uint8> bgraBytes{ 10, 20, 30, 40, 0xEE, 0xEE, 0xEE, 0xEE, 50, 60, 70, 80, 0xEE, 0xEE, 0xEE, 0xEE };
    sw::vector<uint8>       rgbaBytes;
    SW_ASSERT_TRUE( sw::RenderTargetImageFile::unpackRgba8( bgraBytes, layout, sw::RHIFormat::B8G8R8A8_UNORM, rgbaBytes ) );
    const sw::vector<uint8> expectedBgra{ 30, 20, 10, 255, 70, 60, 50, 255 };
    SW_EXPECT_TRUE( rgbaBytes == expectedBgra );

    // half 1.0 = 0x3C00, 0.0 = 0x0000, 2.0 = 0x4000(1 로 잘린다). 리틀 엔디언.
    sw::RHITextureMipSpan halfLayout{};
    halfLayout._width    = 1;
    halfLayout._height   = 1;
    halfLayout._rowBytes = 8;
    const sw::vector<uint8> halfBytes{ 0x00, 0x3C, 0x00, 0x00, 0x00, 0x40, 0x00, 0x3C };
    SW_ASSERT_TRUE( sw::RenderTargetImageFile::unpackRgba8( halfBytes, halfLayout, sw::RHIFormat::R16G16B16A16_FLOAT, rgbaBytes ) );
    const sw::vector<uint8> expectedHalf{ 255, 0, 255, 255 };
    SW_EXPECT_TRUE( rgbaBytes == expectedHalf );
}

/**
 * @brief [RenderTargetImageFileTest] `halfToUnorm8` 은 half 통합 전의 손 해독과 65536 개 비트 패턴 모두에서 같은 값을 낸다
 * @details 옛 함수를 사본으로 두고 비교한다. 음수 · -0 · 음의 NaN 은 0, 양의 Inf · NaN 은 255, 그 사이는 value*255+0.5 의 버림이다.
 */
SW_TEST_CASE( RenderTargetImageFileTest, HalfToUnorm8MatchesLegacyDecode )
{
    const auto legacy = []( uint16 bits ) -> uint8
    {
        const uint32 sign     = static_cast<uint32>( bits >> 15 );
        const int32  exponent = static_cast<int32>( ( bits >> 10 ) & 0x1Fu );
        const uint32 mantissa = static_cast<uint32>( bits & 0x3FFu );
        if ( sign != 0 )
            return 0;
        if ( exponent == 0x1F )
            return 255;
        float32 value = 0.0f;
        if ( exponent == 0 )
            value = static_cast<float32>( mantissa ) * ( 1.0f / 16777216.0f );
        else
            value = ( 1.0f + static_cast<float32>( mantissa ) / 1024.0f ) * sw::MathUtil::pow( 2.0f, static_cast<float32>( exponent - 15 ) );
        if ( value <= 0.0f )
            return 0;
        if ( value >= 1.0f )
            return 255;
        return static_cast<uint8>( value * 255.0f + 0.5f );
    };
    uint32 mismatchCount = 0;
    for ( uint32 bits = 0; bits <= 0xFFFFu; ++bits )
    {
        if ( sw::FrameRendererUtil::halfToUnorm8( static_cast<uint16>( bits ) ) != legacy( static_cast<uint16>( bits ) ) )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( mismatchCount, static_cast<uint32>( 0 ) );
    SW_EXPECT_EQUAL( sw::FrameRendererUtil::halfToUnorm8( 0x7E00 ), static_cast<uint8>( 255 ) ); // 양의 NaN
    SW_EXPECT_EQUAL( sw::FrameRendererUtil::halfToUnorm8( 0xFE00 ), static_cast<uint8>( 0 ) );   // 음의 NaN
}

/**
 * @brief [RenderTargetImageFileTest] 깊이 읽기 배치(R32_FLOAT 0..1)와 R8 은 회색으로 풀린다 — `-gv_screenshotAttachment=ShadowMap` 의 길
 * @details 깊이 첨부를 선언 포맷(D24S8)으로 풀면 float 바이트가 색이 된다. 읽기는 `kRHIDepthReadbackFormat` 배치로 오고 [0,1] 밖은 자른다.
 */
SW_TEST_CASE( RenderTargetImageFileTest, UnpacksDepthAndSingleChannelAsGray )
{
    sw::RHITextureMipSpan layout{};
    layout._width                 = 3;
    layout._height                = 1;
    layout._rowBytes              = 12;
    const float32     arrDepth[3] = { 0.0f, 0.5f, 2.0f };
    sw::vector<uint8> depthBytes( sizeof( arrDepth ) );
    sw::Memory::copy( depthBytes.data(), arrDepth, sizeof( arrDepth ) );
    sw::vector<uint8> rgbaBytes;
    SW_ASSERT_TRUE( sw::RenderTargetImageFile::unpackRgba8( depthBytes, layout, sw::kRHIDepthReadbackFormat, rgbaBytes ) );
    const sw::vector<uint8> expectedDepth{ 0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255 };
    SW_EXPECT_TRUE( rgbaBytes == expectedDepth );

    sw::RHITextureMipSpan byteLayout{};
    byteLayout._width    = 2;
    byteLayout._height   = 1;
    byteLayout._rowBytes = 4;
    const sw::vector<uint8> maskBytes{ 7, 200, 0xEE, 0xEE };
    SW_ASSERT_TRUE( sw::RenderTargetImageFile::unpackRgba8( maskBytes, byteLayout, sw::RHIFormat::R8_UNORM, rgbaBytes ) );
    const sw::vector<uint8> expectedMask{ 7, 7, 7, 255, 200, 200, 200, 255 };
    SW_EXPECT_TRUE( rgbaBytes == expectedMask );
}

/**
 * @brief [FrameCaptureTest] 디바이스가 없으면 텍스처를 만들지 않고 읽지도 않으며, 크기나 포맷이 다른 출력은 복사 대상이 아니다
 */
SW_TEST_CASE( FrameCaptureTest, StaysInactiveWithoutDeviceAndRejectsMismatchedOutput )
{
    sw::IRHIDevice*             pDevice = nullptr;
    sw::TransientAttachmentPool pool;
    sw::RenderPipelineAsset     pipeline;
    sw::FrameCapture            capture( pDevice, pool, pipeline );
    SW_EXPECT_FALSE( capture.isPresentEnabled() );
    capture.setPresentEnabled( true );
    capture.ensurePresent( 64, 32 );
    SW_EXPECT_TRUE( capture.isPresentEnabled() );
    SW_EXPECT_FALSE( capture.isPresentActive() ); // 디바이스가 없어 텍스처가 없다.
    SW_EXPECT_EQUAL( capture.getPresentTexture(), static_cast<sw::RHITextureHandle>( 0 ) );

    // 만든 적 없는 캡처의 크기는 0 이라 어떤 출력과도 맞지 않는다. 포맷이 계약값이 아니어도 맞지 않는다.
    SW_EXPECT_FALSE( capture.matchesOutput( 64, 32, sw::constant::kBackBufferFormat ) );
    SW_EXPECT_FALSE( capture.matchesOutput( 0, 0, sw::RHIFormat::R16G16B16A16_FLOAT ) );
    SW_EXPECT_TRUE( capture.matchesOutput( 0, 0, sw::constant::kBackBufferFormat ) );

    sw::vector<uint8>     bytes;
    sw::RHITextureMipSpan layout{};
    SW_EXPECT_FALSE( capture.readbackPresent( bytes, layout ) );
    sw::RHIFormat format = sw::RHIFormat::Unknown;
    SW_EXPECT_FALSE( capture.readbackTransient( "SceneColor", bytes, layout, format ) );
    SW_EXPECT_FALSE( capture.dumpTextureToFile( 1, sw::constant::kBackBufferFormat, "unused.ppm" ) );
}

/**
 * @brief [TransientAttachmentPoolTest] 꺼진 효과의 출력 이름은 원본을 끝까지 따라가고(효과가 잇달아 꺼진 경우), 그만두면 자기 이름으로 돌아온다
 */
SW_TEST_CASE( TransientAttachmentPoolTest, ForwardedNamesFollowTheSourceChain )
{
    sw::TransientAttachmentPool pool;
    SW_EXPECT_TRUE( pool.forward( "DepthOfFieldColor", "SceneColor" ) );
    SW_EXPECT_FALSE( pool.forward( "DepthOfFieldColor", "SceneColor" ) ); // 같은 곳이면 바뀐 것이 없다
    SW_EXPECT_TRUE( pool.forward( "MotionBlurColor", "DepthOfFieldColor" ) );
    SW_EXPECT_TRUE( pool.isForwarded( "MotionBlurColor" ) );
    SW_EXPECT_TRUE( pool.resolveName( "MotionBlurColor" ) == "SceneColor" );
    SW_EXPECT_TRUE( pool.resolveName( "SceneColor" ) == "SceneColor" );

    SW_EXPECT_TRUE( pool.stopForward( "DepthOfFieldColor" ) );
    SW_EXPECT_FALSE( pool.stopForward( "DepthOfFieldColor" ) );
    SW_EXPECT_TRUE( pool.resolveName( "MotionBlurColor" ) == "DepthOfFieldColor" );
    // 만든 첨부가 없으니 따라가도 따라가지 않아도 핸들은 0 이다.
    SW_EXPECT_EQUAL( pool.findTexture( "MotionBlurColor" ), static_cast<sw::RHITextureHandle>( 0 ) );
    SW_EXPECT_EQUAL( pool.findOwnTexture( "MotionBlurColor" ), static_cast<sw::RHITextureHandle>( 0 ) );

    pool.forget();
    SW_EXPECT_FALSE( pool.isForwarded( "MotionBlurColor" ) );
}

/**
 * @brief [TransientAttachmentPoolTest] 꺼진 효과의 타깃은 이어서 `kIdleFramesBeforeRelease` 프레임 쓰이지 않았을 때 한 번만 놓을 때가 된다 — 그보다 짧은 켜고 끄기는 세지 않는다
 */
SW_TEST_CASE( TransientAttachmentPoolTest, IdleTargetsReleaseOnlyAfterTheFrameCount )
{
    constexpr uint32 kIdle = sw::TransientAttachmentPool::kIdleFramesBeforeRelease;
    SW_EXPECT_TRUE( kIdle > 1u );
    // 모션 블러가 카메라가 서고 움직일 때마다 켜지고 꺼지는 꼴: 기준의 절반씩 꺼졌다 한 프레임 켜진다.
    uint32 idleFrames   = 0;
    uint32 releaseCount = 0;
    for ( uint32 frameIndex = 0; frameIndex < kIdle * 8u; ++frameIndex )
    {
        const bool bUsed = ( frameIndex % ( kIdle / 2u + 1u ) ) == 0u;
        releaseCount += sw::TransientAttachmentPool::advanceIdleFrames( idleFrames, bUsed ) ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( releaseCount, 0u );
    // 이어서 꺼져 있으면 기준 프레임에 한 번만 놓을 때가 된다(그 뒤로는 놓을 것이 없다).
    idleFrames = 0;
    for ( uint32 frameIndex = 1; frameIndex <= kIdle * 2u; ++frameIndex )
    {
        const bool bRelease = sw::TransientAttachmentPool::advanceIdleFrames( idleFrames, false );
        SW_EXPECT_EQUAL( bRelease, frameIndex == kIdle );
    }
    // 쓰이면 처음부터 다시 센다.
    SW_EXPECT_FALSE( sw::TransientAttachmentPool::advanceIdleFrames( idleFrames, true ) );
    SW_EXPECT_EQUAL( idleFrames, 0u );
    // 만든 첨부가 없는 이름은 놓을 것이 없고, 만든 횟수도 0 이다(디바이스 없음).
    sw::TransientAttachmentPool pool;
    SW_EXPECT_FALSE( pool.releaseIfIdle( nullptr, "DepthOfFieldColor", false ) );
    SW_EXPECT_EQUAL( pool.getAllocationCount(), 0u );
}
