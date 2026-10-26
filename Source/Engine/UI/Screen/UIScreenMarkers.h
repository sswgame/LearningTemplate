/**
 * @file UIScreenMarkers.h
 * @brief 화면 마커(월드를 따라가는 HUD 위젯)의 화면 핸들과 그리기 앵커 표입니다(`UISystem` 의 값 멤버).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Screen/UIScreen.h"

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
        void releaseAnchor( uint32 anchor ) { _listFreeAnchor.push_back( anchor ); }
        /** @brief 앵커 @p anchor 의 위치(UI 단위)와 배율을 정합니다. 마커 위젯이 살아 있는 동안만 부른다(번호는 `allocateAnchor` 가 줬다). */
        void setAnchor( uint32 anchor, const float2& position, float32 scale );
        /** @brief 마커 앵커를 배율 @p uiScale 의 물리 픽셀 정수로 바꿔 `getCanvasAnchors` 표로 냅니다. */
        void publishCanvasAnchors( float32 uiScale );
        /** @brief 그리기 단계가 낸 앵커입니다(번호 = 마커 위젯의 그리기 앵커, 물리 픽셀 정수). */
        const vector<CanvasAnchor>& getCanvasAnchors() const { return _listCanvasAnchor; }
        /** @brief 마커 화면을 잊고 앵커 번호를 모두 버립니다(마지막 마커를 뗐을 때). 낸 앵커 표는 다음 `publishCanvasAnchors` 가 맞춘다. */
        void forgetScreen();
        /** @brief `forgetScreen` 에 더해 낸 앵커 표도 비웁니다(종료). */
        void clear();

    private:
        vector<CanvasAnchor> _listAnchor;       ///< 화면 마커의 앵커입니다(번호 = 마커 위젯의 그리기 앵커, UI 단위).
        vector<CanvasAnchor> _listCanvasAnchor; ///< 그리기 단계가 낸 앵커입니다(물리 픽셀 정수).
        vector<uint32>       _listFreeAnchor;   ///< 뗀 마커가 돌려준 앵커 번호입니다.
        UIScreenHandle       _screen;           ///< 화면 마커를 담는 HUD 화면입니다(없으면 무효).
    };
} // namespace sw
