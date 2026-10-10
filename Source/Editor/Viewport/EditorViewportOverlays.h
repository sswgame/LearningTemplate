/**
 * @file EditorViewportOverlays.h
 * @brief 씬 뷰 위에 떠 있는 오버레이 바들입니다(유니티 씬 뷰 Overlays) — 끌어 옮기고, 가장자리에 붙이고, 접고, 숨깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/EditorViewportOverlayLayout.h"

namespace sw::editor
{
    struct ViewportToolbarSettings;

    /** @brief 오버레이 바 하나의 이번 실행 상태(저장하지 않는다)입니다. */
    struct EditorOverlayBarRuntime
    {
        string _id;
        float2 _lastPosition{}; ///< 지난 프레임에 그린 왼쪽 위(뷰포트 기준 픽셀)
        float2 _lastSize{};     ///< 지난 프레임의 바 크기(픽셀)
        float2 _dragPosition{}; ///< 끄는 중의 왼쪽 위
        bool   _bDragging{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorViewportOverlays
     * @brief 씬 뷰의 오버레이 바 넷(보기 · 표시 · 도구 · 트랜스폼)을 뷰포트 이미지 위에 그립니다.
     * @details 바마다 손잡이를 끌어 뷰포트 안 어디든 옮기고, 놓은 자리가 가장자리에서 16 px(× DPI) 안이면 그 쪽에 붙입니다(왼쪽 · 오른쪽이면 세로로 쌓는다).
     *          손잡이 오른쪽 클릭 · 씬 뷰 툴바의 Overlays 메뉴로 바마다 보이기/숨기기와 기본 위치 복원을 합니다. 자리 · 붙은 쪽 · 접힘 · 보임은
     *          `Saved/Editor/ViewportOverlays.ini` 의 `scene.` 칸에 저장합니다(게임 뷰에는 바가 없다). 바는 자식 창이라 그 위의 마우스는 뷰포트
     *          조작(피킹 · 호버 · 궤도)으로 새지 않는다 — 캔버스의 `IsItemHovered` 가 거짓이 된다.
     */
    class SW_EDITOR_API EditorViewportOverlays
    {
    public:
        EditorViewportOverlays();

        /** @brief 바들을 뷰포트 이미지(@p canvasMin, @p canvasSize — 화면 픽셀) 위에 그립니다. 이미지를 그린 뒤 부릅니다. */
        void draw( const float2& canvasMin, const float2& canvasSize, ViewportToolbarSettings& settings, bool bHasSelection );
        /** @brief 씬 뷰 툴바에 둘 Overlays 메뉴(바마다 보이기 · 기본 위치)입니다. */
        void drawOverlaysMenu();
        /** @brief 모든 바를 기본 자리 · 보임으로 되돌리고 저장합니다. */
        void resetToDefault();
        /** @brief 바를 보이거나 숨기고 저장합니다. 그 id 의 바가 없으면 false 입니다. */
        bool setBarVisible( string_view barID, bool bVisible );

        const EditorViewportOverlayLayout& getLayout() const { return _layout; }
        /** @brief 바의 이번 실행 상태입니다(탐침). 없으면 nullptr 입니다. */
        const EditorOverlayBarRuntime* findRuntime( string_view barID ) const;

    private:
        void                     ensureLoaded();
        void                     save() const;
        EditorOverlayBarRuntime& getRuntime( string_view barID );
        void                     drawBar( EditorOverlayBarState& state, const float2& canvasMin, const float2& canvasSize, ViewportToolbarSettings& settings, bool bHasSelection );
        void                     drawBarMenuItems();
        /** @brief 이번 프레임에 먼저 놓인 바와 겹치면 가장자리에서 먼 쪽으로 밀어 낸 자리입니다. */
        float2 pushOutOfPlacedBars( EditorOverlayDock dock, const float2& position, const float2& size ) const;

        /** @brief 이번 프레임에 놓인 바 하나(겹침 피하기)입니다. */
        struct PlacedBar
        {
            EditorOverlayDock _dock;
            float2            _position;
            float2            _size;
        };

        EditorViewportOverlayLayout     _layout;
        vector<EditorOverlayBarRuntime> _listRuntime;
        vector<PlacedBar>               _listPlaced; ///< 이번 프레임에 놓인 바
        bool                            _bLoaded;
    };
} // namespace sw::editor
