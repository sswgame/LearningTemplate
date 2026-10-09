/**
 * @file PortraitRenderer.h
 * @brief 프리팹 하나를 **따로 떨어진 스튜디오**(자기 씬 · 자기 카메라 · 자기 조명 · 자기 렌더러)에서 그려 초상화(썸네일) 그림을 얻습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    class FrameRenderer;
    class IRHIDevice;

    /** @brief 초상화 한 장의 주문입니다 — 프리팹 · 크기 · 카메라 각 · 여백. */
    struct PortraitRequest
    {
        string  _prefabPath{};
        uint32  _width{ 256 };
        uint32  _height{ 256 };
        float32 _yaw{ 0.55f };         ///< 대상 정면(+Z)에서 +X 쪽으로 돈 카메라 각(rad)
        float32 _pitch{ 0.2f };        ///< 내려다보는 각(rad)
        float32 _fieldOfViewY{ 0.6f }; ///< 수직 시야각(rad)
        float32 _padding{ 1.15f };     ///< 경계 구 반지름에 곱하는 여백
        float4  _backgroundColor{ 0.10f, 0.11f, 0.13f, 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PortraitRenderer
     * @brief 프리팹을 스튜디오 씬에 스폰해 경계 구에 맞춘 카메라와 키 · 채움 조명으로 그리고, 화면에 나간 그림(RGBA8)을 돌려줍니다.
     * @details **격리 방식: 따로 만든 씬 + 따로 만든 렌더러.** 엔진 루프는 활성 씬 하나만 틱하고 그리므로, 스튜디오 씬은 씬 매니저에 등록하지 않는다 —
     *          틱하지 않고 플레이어 화면에 나오지 않으며, 조명은 스튜디오의 빛뿐이다. 렌더러도 자기 것(`FrameRenderer` 인스턴스 — GpuScene · 트랜지언트 ·
     *          PSO)이라 주 렌더러의 상태를 건드리지 않는다. 숨김 레이어(같은 씬에 두고 마스크로 가리기)는 고르지 않았다 — 조명 · 그림자 · 후처리가 씬
     *          전체의 것이라 스튜디오가 게임의 해 · 점광을 받고, 게임 화면이 스튜디오의 빛을 받는다. 다중 월드가 생기면 스튜디오가 미리보기 월드가 된다.
     *
     *          스레드: 그래픽스 컨텍스트를 쥔 스레드에서 부릅니다(렌더 스레드가 쉬는 동안의 게임 스레드 — `EngineLoop::renderPortraits`). 한 장마다 디바이스
     *          프레임을 몇 번 열고 닫으며 GPU 를 기다립니다(프레임 경로가 아니다).
     */
    class SW_API PortraitRenderer
    {
    public:
        PortraitRenderer();
        ~PortraitRenderer();

        PortraitRenderer( const PortraitRenderer& )            = delete;
        PortraitRenderer& operator=( const PortraitRenderer& ) = delete;

        /** @brief 이 디바이스로 스튜디오 렌더러(포워드 파이프라인)를 세웁니다. */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice );
        void               shutdown();

        /**
         * @brief 프리팹 한 장을 그려 @p outRgbaBytes(빈틈없는 RGBA8, 위 행부터)에 담습니다. 프리팹을 못 읽거나 그릴 메시가 없으면 false 입니다.
         * @details 카메라는 대상의 경계 구(메시마다 로컬 경계 상자를 월드로 옮긴 것을 모두 덮는 구)를 시야각에 맞춰 물러난다 — 대상 크기가 달라도 같은 화면 비율이다.
         */
        [[nodiscard]] bool renderPrefab( const PortraitRequest& request, vector<uint8>& outRgbaBytes );

    private:
        IRHIDevice*               _pDevice;
        unique_ptr<FrameRenderer> _renderer;
    };
} // namespace sw
