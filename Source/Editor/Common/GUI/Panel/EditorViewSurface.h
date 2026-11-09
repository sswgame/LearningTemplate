/**
 * @file EditorViewSurface.h
 * @brief 씬 뷰 · 게임 뷰 패널이 자기 렌더 타깃(이미지)을 다루는 창구입니다 — 그릴 텍스처, 크기 맞추기, 그린 자리 알리기, 게임 뷰 초점, 스크린숏.
 * @details 뷰 렌더 타깃은 작업 공간의 `EditorRenderViews` 가 소유한다. 패널 View 는 작업 공간 · 커맨드 헤더를 보지 않으므로(`docs/agents/EditorUI.md` MVVM 계약) 여기를 지난다 —
 *          이 헤더는 값 헤더(`EditorViewTargetUtil.h`)와 Core 만 include 하고 본문(.cpp)이 작업 공간 · 스크린숏 커맨드를 부른다(언리얼 `SViewport` 가 `FSceneViewport` 를 쥐는 자리).
 *          UI 가 메인 스레드를 떠나면 이 창구가 메인 스레드로 보낼 요청의 경계가 된다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

namespace sw::editor
{
    /**
     * @struct EditorViewSurface
     * @brief 뷰 렌더 타깃 창구입니다(상태 없음 — 에디터 컨텍스트가 없으면 아무것도 하지 않고 빈 값을 돌려준다).
     */
    struct SW_EDITOR_API EditorViewSurface
    {
        /** @brief 뷰 @p kind 가 지금 그릴 텍스처 id 입니다(ImGui 이미지). 타깃이 아직 없으면 nullptr 입니다. */
        static void* getTextureID( EditorViewKind kind );
        /** @brief 뷰 @p kind 의 렌더 타깃을 @p width x @p height 로 맞춥니다. 1 픽셀 차이는 무시한다(`EditorViewTargetUtil::needsResize`). */
        static void fitTargetSize( EditorViewKind kind, uint32 width, uint32 height );
        /** @brief 이번 프레임에 뷰 @p kind 를 그렸고 이미지 왼쪽 위가 주 창 클라이언트 픽셀 @p imageClientPos 에 놓였다고 알립니다(호스트가 포인터를 뷰 픽셀로 옮긴다). */
        static void markDrawn( EditorViewKind kind, const float2& imageClientPos );
        /** @brief 이번 UI 프레임에 뷰 @p kind 를 그렸으면 true 입니다(그 패널 탭이 앞에 있다). */
        static bool wasViewDrawn( EditorViewKind kind );
        /** @brief 이번 UI 프레임에 셸이 호스트에 뷰 @p kind 의 렌더 타깃을 알렸으면 true 입니다(`ImGuiEditor::getSceneViewport` · `getGameViewport` 와 같은 판정, 타깃이 있어야 한다). */
        static bool isViewRequested( EditorViewKind kind );
        /** @brief 게임 뷰 패널의 초점 · 마우스 올림을 알립니다(게임 입력이 이 값을 본다). */
        static void setGameViewInput( bool bFocused, bool bHovered );
        /** @brief 게임 뷰 패널에 초점이 있으면 true 입니다. */
        static bool isGameViewFocused();
        /** @brief 초점이 간 뷰를 기록합니다(스크린숏 단축키가 고르는 뷰). */
        static void recordFocusedView( EditorViewKind kind );
        /** @brief 뷰 @p kind 의 스크린숏을 찍습니다(다음 프레임에 저장한다). */
        static void captureScreenshot( EditorViewKind kind );
        /** @brief RHI 텍스처 @p rhiTexture 를 ImGui 이미지로 등록합니다(렌더 타깃 미리보기 · UI 미리보기). 렌더러가 아직 없거나 텍스처가 0 이면 nullptr 입니다(다음 프레임에 다시 부른다). */
        static void* registerImage( uint64 rhiTexture );
        /** @brief `registerImage` 가 준 id 를 돌려줍니다. 에디터 컨텍스트가 먼저 내려갔으면 아무것도 하지 않습니다. */
        static void unregisterImage( void* pTextureID );
        /**
         * @brief RHI 텍스처 @p rhiTexture 를 ImGui 이미지로 등록하고 UI 캡처에서 뺍니다(게임 뷰 위 개발 겹치기). 렌더러가 아직 없으면 nullptr 입니다(다음 프레임에 다시 부른다).
         * @details 받은 id 는 `unregisterOverlayTexture` 로 돌려준다.
         */
        static void* registerOverlayTexture( uint64 rhiTexture );
        /** @brief `registerOverlayTexture` 가 준 id 를 돌려줍니다. 에디터 컨텍스트가 먼저 내려갔으면 아무것도 하지 않습니다. */
        static void unregisterOverlayTexture( void* pTextureID );
    };
} // namespace sw::editor
