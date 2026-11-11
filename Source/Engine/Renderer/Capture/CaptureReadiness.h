/**
 * @file CaptureReadiness.h
 * @brief 지금 읽으면 다 그려진 그림인지 정하는 한 곳입니다 — 모든 캡처(시나리오 `<Screenshot>` · `-gv_screenshotSettledFrames` · 화면 밖 캡처 `SceneCapture`)가 이 판정만 씁니다.
 * @details 판정은 두 쪽의 남은 일입니다. 씬 쪽(게임 스레드): 읽거나 애니메이션을 기다리거나 디더로 나타나는 중인 메시, 메시를 기다리는 내비메시, 읽거나 올리는 중인 텍스처.
 *          렌더 쪽: 렌더러가 지난 프레임에 머티리얼 PSO 를 아직 만드는 중이라 빠뜨린 배치(`FrameRenderer::getLastPendingPSOBatchCount`).
 *          예전에는 입구마다 "몇 프레임 뒤면 다 그려졌겠지" 를 따로 어림했다(초상화 · 반사 프로브 3 장, 시작 인자 10 장, 시나리오는 씬 쪽만) — DX11 에서 PSO 를 만드는
 *          동안의 프레임을 초상화가 읽어 배경만 찍혔다. 언리얼 자동화 스크린샷 · 썸네일이 스트리밍과 셰이더 컴파일이 끝나기를 같은 규칙으로 기다리는 것과 같다.
 *          캡처를 미루는 화면 덮개(로딩 화면 · 페이드)는 자동화 층의 것이라 여기 없다(`AutomationCaptureUtil::isReadyForCapture` 가 더한다).
 */
#pragma once
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class FrameRenderer;
    class GameObjectManager;

    /** @brief 캡처 준비 판정입니다. 함수마다 부르는 스레드가 정해져 있다. */
    struct SW_API CaptureReadiness
    {
        /** @brief (게임 스레드) @p pManager 씬의 남은 일 수입니다 — 메시 대기 · 내비메시 대기 · 텍스처 읽기 · 올리기. @p pManager 가 nullptr 이면 텍스처만 센다. */
        [[nodiscard]] static uint32 countPendingSceneJobs( const GameObjectManager* pManager );
        /**
         * @brief (게임 스레드) 씬 키 @p sceneKey(0 = 주 화면, 미리보기 씬 키)를 그린 렌더러가 지난 프레임에 빠뜨린 배치 수입니다(`RenderThread::findPendingMaterialPSOBatchCount`).
         * @details 지난 프레임에 그 씬을 그리지 않았으면 1 이다(아직 그려진 그림이 없다). 렌더 스레드가 없는 호스트(전용 서버)는 0 이다.
         */
        [[nodiscard]] static uint32 countPendingRenderJobs( uint32 sceneKey );
        /** @brief (게임 스레드) 씬 @p pManager 를 @p sceneKey 로 찍는 캡처의 남은 일 수입니다 — 씬 쪽과 렌더 쪽의 합입니다(감시 값 `Assets.Pending` 은 주 화면). */
        [[nodiscard]] static uint32 countPendingJobs( const GameObjectManager* pManager, uint32 sceneKey = 0 )
        {
            // 전용 서버는 Renderer 폴더를 짓지 않고 찍을 그림도 없다.
            if constexpr ( build::kWithClientCode )
                return countPendingSceneJobs( pManager ) + countPendingRenderJobs( sceneKey );
            else
                return 0u;
        }
        /** @brief (렌더 스레드) @p renderer 가 지난 프레임을 빠짐없이 그렸는지입니다. 화면 밖 캡처는 자기 하위 렌더러로 묻는다. */
        [[nodiscard]] static bool isFrameComplete( const FrameRenderer& renderer );
    };
} // namespace sw
