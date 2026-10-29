/**
 * @file UIStressTestScreen.h
 * @brief 런타임 UI 성능 벤치 화면입니다(`-gv_stressTestUiWidgets=N -gv_stressTestUiChurn=M`). 위젯 N 개짜리 격자를 만들고 프레임마다 M 개의 텍스트를 바꿉니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

SW_EXTERN_GLOBAL_VARIABLE( int32, gv_stressTestUiWidgets );
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_stressTestUiChurn );
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_stressTestUiOpacityPulse );

namespace sw
{
    /**
     * @class UIStressTestScreen
     * @brief 스크롤 패널 안에 줄 바꿈 격자를 두는 벤치 화면입니다. 칸마다 테두리(바탕), 아이콘 상자, 텍스트를 놓고 크기를 고정합니다(칸이 레이아웃 경계). 대부분은 스크롤 밖이라 잘립니다.
     * @details `onTick` 이 프레임마다 앞쪽(보이는) 칸 `gv_stressTestUiChurn` 개의 텍스트를 바꾼다. 무효화와 그리기 캐시가 비용을 그 칸으로 가두는지 측정한다.
     *          측정할 구간은 `GT.UI.*`, `RT.Canvas.*` 이고, 카운터는 `UI.LayoutWidgets`, `UI.PaintWidgets`, `Text.GlyphsRasterized` 다(값은 per_frame 열에 있다).
     *          `gv_stressTestUiOpacityPulse` 는 프레임마다 화면 루트의 불투명도를 바꿔 조상의 `kTransform` 으로 보이는 자손 전체를 다시 칠하게 한다(그림 캐시를 캐시 밖에서 곱하는 구조가
     *          이기는지 판단하는 측정용이며, 방법과 기준은 `UI/README.md` "성능" 의 "측정 시나리오" 에 있다).
     *          .cpp 의 화면 스위치(`SW_UI_SCREEN_SWITCH`)가 `gv_stressTestUiWidgets` 를 보고 열고 닫는다(HUD 레이어이며 포커스와 입력을 받지 않는다).
     */
    class SW_API UIStressTestScreen final : public UIScreen
    {
    public:
        static constexpr float32 kCellWidth    = 140.0f; ///< 칸 하나의 크기입니다(UI 단위).
        static constexpr float32 kCellHeight   = 44.0f;
        static constexpr uint32  kChurnWindow  = 128;   ///< 텍스트를 바꾸는 칸은 앞쪽 이만큼 안에서 돕니다(보이는 칸이며, 칠하기까지 측정합니다).
        static constexpr float32 kPulseOpacity = 0.95f; ///< `gv_stressTestUiOpacityPulse` 가 번갈아 쓰는 루트 불투명도입니다(1 과 이 값).

        /** @brief 칸 @p cellCount 개의 벤치 화면을 만듭니다. */
        static unique_ptr<UIStressTestScreen> create( uint32 cellCount );

        UIStressTestScreen( const UIScreenDesc& desc, unique_ptr<Widget> root, vector<WidgetID> listCellText );

        void   onTick( float32 deltaSeconds ) override;
        uint32 getCellCount() const { return static_cast<uint32>( _listCellText.size() ); }

    private:
        vector<WidgetID> _listCellText; ///< 칸마다 텍스트 위젯 번호입니다(칸 순서).
        uint32           _churnCursor;  ///< 다음에 바꿀 칸입니다.
        uint32           _frameIndex;   ///< 바꾼 텍스트에 넣는 프레임 번호입니다.
    };
} // namespace sw
