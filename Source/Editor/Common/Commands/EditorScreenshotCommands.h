/**
 * @file EditorScreenshotCommands.h
 * @brief 에디터의 스크린샷 단추 · 커맨드 `viewport.screenshot` 입니다 — 게임 뷰 · 씬 뷰 그림을 `Saved/Screenshots/<시각>.png` 로.
 * @details ImGui 없이 렌더 스레드 요청 창구(`RenderThread::requestScreenshot`)만 부릅니다. 끝나면 셸이 프레임마다 부르는 `update` 가 토스트를 띄웁니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Viewport/EditorViewTargetUtil.h"

namespace sw::editor
{
    /**
     * @struct EditorScreenshotCommands
     * @brief 뷰 스크린샷 요청과 결과 알림입니다(유니티 Game view 스크린샷 · 언리얼 HighResShot 자리).
     */
    struct EditorScreenshotCommands
    {
        /** @brief 게임 뷰 그림을 찍습니다 — 화면에 나간 그림(주 출력)이라 게임 뷰가 보이면 게임 뷰, 아니면 씬 뷰다. */
        static void captureGameView();
        /** @brief 씬 뷰 그림을 찍습니다 — 씬 뷰 렌더 타깃을 읽는다(주 출력이 아니어도). 씬 뷰 RT 가 없으면 알리고 끝낸다. */
        static void captureSceneView();
        /** @brief 마지막으로 포커스를 받은 뷰를 찍습니다(커맨드 `viewport.screenshot`, F9). */
        static void captureFocusedView();
        /** @brief 뷰 패널이 포커스를 받았으면 그 종류를 적습니다(패널 그리기에서 부른다 — 단축키는 패널보다 먼저 처리된다). */
        static void noteFocusedView( EditorViewKind kind );
        /** @brief 요청이 끝났으면 토스트를 띄웁니다. 셸이 프레임마다 부릅니다. */
        static void update();
    };
} // namespace sw::editor
