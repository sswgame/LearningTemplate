/**
 * @file EditorFileDrop.h
 * @brief OS 파일 끌어 놓기를 에디터 UI 프레임으로 넘기는 한 곳입니다 — 창이 낸 사건(`WindowFileDropEvent`)을 ImGui 화면 좌표로 바꿔 들고, 받는 창(콘텐츠 브라우저)이 꺼낸다.
 * @details 창 처리기(`ImGuiEditor` 의 창 구독)와 자체 시험 입력(`EditorSelfTestInput::dropFiles`)이 같은 입구(`post*`)로 넣는다 — 시나리오가 OS 없이 같은 길을 돈다.
 *          놓는 동안(능력 `Hover` 인 창 — X11 · Wayland)은 받는 창이 놓을 자리를 보이고(EditorUI R11), 놓은 것은 받는 창이 한 번 꺼낸다(`takeDrop`).
 *          경로는 믿지 않는 바깥 입력이라 여기서는 옮기기만 하고 검사는 받는 쪽 모델이 한다(`ContentBrowserFileDropPlan`). 상태는 에디터 범위 상태 저장소가 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Window/WindowFileDrop.h"

namespace sw::editor
{
    /**
     * @struct EditorFileDrop
     * @brief OS 파일 끌어 놓기 상태의 입구 · 출구입니다(정적 함수 — 상태는 에디터 범위 저장소).
     */
    struct SW_EDITOR_API EditorFileDrop
    {
        /** @brief 창 처리기가 부릅니다 — 창 클라이언트 좌표를 ImGui 화면 좌표(주 뷰포트 기준)로 바꿔 넣습니다. */
        static void post( const WindowFileDropEvent& event );
        /** @brief ImGui 화면 좌표 @p screenPosition 의 사건을 넣습니다(자체 시험 입력 — 레이블 자리에 놓는다). */
        static void postAtScreenPosition( WindowFileDropPhase phase, const vector<string>& listPath, const float2& screenPosition );
        /** @brief 파일을 끈 포인터가 에디터 창 위에 있으면 그 화면 좌표를 @p outPosition 에 담고 true 입니다(놓을 자리 미리보기 — R11). */
        [[nodiscard]] static bool findHover( float2& outPosition );
        /** @brief 놓인 것이 있으면 그 자리를 @p outPosition 에 담고 true 입니다(꺼내지 않는다 — 받는 창이 자기 위인지 본다). */
        [[nodiscard]] static bool peekDrop( float2& outPosition );
        /** @brief 놓인 것을 꺼냅니다. 없으면 false 이고 @p outListPath 는 빈다. 꺼낸 것은 다시 오지 않는다. */
        [[nodiscard]] static bool takeDrop( vector<string>& outListPath, float2& outPosition );
    };
} // namespace sw::editor
