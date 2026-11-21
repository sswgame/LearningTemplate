/**
 * @file SceneViewOverlayLayoutStore.h
 * @brief 씬 뷰 오버레이 바 배치(보임 · 접힘 · 붙은 쪽 · 위치 비율)의 주인과 그 저장입니다 — 에디터 서브시스템이라 씬 뷰 창이 그려지지 않아도 명령이 바로 고친다.
 * @details 언리얼이 뷰포트 설정을 설정 객체(`ULevelEditorViewportSettings`)에 두고 명령이 그것을 고치며 뷰포트는 읽기만 하는 것과 같은 선이다.
 *          그리기 · 끌기 런타임(지난 프레임 위치 · 크기)은 View(`EditorViewportOverlays`)가 들고, 배치는 여기서 읽고 바꾸며 바꿀 때마다 저장한다.
 *          View 는 이 서브시스템을 찾지 못한다(서비스 창구가 `[panelViewBan]` 에 막힌다) — 배치는 `EditorViewportOverlaysViewModel` 을 거쳐 읽고 바꾸고, 여기서는 정적 바 표(`kArrBar` · `findBarDesc`)만 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Viewport/Model/EditorViewportOverlayLayout.h"

namespace sw::editor
{
    /** @brief 씬 뷰 오버레이 바 하나의 고정 정보입니다(id · 제목 · 기본 위치). */
    struct SceneViewOverlayBarDesc
    {
        const utf8*       _pID;
        const utf8*       _pTitle;
        EditorOverlayDock _defaultDock;
        float2            _defaultFraction;
    };

    /**
     * @class SceneViewOverlayLayoutStore
     * @brief 에디터 범위 서브시스템입니다(`editor::getSubsystem<SceneViewOverlayLayoutStore>()`). UI 스레드에서만 쓴다.
     * @details 초기화에서 상태 폴더의 `ViewportOverlays.json` 을 읽고 기본 바를 등록한다. 의존하는 에디터 서브시스템은 없다 — 상태 폴더는 `EditorPaths`(호스트 서비스)가 정한다.
     */
    class SceneViewOverlayLayoutStore final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "SceneViewOverlayLayoutStore";
        static constexpr const utf8* kFileName      = "ViewportOverlays.json";

        /** @brief 씬 뷰의 바 넷(등록 · 메뉴 순)입니다. */
        static constexpr SceneViewOverlayBarDesc kArrBar[] = {
            {     "view",      "View",    EditorOverlayDock::Top, float2{ 0.0f, 0.0f }},
            {"transform", "Transform",    EditorOverlayDock::Top, float2{ 0.5f, 0.0f }},
            {  "display",   "Display", EditorOverlayDock::Bottom, float2{ 0.0f, 1.0f }},
            {    "tools",     "Tools", EditorOverlayDock::Bottom, float2{ 1.0f, 1.0f }},
        };

        /** @brief 저장된 배치를 읽고 기본 바를 등록합니다. 깨진 파일은 키와 함께 경고를 남기고 기본 배치로 둔다(다음 저장이 다시 쓴다). */
        SubsystemInitResult initializeSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief @p barID 의 고정 정보입니다. 씬 뷰 바가 아니면 nullptr 입니다. */
        [[nodiscard]] static const SceneViewOverlayBarDesc* findBarDesc( string_view barID );
        /** @brief @p barID 가 씬 뷰 오버레이 바 id(view · transform · display · tools)인지입니다. */
        [[nodiscard]] static bool isKnownBarID( string_view barID ) { return findBarDesc( barID ) != nullptr; }

        const EditorViewportOverlayLayout& getLayout() const { return _layout; }

        /** @brief 모든 바를 기본 위치 · 보임으로 되돌리고 저장합니다. */
        void resetToDefault();
        /** @brief 바를 보이거나 숨기고 저장합니다. 그 id 의 바가 없으면 false 입니다. */
        bool setBarVisible( string_view barID, bool bVisible );
        /** @brief 바를 접거나 펴고 저장합니다. 그 id 의 바가 없으면 false 입니다. */
        bool setBarCollapsed( string_view barID, bool bCollapsed );
        /**
         * @brief 끌어 놓은 위치로 바의 붙은 쪽 · 비율을 정하고 저장합니다(`EditorViewportOverlayLayout::applyDrop`). 그 id 의 바가 없으면 false 입니다.
         * @param position 놓은 왼쪽 위(여백 안쪽 기준 픽셀), @param barSize 바 크기, @param viewSize 여백을 뺀 뷰포트 크기, @param snapDistance 붙는 거리(DPI 반영)
         */
        bool dropBar( string_view barID, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance );

    private:
        void save() const;

    private:
        EditorViewportOverlayLayout _layout;
    };
} // namespace sw::editor
