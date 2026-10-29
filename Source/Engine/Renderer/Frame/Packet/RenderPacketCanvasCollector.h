/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketCanvasCollector.h
 * @brief 패킷의 화면 2D(캔버스)를 채웁니다(게임 스레드). 런타임 UI 가 칠한 목록 · 화면 마커, 월드 UI · 오프스크린 화면의 렌더 텍스처 대상, 디버그 드로우 겹치기, 개발 테스트 그림, 글리프 아틀라스 구간, 색각 보정입니다.
 * @details `RenderPacketBuilder` 가 값으로 소유합니다. 언리얼 `FSlateApplication` 이 그린 요소 목록을 `FSlateRHIRenderer` 로 넘기는 자리에 해당합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"

namespace sw
{
    struct RenderFramePacket;

    class FontService;
    class RHI;
    class UISystem;

    /**
     * @class RenderPacketCanvasCollector
     * @brief 패킷의 캔버스 채우기와 그 재사용 버퍼(디버그 드로우 선분)입니다.
     */
    class SW_API RenderPacketCanvasCollector
    {
    public:
        RenderPacketCanvasCollector();
        ~RenderPacketCanvasCollector();

        RenderPacketCanvasCollector( const RenderPacketCanvasCollector& )            = delete;
        RenderPacketCanvasCollector& operator=( const RenderPacketCanvasCollector& ) = delete;

        /**
         * @brief 화면 2D(런타임 UI 의 캔버스, 월드 UI 캔버스, 디버그 드로우 겹치기, 개발 테스트 이미지), 글리프 아틀라스 구간, 색각 보정을 싣습니다.
         * @param bSceneMain 씬 뷰가 주 출력인 프레임이면 화면 UI 를 싣지 않습니다. UI 는 게임 화면(게임 뷰, 백버퍼)에만 있습니다.
         */
        void populate( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontService* pFontService, const RHI* pRHI );

    private:
        /** @brief 디버그 드로우 겹치기가 펼친 선분입니다(`DebugDrawCanvasUtil::paint`). 게임 스레드 하나가 패킷을 채우므로 하나를 프레임마다 재사용합니다. */
        vector<DebugLine> _listDebugDrawLine;
    };
} // namespace sw
