/**
 * @file EditorViewportOverlayLayout.h
 * @brief 씬 뷰 위에 떠 있는 오버레이 바(유니티 씬 뷰 Overlays)의 위치 · 붙은 쪽 · 접힘 · 보임과 그 저장입니다(ImGui 없음 — EditorTest 가 테스트합니다).
 * @details 저장은 사용자 상태라 리플렉션 JSON(`EditorViewportOverlayFile`)이다(계획 StructureSimplification TX2 — 예전 `key=value`). 옛 파일을 읽는 길은 두지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief 오버레이 바가 붙은 쪽입니다. 왼쪽 · 오른쪽에 붙으면 세로로 쌓는다. */
    ENUM()
    enum class EditorOverlayDock : uint8
    {
        Free = 0, ///< 뷰포트 안 아무 데나(가장자리에서 떨어져 있다)
        Top,
        Bottom,
        Left,
        Right,
    };

    /** @brief 오버레이 바 하나의 상태입니다. 위치는 뷰포트 크기에 대한 비율이라 뷰포트가 바뀌어도 화면 안에 남는다. */
    REFLECT()
    struct SW_EDITOR_API EditorOverlayBarState
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id;
        PROPERTY()
        EditorOverlayDock _dock{ EditorOverlayDock::Top };
        PROPERTY()
        float2 _fraction{}; ///< 남는 공간(뷰포트 − 바 크기)에 대한 왼쪽 위의 비율 0..1. 붙었으면 붙은 축의 값은 0 · 1 로 고정
        PROPERTY()
        bool _bCollapsed{ false };
        PROPERTY()
        bool _bVisible{ true };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 오버레이 배치 파일(`Saved/Editor/ViewportOverlays.json`)의 본문입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorViewportOverlayFile
    {
        REFLECT_BODY();

        PROPERTY()
        vector<EditorOverlayBarState> _listBar; ///< 저장한 바(등록 순)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorViewportOverlayLayout
     * @brief 한 뷰(씬 뷰)의 오버레이 바 배치입니다. 놓는 위치가 가장자리에서 `snapDistance` 안이면 그 가장자리에 붙인다.
     */
    struct SW_EDITOR_API EditorViewportOverlayLayout
    {
        vector<EditorOverlayBarState> _listBar;
        vector<EditorOverlayBarState> _listDefault; ///< 등록한 기본값(Reset 이 되돌린다)

        /** @brief 바를 등록합니다. 저장된 상태가 없으면 기본값을 쓴다. 이미 있으면 기본값만 적는다. */
        void registerBar( const EditorOverlayBarState& defaultState );
        /** @brief 바 상태입니다. 없으면 nullptr 입니다. */
        EditorOverlayBarState*       findBar( string_view barID );
        const EditorOverlayBarState* findBar( string_view barID ) const;
        /** @brief 모든 바를 기본값으로 되돌립니다(보임 · 접힘 포함). */
        void resetToDefault();

        /** @brief 바의 왼쪽 위 좌표(뷰포트 왼쪽 위 기준 픽셀)입니다. 바가 뷰포트보다 크면 0 입니다. */
        static float2 computePosition( const EditorOverlayBarState& state, const float2& barSize, const float2& viewSize );
        /**
         * @brief 끌어 놓은 위치(왼쪽 위, 뷰포트 기준 픽셀)로 상태를 정합니다. 가장자리에서 @p snapDistance 안이면 그 쪽에 붙인다(가까운 쪽 하나).
         * @details 위치는 뷰포트 안으로 묶는다. 붙으면 붙은 축의 비율은 0(위 · 왼쪽) 또는 1(아래 · 오른쪽)이다.
         */
        static void applyDrop( EditorOverlayBarState& state, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance );
        /** @brief 세로로 쌓는 쪽(왼쪽 · 오른쪽)에 붙었으면 true 입니다. */
        static bool isVertical( const EditorOverlayBarState& state );

        /** @brief 바 상태를 @p absPath 에 JSON 으로 씁니다. */
        [[nodiscard]] bool saveFile( string_view absPath ) const;
        /**
         * @brief @p absPath 에서 엄격하게 읽습니다. 아직 등록하지 않은 바도 받아 두고(등록할 때 그 값을 쓴다), 비율은 0..1 로 묶습니다.
         * @return 없으면 `Missing`(지금 상태 그대로), 틀리면 `Invalid`(키와 함께 오류를 남겼고 지금 상태 그대로)입니다.
         */
        [[nodiscard]] ConfigReadResult loadFile( string_view absPath );
    };
} // namespace sw::editor
