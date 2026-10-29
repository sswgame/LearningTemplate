/**
 * @file RHIWaitIdleReason.h
 * @brief `IRHIDevice::waitIdle` · `RenderThread::waitIdle` 를 부르는 이유입니다.
 * @details 호출이 GPU 와 렌더 스레드를 통째로 기다리므로 프레임 경로에 새로 생기면 히치가 된다. 이유를 필수 인자로 두면 호출처가 "왜 기다리는가" 를 이름으로 적어야 하고,
 *          프레임 경로 폴더(`Renderer/Frame/`, `Graphics/Material/`, `Graphics/Texture/` 의 핫 리로드가 아닌 함수)에서 `Tool` 이 아닌 이유의 호출을 찾아내기 쉽다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum RHIWaitIdleReason
     * @brief 장치 · 렌더 스레드를 비우는 이유입니다(리플렉션 열거형 — 이름은 `getEnumName` 이 준다). 프레임마다 도는 경로에는 어느 이유도 맞지 않는다 — 맞는 이유가 없으면 대기를 쓰지 않고 지연 해제 · 티켓 폴링으로 푼다.
     */
    ENUM()
    enum class RHIWaitIdleReason : uint8
    {
        Shutdown = 0,    ///< 엔진 · 서브시스템 종료. 모든 GPU 일을 끝내고 자원을 거둔다
        BackendSwap,     ///< 백엔드 교체(옛 디바이스를 내리기 전)
        Resize,          ///< 창 · 스왑체인 크기 변경(렌더 스레드가 스왑체인을 건드리지 않게 비운다)
        HotReload,       ///< 에셋 · 셰이더 · 모듈 핫 리로드(도구 경로). 이전 프레임이 옛 자원을 읽는 것이 끝나야 한다
        SceneActivation, ///< 씬 전환 — 이전 씬의 GPU 일이 끝난 뒤에 새 씬의 기본 자원을 잡는다
        Capture,         ///< 초상 · 반사 프로브 베이크 같은 캡처가 한 번 GPU 결과를 기다린다(프레임 경로 밖)
        Tool,            ///< 에디터 · 테스트 · 개발 토글 같은 도구 경로(프레임 경로가 아니다)
        PipelineSwitch,  ///< 렌더 파이프라인을 실행 중에 바꾼다(`gv_renderPipeline` · `gv_deferredShading`) — 옛 파이프라인의 패스 자원을 거두기 전
    };
} // namespace sw
