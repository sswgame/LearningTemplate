/**
 * @file EditorViewportVisualizer.h
 * @brief 컴포넌트 종류별 뷰포트 디버그 시각화를 등록합니다. 콜라이더 와이어프레임과 카메라 프러스텀이 예입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorExtensionRegistry.h"
#include "Editor/Viewport/EditorVisualizerStats.h"
#include "Editor/Viewport/Model/EditorVisualizerToggles.h"

struct ImDrawList;

namespace sw
{
    struct float4x4;

    class BoxCollider2DComponent;
    class CameraComponent;
    class DebugDrawQueue;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 시각화 함수가 그리는 데 필요한 프레임 정보입니다. */
    struct EditorViewportVisualizerArgs
    {
        ImDrawList*      _pDrawList{ nullptr };
        const float4x4*  _pViewProj{ nullptr };
        float2           _canvasPos{};
        float2           _canvasSize{};
        CameraComponent* _pActiveCamera{ nullptr }; ///< 자기 자신의 프러스텀은 그리지 않는다
        /** @brief 씬의 카메라 레지스트리 목록입니다. 프러스텀 시각화가 씬 전체를 스캔하지 않게 합니다. 매니저가 없으면 nullptr 입니다. */
        const vector<CameraComponent*>* _pListCamera{ nullptr };
        /** @brief 씬의 콜라이더 레지스트리 목록입니다(`ScenePhysics::getColliders2D`). 매니저가 없으면 nullptr 입니다. */
        const vector<BoxCollider2DComponent*>* _pListCollider{ nullptr };
        /** @brief 이번 프레임의 디버그 도형 큐입니다(`DebugDrawQueue` 엔진 서비스). 없으면 nullptr 입니다. */
        const DebugDrawQueue* _pDebugDrawQueue{ nullptr };
        /** @brief 이 프레임의 씬 오브젝트 매니저입니다. 씬 서비스(내비게이션)를 읽는 시각화가 씁니다. 활성 씬이 없으면 nullptr 입니다. */
        GameObjectManager* _pObjectManager{ nullptr };
        /** @brief 변환 기즈모를 끌고 있으면 true 입니다. 콜라이더 손잡이는 이때 끌기를 시작하지 않는다. */
        bool _bGizmoDragging{ false };
        /** @brief 2D 뷰(직교 카메라가 월드 Z 축을 본다)이면 true 입니다(`EditorVisualizerGeometryUtil::isFlat2DView`). */
        bool _bFlat2D{ false };
        /** @brief 이 뷰포트가 든 시각화 통계입니다. 그린 양을 적는 시각화가 쓰며, 인자를 만드는 뷰포트가 항상 채웁니다(없는 경우가 없으니 널 검사를 하지 않는다). */
        EditorVisualizerStats* _pStats{ nullptr };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorVisualizerRegistration
     * @brief 시각화 하나의 등록 항목입니다. 시각화의 .cpp 가 `SW_EDITOR_VISUALIZER` 로 둡니다.
     * @details 항목 하나가 라벨, 툴팁, 기본값, 그리기 함수를 모두 가지며, 툴바 체크박스는 이 항목들에서 만들어진다. `_order` 가 툴바
     *          체크박스의 순서다. 켬과 끔은 id 로 보관한다(`EditorVisualizerToggles`).
     */
    struct EditorVisualizerRegistration : EditorExtensionRegistration
    {
        /** @brief 시각화 하나를 그립니다. */
        using DrawFunc = void ( * )( const EditorViewportVisualizerArgs& args );

        static constexpr const utf8* kKindName = "visualizer";

        const utf8* _pIcon;        ///< 툴바 토글 버튼의 아이콘(`editoricon::k*`). nullptr 이면 라벨 문자로 된 버튼이다
        const utf8* _pToggleLabel; ///< 짧은 이름. 툴팁의 제목이며, 아이콘이 없으면 버튼 문자로 쓴다
        const utf8* _pTooltip;
        bool        _bDefaultOn;
        DrawFunc    _pDraw;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorViewportVisualizer
     * @brief 등록된 뷰포트 디버그 시각화 중 켜진 것을 그립니다. 시각화를 추가하려면 자기 .cpp 에 `SW_EDITOR_VISUALIZER` 한 줄을 두면 됩니다.
     */
    class SW_EDITOR_API EditorViewportVisualizer
    {
    public:
        /** @brief 등록된 시각화 수를 반환합니다. */
        static uint32 getCount();
        /** @brief 순서대로 index 번째 시각화를 반환합니다. */
        static const EditorVisualizerRegistration& getAt( uint32 index );
        /** @brief @p toggles 에서 켜진 시각화를 모두 그립니다. */
        static void drawAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 선택 상자 시각화(`Visualizers/SelectionBoundsVisualizer.cpp`)가 지난 프레임에 그린 상자 수입니다. 프로브 `Editor.SelectionOutlineBoxes` 가 읽습니다. */
    struct SW_EDITOR_API EditorSelectionBounds
    {
        static uint32 getDrawnBoxCount();
        /** @brief 씬 뷰가 이번 프레임의 호버 오브젝트를 알립니다. 0 이면 없습니다. 선택한 오브젝트이면 그리지 않습니다. */
        static void   setHoveredObjectID( uint64 objectID );
        static uint64 getHoveredObjectID();
    };
} // namespace sw::editor

/**
 * @brief 뷰포트 시각화를 그 시각화의 .cpp 에서 등록합니다.
 * @param name         파일 안에서 유일한 이름 조각입니다. 변수 이름에 씁니다.
 * @param pID          시각화 id 입니다. 리터럴이며 종류 안에서 유일해야 합니다.
 * @param order        툴바 토글 순서입니다. 작을수록 왼쪽입니다.
 * @param pIcon        툴바 토글 버튼 아이콘(`editoricon::k*`)입니다. nullptr 이면 라벨 문자를 씁니다.
 * @param pToggleLabel 짧은 이름이며 툴팁의 제목입니다.
 * @param pTooltip     체크박스 툴팁입니다.
 * @param bDefaultOn   처음에 켜져 있는지 정합니다.
 * @param pDraw        `void( const EditorViewportVisualizerArgs& )` 시그니처의 그리기 함수입니다.
 */
#define SW_EDITOR_VISUALIZER( name, pID, order, pIcon, pToggleLabel, pTooltip, bDefaultOn, pDraw )                                    \
    SW_EDITOR_REGISTER( ::sw::editor::EditorVisualizerRegistration, Visualizer_##name, { pID, order }, pIcon, pToggleLabel, pTooltip, \
                        bDefaultOn, pDraw )
