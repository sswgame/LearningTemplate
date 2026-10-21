/**
 * @file VfxParticle.h
 * @brief 입자 하나의 렌더 입력 형식 `GPUParticle` 입니다. CPU 시뮬레이션과 GPU 시뮬레이션이 같은 형식을 냅니다.
 * @details float4 셋(48 바이트)이라 구조 버퍼의 한 원소가 됩니다. 좌표는 방출기 로컬입니다(월드 변환은 그리는 쪽이 방출기 행렬로 합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct VfxEmitterDef;
} // namespace sw

namespace sw
{
    /** @brief 입자 하나의 렌더 입력입니다. 필드 순서가 셰이더 쪽 구조체(`particle.hlsl` 의 `GPUParticle`)와 같습니다. */
    struct GPUParticle
    {
        /** @brief 입자 하나(띠는 마디 하나)의 정점 수입니다 — particle.hlsl 이 SV_VertexID 로 모서리 여섯을 만든다. CPU 드로우와 GPU 간접 인자가 같은 값을 쓴다. */
        static constexpr uint32 kVertexCount = 6;

        float4 _positionSize{}; ///< xyz = 방출기 로컬 위치, w = 크기
        float4 _color{};        ///< 선형 RGBA
        float4 _rotationLife{}; ///< x = 회전(라디안), y = 수명 비율(0 = 갓 태어남, 1 = 사라짐), zw = 비어 있음
    };

    static_assert( sizeof( GPUParticle ) == sizeof( float4 ) * 3, "GPUParticle must be exactly three float4 (structured buffer element)" );
} // namespace sw

namespace sw
{
    /**
     * @struct VfxGPUEmitterRequest
     * @brief GPU 시뮬레이션 방출기 하나가 렌더 스레드에 싣는 요청입니다(`VfxGPUEmitter::makeRequest` → `GPUParticleBatch` → `VfxGPUSimulation`).
     * @details 입자는 싣지 않는다 — 렌더 스레드가 같은 시계(`VfxEmitterClock`)로 일정을 다시 만들어 `_stepCount` 까지 돌린다. 단계 수가 누적값이라
     *          렌더 스레드가 패킷을 건너뛰어도 남은 단계를 다음 프레임에 돌아 같은 결과에 닿는다.
     */
    struct VfxGPUEmitterRequest
    {
        shared_ptr<const VfxEmitterDef> _pDef;           ///< 방출기 데이터(공유 — 게임 스레드가 놓아도 렌더 스레드가 읽는다)
        uint64                          _runID{ 0 };     ///< 재생 한 번의 정체성. 다시 재생하면 새 번호다(렌더 스레드가 GPU 상태를 새로 만든다)
        uint64                          _seed{ 0 };      ///< 방출기 씨앗(`VfxCPUSimulation` 과 같은 값)
        uint64                          _stepCount{ 0 }; ///< 이 재생에서 지금까지 돈 고정 단계 수(누적)
    };
} // namespace sw
