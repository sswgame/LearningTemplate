/**
 * @file EditorViewportVisualizer.h
 * @brief 컴포넌트 종류별 뷰포트 디버그 시각화 등록 (콜라이더 와이어프레임, 카메라 프러스텀 …)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

struct ImDrawList;

namespace sw
{
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
         * @brief 이 프레임의 오브젝트 스냅샷. 호출부가 재사용 버퍼로 채워 넘깁니다.
         * @details 시각화마다 씬 목록을 따로 받으면 프레임마다 시각화 수만큼 씬 전체를 힙에
         *          복사한다(값 반환 getAllGameObjects()). 한 번 채워 함께 본다.
         */
        const vector<GameObject*>* _pListObject{ nullptr };
    };

    /**
     * @class EditorViewportVisualizer
     * @brief 뷰포트 디버그 시각화 표. 시각화를 하나 더하려면 표에 한 줄이면 됩니다.
     * @details 예전에는 `EditorViewportClient.cpp` 의 `drawDebugVisualizers` 안에 BoxCollider2D 와
     *          CameraComponent 가 손으로 나열되어 있었고, 각자 `ViewportToolbarSettings` 의 bool
     *          하나와 툴바 체크박스 하나에 짝지어 있었습니다 — 시각화를 하나 더하려면 세 파일 네 곳을
     *          고쳐야 했습니다. 지금은 표 한 줄이 라벨·툴팁·기본값·그리기 함수를 모두 들고 있고,
     *          툴바 체크박스는 그 표에서 만들어집니다.
     */
    class EditorViewportVisualizer
    {
    public:
        /** @brief 시각화 하나를 그립니다. */
        using DrawFunc = void ( * )( const EditorViewportVisualizerArgs& args );

        /** @brief 시각화 한 줄 */
        struct Row
        {
            const utf8* _pToggleLabel; ///< 툴바 체크박스에 보이는 짧은 라벨
            const utf8* _pTooltip;
            bool        _bDefaultOn;
            DrawFunc    _pDraw;
        };

        /** @brief 등록된 시각화 목록입니다. outCount에 개수를 씁니다. */
        static const Row* getRows( uint32& outCount );
        /** @brief 기본으로 켜지는 시각화 비트마스크입니다. */
        static uint32 getDefaultMask();
        /** @brief 마스크에서 켜진 시각화를 모두 그립니다. */
        static void drawAll( const EditorViewportVisualizerArgs& args, uint32 visualizerMask );
        /** @brief index번째 시각화의 마스크 비트입니다. */
        static uint32 getMaskBit( uint32 index ) { return 1u << index; }
    };
} // namespace sw::editor
