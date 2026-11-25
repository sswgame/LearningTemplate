/**
 * @file EditorViewportVisualizer.h
 * @brief 컴포넌트 종류별 뷰포트 디버그 시각화를 등록합니다. 콜라이더 와이어프레임과 카메라 프러스텀이 예입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Config/EditorIconName.h"
#include "Editor/Common/EditorExtensionRegistry.h"
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
    struct EditorVisualizerDrawList;
    struct EditorVisualizerLine;
    struct EditorVisualizerScreenBillboard;

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
        /** @brief 이 프레임의 씬 오브젝트 매니저입니다. 씬 서브시스템(내비게이션)을 읽는 시각화가 씁니다. 활성 씬이 없으면 nullptr 입니다. */
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
     * @brief 시각화 하나의 등록 항목입니다. 시각화의 .cpp 가 `SW_EDITOR_VISUALIZER_GATHER`(Model) · `SW_EDITOR_VISUALIZER_TOOL`(View)로 둡니다.
     * @details 항목 하나가 라벨, 툴팁, 기본값, 그리기 함수를 모두 가지며, 툴바 체크박스는 이 항목들에서 만들어진다. `_order` 가 툴바
     *          체크박스의 순서다. 켬과 끔은 id 로 보관한다(`EditorVisualizerToggles`).
     */
    struct EditorVisualizerRegistration : EditorExtensionRegistration
    {
        /** @brief 화면에 바로 그리고 마우스로 조작하는 도구(기즈모 · 손잡이 · 붓)입니다. 도형만 내는 시각화는 쓰지 않는다(`_pGather`). 등록은 View 파일의 `SW_EDITOR_VISUALIZER_TOOL` 만 한다. */
        using ToolFunc = void ( * )( const EditorViewportVisualizerArgs& args );
        /** @brief 컴포넌트를 읽어 그릴 도형 값을 모읍니다(Model — ImGui 를 모른다). 그리기는 `EditorViewportVisualizer::drawAll` 이 값만 보고 한다. */
        using GatherFunc = void ( * )( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList );

        SW_EDITOR_EXTENSION_KIND( "visualizer" );

        EditorIconName _pIconName;    ///< 툴바 토글 버튼의 아이콘 이름(`SW_EDITOR_ICON_NAME`). 비었으면 라벨 문자로 된 버튼이다
        const utf8*    _pToggleLabel; ///< 짧은 이름. 툴팁의 제목이며, 아이콘이 없으면 버튼 문자로 쓴다
        const utf8*    _pTooltip;
        uint8          _bDefaultOn : 1;
        ToolFunc       _pTool;
        GatherFunc     _pGather{ nullptr };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorViewportVisualizer
     * @brief 등록된 뷰포트 디버그 시각화 중 켜진 것을 그립니다. 시각화를 추가하려면 자기 .cpp 에 등록 줄(`SW_EDITOR_VISUALIZER_GATHER` · `SW_EDITOR_VISUALIZER_TOOL`) 하나를 두면 됩니다.
     */
    class SW_EDITOR_API EditorViewportVisualizer
    {
    public:
        /** @brief 등록된 시각화 수를 반환합니다. */
        static uint32 getCount();
        /** @brief 순서대로 index 번째 시각화를 반환합니다. */
        static const EditorVisualizerRegistration& getAt( uint32 index );
        /**
         * @brief 모은 도형 값(@p drawList, `EditorVisualizerDrawList::gatherAll`)을 그리고, @p toggles 에서 켜진 도구(`_pTool`)를 부릅니다.
         * @details 도형을 그릴 때 화면에 남은 선 · 글자 수를 `args._pStats` 에 적는다(통계는 매 프레임 다시 센다). 아이콘 배지는 그은 화면 자리를 @p outListScreenBillboard 에 남긴다(먼저 비운다) —
         *          클릭 선택(`EditorViewportPicker`)이 그 목록으로 배지를 집는다.
         */
        static void drawAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles, const EditorVisualizerDrawList& drawList,
                             vector<EditorVisualizerScreenBillboard>& outListScreenBillboard );
        /**
         * @brief 월드 선분 값들을 캔버스에 긋습니다(근평면에서 잘라 투영, 테마 색 역할을 푼다). 그린 선분 수를 돌려준다(다 잘린 것은 빼고).
         * @details 미리보기 패널처럼 시각화 등록 밖에서 모은 선(소켓 축 · 관통 점 …)을 같은 규칙으로 그릴 때 쓴다.
         */
        static uint32 drawLines( ImDrawList* pDrawList, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize, const vector<EditorVisualizerLine>& listLine );
    };
} // namespace sw::editor

// 화면 도구(`_pTool`)를 등록하는 줄은 MVVM View 번역 단위에서만 컴파일된다 — View 부분(`EditorTier<N>MVVMView` · 확장의 `<이름>PanelView`)만 빌드가
// SW_EDITOR_PANEL_VIEW=1 을 주고, 그 부분은 씬 · 모델 헤더 껍데기를 받는다. 그래서 도구를 등록하면 그 파일이 View 목록에 들어 있지 않고는 짓지 못한다.
// 도구 함수 시그니처를 값 문맥으로 좁히는 길은 고르지 않았다 — 언리얼 FEditorViewportClient 확장 · 유니티 EditorTool 처럼 도구 본문은 전역 서비스(활성 씬 · 선택)에
// 닿을 수 있어 시그니처로는 씬 읽기를 막지 못하고, include 경계(껍데기)만 막는다.
#ifndef SW_EDITOR_PANEL_VIEW
    #define SW_EDITOR_PANEL_VIEW 0
#endif

/**
 * @brief 도형 값을 모으는 뷰포트 시각화(화면 도구 없음)를 그 시각화의 .cpp 에서 등록합니다. Model 파일에서 쓴다.
 * @param name         파일 안에서 유일한 이름 조각입니다. 변수 이름에 씁니다.
 * @param pID          시각화 id 입니다. 리터럴이며 종류 안에서 유일해야 합니다.
 * @param order        툴바 토글 순서입니다. 작을수록 왼쪽입니다.
 * @param pIcon        툴바 토글 버튼 아이콘 이름(`SW_EDITOR_ICON_NAME`)입니다. nullptr 이면 라벨 문자를 씁니다.
 * @param pToggleLabel 짧은 이름이며 툴팁의 제목입니다.
 * @param pTooltip     체크박스 툴팁입니다.
 * @param bDefaultOn   처음에 켜져 있는지 정합니다.
 * @param pGather      `void( const EditorViewportVisualizerArgs&, EditorVisualizerDrawList& )` 모으기 함수입니다.
 */
// pToggleLabel · pTooltip 은 현지화 키다(`SW_LOCKEYREF( "EdVisualizer", "..." )`) — 툴바가 그릴 때 지금 언어로 푼다.
#define SW_EDITOR_VISUALIZER_GATHER( name, pID, order, pIcon, pToggleLabel, pTooltip, bDefaultOn, pGather ) SW_EDITOR_REGISTER( ::sw::editor::EditorVisualizerRegistration, Visualizer_##name, { pID, order }, pIcon, pToggleLabel, pTooltip, bDefaultOn, nullptr, pGather )
/**
 * @brief 화면 도구(`_pTool` — 값을 그리고 마우스를 명령으로 낸다)가 있는 시각화를 등록합니다. **MVVM View 파일에서만** 컴파일된다(위 주석). 앞 인자는
 *        `SW_EDITOR_VISUALIZER_GATHER` 와 같고, @p pGather 는 모으기가 없으면 nullptr 입니다(Model 의 모으기 함수는 그 Model 헤더가 선언한다).
 */
#define SW_EDITOR_VISUALIZER_TOOL( name, pID, order, pIcon, pToggleLabel, pTooltip, bDefaultOn, pTool, pGather )                                                \
    static_assert( SW_EDITOR_PANEL_VIEW, "SW_EDITOR_VISUALIZER_TOOL: a viewport tool draws values and sends commands - register it from an MVVM View file "     \
                                         "([panelViewBan] views in Config/Rules/Layers/EditorLayers.toml, or PANEL_VIEW_SOURCES of sw_add_editor_extension)" ); \
    SW_EDITOR_REGISTER( ::sw::editor::EditorVisualizerRegistration, Visualizer_##name, { pID, order }, pIcon, pToggleLabel, pTooltip, bDefaultOn, pTool, pGather )
