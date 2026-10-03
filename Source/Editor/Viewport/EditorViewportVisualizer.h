/**
 * @file EditorViewportVisualizer.h
 * @brief 컴포넌트 종류별 뷰포트 디버그 시각화 등록 (콜라이더 와이어프레임, 카메라 프러스텀 …)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

struct ImDrawList;

namespace sw
{
    struct float4x4;

    class CameraComponent;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /** @brief 시각화 함수가 그리는 데 필요한 프레임 정보 */
    struct EditorViewportVisualizerArgs
    {
        ImDrawList*      _pDrawList{ nullptr };
        const float4x4*  _pViewProj{ nullptr };
        float2           _canvasPos{};
        float2           _canvasSize{};
        CameraComponent* _pActiveCamera{ nullptr }; ///< 자기 자신은 프러스텀을 그리지 않습니다
        /**
         * @brief 이 프레임의 오브젝트 스냅샷입니다. 부르는 쪽이 재사용 버퍼로 채워 넘깁니다.
         * @details 시각화마다 씬 목록을 따로 받으면 프레임마다 시각화 수만큼 씬 전체를 힙에
         *          복사합니다(값으로 반환하는 getAllGameObjects()). 한 번 채워 함께 봅니다.
         */
        const vector<GameObject*>* _pListObject{ nullptr };
        /** @brief 씬의 카메라 등록부 목록입니다(프러스텀 시각화가 씬 전체를 훑지 않게). 매니저가 없으면 nullptr 입니다. */
        const vector<CameraComponent*>* _pListCamera{ nullptr };
    };

    /**
     * @struct EditorVisualizerRegistration
     * @brief 시각화 하나의 등록 줄입니다. 시각화의 .cpp 가 `SW_EDITOR_VISUALIZER` 로 둡니다.
     * @details 한 줄이 라벨 · 툴팁 · 기본값 · 그리기 함수를 모두 들고, 툴바 체크박스는 이 줄들에서 만들어집니다. `_order` 가 툴바
     *          체크박스의 순서이자 시각화 마스크의 비트 자리입니다.
     */
    struct EditorVisualizerRegistration : EditorRegistration
    {
        /** @brief 시각화 하나를 그립니다. */
        using DrawFunc = void ( * )( const EditorViewportVisualizerArgs& args );

        static constexpr const utf8* kKindName = "visualizer";

        const utf8* _pToggleLabel; ///< 툴바 체크박스에 보이는 짧은 라벨
        const utf8* _pTooltip;
        bool        _bDefaultOn;
        DrawFunc    _pDraw;
    };

    /**
     * @class EditorViewportVisualizer
     * @brief 등록된 뷰포트 디버그 시각화를 마스크로 켜고 그립니다. 시각화를 하나 더하려면 자기 .cpp 에 `SW_EDITOR_VISUALIZER` 한 줄입니다.
     */
    class EditorViewportVisualizer
    {
    public:
        /** @brief 시각화 마스크가 uint32 라 시각화는 이만큼까지 켤 수 있습니다. 넘는 등록은 그리지 않습니다. */
        static constexpr uint32 kMaxVisualizerCount = 32;

        /** @brief 켤 수 있는 시각화 수입니다(`kMaxVisualizerCount` 이하). */
        static uint32 getCount();
        /** @brief 순서대로 index 번째 시각화입니다. */
        static const EditorVisualizerRegistration& getAt( uint32 index );
        /** @brief 기본으로 켜지는 시각화 비트마스크입니다. */
        static uint32 getDefaultMask();
        /** @brief 마스크에서 켜진 시각화를 모두 그립니다. */
        static void drawAll( const EditorViewportVisualizerArgs& args, uint32 visualizerMask );
        /** @brief index번째 시각화의 마스크 비트입니다. */
        static uint32 getMaskBit( uint32 index ) { return 1u << index; }
    };
} // namespace sw::editor

/**
 * @brief 뷰포트 시각화를 그 시각화의 .cpp 에서 등록합니다.
 * @param name         파일 안에서 유일한 이름 조각(변수 이름용)
 * @param pId          시각화 id(리터럴, 종류 안에서 유일)
 * @param order        툴바 체크박스 순서(작을수록 왼쪽)
 * @param pToggleLabel 툴바 체크박스 라벨
 * @param pTooltip     체크박스 툴팁
 * @param bDefaultOn   처음에 켜져 있는가
 * @param pDraw        `void( const EditorViewportVisualizerArgs& )` 그리기 함수
 */
#define SW_EDITOR_VISUALIZER( name, pId, order, pToggleLabel, pTooltip, bDefaultOn, pDraw )                                    \
    SW_EDITOR_REGISTER( ::sw::editor::EditorVisualizerRegistration, Visualizer_##name, { pId, order }, pToggleLabel, pTooltip, \
                        bDefaultOn, pDraw )
