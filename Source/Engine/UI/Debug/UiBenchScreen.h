/**
 * @file UiBenchScreen.h
 * @brief 런타임 UI 성능 벤치 화면입니다(`-gv_benchUiWidgets=N -gv_benchUiChurn=M`) — 위젯 N 칸 격자와 프레임마다 M 칸의 글 바꿈.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Core/WidgetTypes.h"
#include "Engine/UI/Screen/UiScreen.h"

SW_EXTERN_GLOBAL_VARIABLE( int32, gv_benchUiWidgets );
SW_EXTERN_GLOBAL_VARIABLE( int32, gv_benchUiChurn );

namespace sw
{
    /**
     * @class UiBenchScreen
     * @brief 스크롤 패널 안 줄 바꿈 격자 — 칸마다 테두리(바탕) + 아이콘 상자 + 글, 고정 크기(칸이 레이아웃 경계). 대부분은 스크롤 밖(잘린다).
     * @details `onTick` 이 프레임마다 앞쪽(보이는) 칸 `gv_benchUiChurn` 개의 글을 바꾼다 — 무효화 · 그림 캐시가 비용을 그 칸으로 가두는지 잰다.
     *          잴 구간은 `GT.Ui.*` · `RT.Canvas.*`, 카운터 `Ui.LayoutWidgets` · `Ui.PaintWidgets` · `Text.GlyphsRasterized`(값은 per_frame 열).
     *          `UiSystem::update` 가 `gv_benchUiWidgets` 를 보고 열고 닫는다(Hud 층 — 포커스 · 입력을 받지 않는다).
     */
    class SW_API UiBenchScreen final : public UiScreen
    {
    public:
        static constexpr float32 kCellWidth   = 140.0f; ///< 칸 크기(UI 단위)
        static constexpr float32 kCellHeight  = 44.0f;
        static constexpr uint32  kChurnWindow = 128; ///< 글을 바꾸는 칸은 앞쪽 이만큼 안에서 돈다(보이는 칸 — 칠하기까지 잰다)

        /** @brief 칸 @p cellCount 개의 벤치 화면을 짓습니다. */
        static unique_ptr<UiBenchScreen> create( uint32 cellCount );

        UiBenchScreen( const UiScreenDesc& desc, unique_ptr<Widget> root, vector<WidgetId> listCellText );

        void   onTick( float32 deltaSeconds ) override;
        uint32 getCellCount() const { return static_cast<uint32>( _listCellText.size() ); }

    private:
        vector<WidgetId> _listCellText; ///< 칸마다 글 위젯 번호(칸 순서)
        uint32           _churnCursor;  ///< 다음에 바꿀 칸
        uint32           _frameIndex;   ///< 바꾼 글에 넣는 프레임 번호
    };
} // namespace sw
