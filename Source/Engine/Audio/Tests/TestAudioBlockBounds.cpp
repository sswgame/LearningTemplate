// AudioBlockView · AudioBlockBuffer 범위 검사 — 채널 수를 넘는 채널 · 블록 칸 수를 넘는 프레임 수가 개발 구성에서 단언으로 잡히고, 경계 안은 잡히지 않는지(음성 대조).
#include "pch.h"

#include "Core/TestFramework/ScopedAssertCapture.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Audio/AudioBlock.h"

using namespace sw;

/**
 * @brief [AudioBlockBoundsTest] 채널 · 프레임 경계
 * @details 경계 밖 채널은 주소만 만든다(읽지 않는다) — 가로채기 뒤에도 코드가 이어진다.
 */
SW_TEST_CASE( AudioBlockBoundsTest, ChannelAndFrameCountAreChecked )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    SW_REQUIRE_EQUAL( 2u, buffer.getChannelCount() );
    {
        const test::ScopedAssertCapture assertCapture;
        const AudioBlockView            view = buffer.makeView( audio::kBlockFrameCount );
        SW_EXPECT_TRUE( view.getChannel( 1 ) == view.getChannel( 0 ) + audio::kBlockFrameCount );
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    }
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
    {
        const test::ScopedAssertCapture assertCapture;
        const AudioBlockView            view = buffer.makeView( audio::kBlockFrameCount + 1 );
        (void)view.getChannel( 2 );
        SW_EXPECT_EQUAL( 2u, assertCapture.getCount() );
    }
}
