/**
 * @file UIScreenMarkers.h
 * @brief 화면 마커(월드를 따라가는 HUD 위젯)의 화면 핸들과 그리기 앵커 표입니다(`UISystem` 의 값 멤버).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Screen/UIScreenTypes.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw
{
    /**
     * @class UIScreenMarkers
     * @brief 마커를 담는 HUD 화면의 핸들, 마커마다의 앵커(UI 단위), 뗀 마커가 돌려준 앵커 번호, 그리기 단계가 낸 앵커(물리 픽셀 정수)를 듭니다.
     * @details 마커 화면을 올리고 닫는 일과 위젯 붙이기 · 떼기는 화면 스택의 일이라 `UISystem` 이 한다. 이 클래스는 핸들과 앵커 번호 · 값만 맡는다.
     */
    class SW_API UIScreenMarkers
    {
    public:
        UIScreenMarkers();
        ~UIScreenMarkers();
        UIScreenMarkers( const UIScreenMarkers& )            = delete;
        UIScreenMarkers& operator=( const UIScreenMarkers& ) = delete;

        /** @brief 마커를 담는 HUD 화면입니다(없으면 `kInvalidUIScreenHandle`). */
        UIScreenHandle getScreen() const { return _screen; }
        void           setScreen( UIScreenHandle screen ) { _screen = screen; }
        /** @brief 앵커 번호 하나를 줍니다(뗀 마커의 번호부터 다시 쓴다). 앵커 값은 비어 있다 — 첫 위치는 붙인 쪽이 정한다. */
        uint32 allocateAnchor();
        /** @brief 뗀 마커의 앵커 번호를 돌려받습니다(다음 마커가 다시 쓴다). */
        void releaseAnchor( uint32 anchor )
        {
            clearGPUMarker( anchor );
            _listFreeAnchor.push_back( anchor );
        }
        /** @brief 앵커 @p anchor 의 위치(UI 단위)와 배율을 정합니다. 마커 위젯이 살아 있는 동안만 부른다(번호는 `allocateAnchor` 가 줬다). */
        void setAnchor( uint32 anchor, const float2& position, float32 scale );
        /** @brief 마커 앵커를 배율 @p uiScale 의 물리 픽셀 정수로 바꿔 `getCanvasAnchors` 표로 냅니다. */
        void publishCanvasAnchors( float32 uiScale );
        /** @brief 그리기 단계가 낸 앵커입니다(번호 = 마커 위젯의 그리기 앵커, 물리 픽셀 정수). */
        const vector<CanvasAnchor>& getCanvasAnchors() const { return _listCanvasAnchor; }
        /**
         * @brief 앵커 @p anchor 를 GPU 가 투영하는 마커로 올립니다(`CanvasMarkerRecord::_bActive` 를 켠다). 월드 점과 보이기 규칙만 적고 화면 좌표는 렌더러의 컴퓨트가 낸다.
         * @details 마커 위젯이 살아 있는 동안만 부른다. 같은 값이면 아무것도 하지 않는다(활성 수가 컴퓨트를 돌릴지 정한다).
         */
        void setGPUMarker( uint32 anchor, const CanvasMarkerRecord& record );
        /** @brief 앵커 @p anchor 를 GPU 투영에서 뺍니다(CPU 투영으로 돌아가거나 숨겼다). 활성이 아니면 아무것도 하지 않는다. */
        void clearGPUMarker( uint32 anchor );
        /** @brief 이번 프레임 GPU 투영이 쓰는 공용 값(카메라 · 뷰포트)을 정합니다. 활성 마커가 없으면 부르지 않는다. */
        void setGPUMarkerView( const CanvasMarkerView& view, const GameObjectManager* pManager )
        {
            const float32 uiScale = _markerView._uiScale; // 배율은 그리기 단계(`publishCanvasAnchors`)가 정한다
            _markerView           = view;
            _pGPUManager          = pManager;
            _markerView._uiScale  = uiScale;
        }
        /** @brief GPU 마커의 기록이 속한 오브젝트 관리자입니다(렌더 패킷의 활성 씬과 같을 때만 인스턴스 칸을 푼다). */
        const GameObjectManager* getGPUMarkerManager() const { return _pGPUManager; }
        /** @brief 켜진 GPU 마커 수입니다. 0 이면 패킷에 아무것도 싣지 않는다. */
        uint32 getGPUMarkerCount() const { return _activeMarkerCount; }
        /** @brief 패킷에 실을 GPU 마커 기록입니다(앵커 번호가 인덱스). 켜진 것이 없으면 빈 목록입니다. */
        const vector<CanvasMarkerRecord>& getGPUMarkers() const { return _listMarker; }
        /** @brief 패킷에 실을 공용 값입니다(`publishCanvasAnchors` 가 배율을 채운다). */
        const CanvasMarkerView& getGPUMarkerView() const { return _markerView; }
        /** @brief 마커 화면을 잊고 앵커 번호를 모두 버립니다(마지막 마커를 뗐을 때). 낸 앵커 표는 다음 `publishCanvasAnchors` 가 맞춘다. */
        void forgetScreen();
        /** @brief `forgetScreen` 에 더해 낸 앵커 표도 비웁니다(종료). */
        void clear();

    private:
        vector<CanvasAnchor>       _listAnchor;        ///< 화면 마커의 앵커입니다(번호 = 마커 위젯의 그리기 앵커, UI 단위).
        vector<CanvasMarkerRecord> _listMarker;        ///< GPU 가 투영하는 마커의 입력입니다(앵커 번호가 인덱스, 비활성은 `_bActive` 0).
        CanvasMarkerView           _markerView;        ///< 위 기록이 쓰는 공용 값입니다.
        uint32                     _activeMarkerCount; ///< `_listMarker` 중 켜진 기록 수입니다.
        const GameObjectManager*   _pGPUManager;       ///< `_markerView` 를 구한 관리자입니다(빌린 포인터).
        vector<CanvasAnchor>       _listCanvasAnchor;  ///< 그리기 단계가 낸 앵커입니다(물리 픽셀 정수).
        vector<uint32>             _listFreeAnchor;    ///< 뗀 마커가 돌려준 앵커 번호입니다.
        UIScreenHandle             _screen;            ///< 화면 마커를 담는 HUD 화면입니다(없으면 무효).
    };
} // namespace sw
