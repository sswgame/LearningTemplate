#include "pch.h"

#include "Editor/Common/Backend/ImGuiTextureUpdate.h"

#include "TestFramework/TestFramework.h"

#include <imgui.h>

/**
 * @brief [EditorUiTextureUpdateTest] 파괴할 텍스처는 그리는 스냅샷 · GPU 프레임이 다 지난 뒤에 백엔드로 넘긴다
 * @details ImGui 는 갈아 치운 글꼴 아틀라스를 `WantDestroy` 로 두고 미사용 프레임(`UnusedFrames`)을 센다. 바로 넘기면 백엔드가 이미지 ·
 *          디스크립터를 즉시 놓아, 그것을 아직 그리는 렌더 스레드 · GPU 가 해제된 자원을 읽는다. 생성 · 갱신은 바로 넘긴다.
 */
SW_TEST_CASE( EditorUiTextureUpdateTest, DestroyIsDeferredUntilInFlightFramesPass )
{
    ImTextureData texture;
    texture.Status = ImTextureStatus_OK;
    SW_EXPECT_FALSE( sw::editor::isTextureUpdateDue( texture ) );
    texture.Status = ImTextureStatus_WantCreate;
    SW_EXPECT_TRUE( sw::editor::isTextureUpdateDue( texture ) );
    texture.Status = ImTextureStatus_WantUpdates;
    SW_EXPECT_TRUE( sw::editor::isTextureUpdateDue( texture ) );

    texture.Status       = ImTextureStatus_WantDestroy;
    texture.UnusedFrames = 1;
    SW_EXPECT_FALSE( sw::editor::isTextureUpdateDue( texture ) );
    texture.UnusedFrames = sw::editor::kImGuiTextureDestroyDelayFrames - 1;
    SW_EXPECT_FALSE( sw::editor::isTextureUpdateDue( texture ) );
    texture.UnusedFrames = sw::editor::kImGuiTextureDestroyDelayFrames;
    SW_EXPECT_TRUE( sw::editor::isTextureUpdateDue( texture ) );

    // 지연은 스냅샷 슬롯과 GPU 프레임 둘 다를 넘어야 한다.
    SW_EXPECT_TRUE( sw::editor::kImGuiTextureDestroyDelayFrames > static_cast<int32>( sw::constant::kMaxFrameCountInFlight * 2u ) );
}
