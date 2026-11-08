/**
 * @file VFXParticle.h
 * @brief 입자 하나의 렌더 입력 형식 `GPUParticle` 입니다. CPU 시뮬레이션과 GPU 시뮬레이션이 같은 형식을 냅니다.
 * @details float4 셋(48 바이트)이라 구조 버퍼의 한 원소가 됩니다. 좌표는 방출기 로컬입니다(월드 변환은 그리는 쪽이 방출기 행렬로 합니다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct VFXEmitterDef;
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
     * @struct VFXEmitterUserValues
     * @brief 사용자 파라미터(`VFXUserParameterDef`)를 방출기의 묶음으로 푼 값입니다. 커브가 낸 색 · 크기에 곱합니다. CPU · GPU 시뮬레이션이 같은 식을 씁니다.
     * @details 시뮬레이션 상태가 아니라 렌더 출력에 곱하므로 값을 바꿔도 일정과 난수가 그대로다. 재생 중에 바꾸면 살아 있는 입자까지 바로 따른다.
     */
    struct VFXEmitterUserValues
    {
        float4  _tint{ 1.0f, 1.0f, 1.0f, 1.0f }; ///< 입자 색에 곱한다
        float32 _sizeScale{ 1.0f };              ///< 입자 크기에 곱한다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VFXGPURunToken
     * @brief GPU 방출기 재생 한 번의 정체성입니다(칸이 없다 — 주소가 정체성이다). 방출기가 재생마다 새로 만들고 요청에 실어 보낸다.
     * @details 렌더 스레드(`VFXGPUSimulation`)는 이 주소를 GPU 상태 열쇠로 쓰고 상태가 사는 동안 토큰을 들어, 주소가 다른 재생에 다시 쓰이지 않는다.
     *          프로세스 번호 발급기(정적 원자 칸) 대신이다 — 언리얼 `FRenderResource` 가 GPU 자원을 객체 수명에 묶는 것과 같은 자리.
     */
    struct VFXGPURunToken
    {
    };

    /**
     * @struct VFXGPUEmitterRequest
     * @brief GPU 시뮬레이션 방출기 하나가 렌더 스레드에 싣는 요청입니다(`VFXGPUEmitter::makeRequest` → `GPUParticleBatch` → `VFXGPUSimulation`).
     * @details 입자는 싣지 않는다 — 렌더 스레드가 같은 시계(`VFXEmitterClock`)로 일정을 다시 만들어 `_stepCount` 까지 돌린다. 단계 수가 누적값이라
     *          렌더 스레드가 패킷을 건너뛰어도 남은 단계를 다음 프레임에 돌아 같은 결과에 닿는다.
     */
    struct VFXGPUEmitterRequest
    {
        shared_ptr<const VFXEmitterDef>  _pDef;               ///< 방출기 데이터(공유 — 게임 스레드가 놓아도 렌더 스레드가 읽는다)
        shared_ptr<const VFXGPURunToken> _pRunToken;          ///< 재생 한 번의 정체성. 다시 재생하면 새 토큰이다(렌더 스레드가 GPU 상태를 새로 만든다)
        uint64                           _seed{ 0 };          ///< 방출기 씨앗(`VFXCPUSimulation` 과 같은 값)
        uint64                           _stepCount{ 0 };     ///< 이 재생에서 지금까지 돈 고정 단계 수(누적)
        float32                          _spawnScale{ 0.0f }; ///< 게임 스레드 시계의 생성 배율(`VFXEmitterClock::getSpawnScale`) — 렌더 스레드 시계가 같은 일정을 얻는다
        VFXEmitterUserValues             _userValues{};       ///< 이번 프레임의 사용자 값(렌더 출력에 곱한다)
    };
} // namespace sw
