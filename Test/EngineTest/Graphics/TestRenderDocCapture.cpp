#include "pch.h"

#include "Engine/Renderer/Capture/RenderDocCapture.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [RenderDocCaptureTest] RenderDoc 없이(`-renderdoc` 없음 · RenderDoc 으로 띄우지 않음) 붙지 않고, 캡처 · UI 요청은 아무것도 하지 않고 죽지 않는다
 * @details 실제 캡처는 RenderDoc 이 설치된 기계에서만 된다 — 시험은 "없을 때 조용히 꺼진다" 쪽만 본다.
 */
SW_TEST_CASE( RenderDocCaptureTest, IsUnavailableWithoutRenderDoc )
{
    sw::RenderDocCapture::initialize( false );
    SW_EXPECT_FALSE( sw::RenderDocCapture::isAvailable() );
    sw::RenderDocCapture::triggerCapture();
    SW_EXPECT_FALSE( sw::RenderDocCapture::launchReplayUI() );
}
