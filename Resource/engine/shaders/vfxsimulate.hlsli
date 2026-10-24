/**
 * vfxsimulate.hlsli — GPU 입자 시뮬레이션(vfxsimulate.hlsl · vfxmeshinstance.hlsl)과 C++ `VfxGPUSimulation` 이 함께 읽는 계약이다. 숫자 #define 만 둔다(C++ 가 그대로 include 한다).
 *
 * 매개변수 버퍼(StructuredBuffer<uint4> g_VfxParams, t0)에 방출기마다 한 구간이 이어 붙고, 루트 상수 g_VfxParamBase 가 그 첫 행이다. 실수는 비트 그대로 담는다(asfloat).
 * 행 번호는 구간 첫 행 기준이다. 해시 상수(splitmix64)와 충돌의 띄움 거리도 C++ 가 올린다 — 셰이더에 같은 값을 다시 적지 않는다.
 * 행렬은 행벡터 규약의 행 넷(C++ float4x4 의 _11.._14 가 첫 행)이다 — 점은 x·행0 + y·행1 + z·행2 + 행3.
 * 간접 인자 버퍼(g_VfxArgsRW, u2)는 32 비트 단어 SW_VFX_ARGS_WORD_COUNT 개다 — [0..3] RHIDrawIndirectCommand, 그 뒤 살아 있는 수 · 지금 반쪽(다음 디스패치가 읽는다) ·
 * 풀이 차서 버린 입자의 누적 수.
 * 파티클 라이트(VfxLightDef)는 라이트 버퍼(g_VfxLightsRW, u3 — 씬 라이트 목록과 같은 버퍼)의 GPU 구간에 쓴다. 방출기가 받은 슬롯 범위는 SW_VFX_PARAM_ROW_LIGHT 행이 알려 주고,
 * 슬롯 하나가 살아 있는 입자 하나다(앞의 슬롯 수 개 — CPU 방출기와 같은 규칙). 입자가 모자란 슬롯은 반경 0 의 죽은 점광으로 덮어쓴다(그리드에 들지 않는다).
 */

#ifndef SW_ENGINE_VFXSIMULATE_HLSLI
#define SW_ENGINE_VFXSIMULATE_HLSLI

// 그룹 하나 = 방출기 하나. 스레드가 입자를 이만큼씩 나눠 돈다(풀 크기에 상한이 없다 — 덩어리를 차례로 돈다).
#define SW_VFX_SIMULATE_THREAD_COUNT 256u
// 메시 인스턴스 쓰기(vfxmeshinstance.hlsl)의 그룹 크기 — 풀 자리 하나가 스레드 하나다.
#define SW_VFX_MESH_INSTANCE_THREAD_COUNT 64u
// 입자 상태 원소(VfxParticleState)의 float4 수 — 위치와 나이, 속도와 수명, 입자 번호(uint2)와 빈칸.
#define SW_VFX_STATE_FLOAT4_COUNT 3u
// 디스패치 하나가 도는 단계의 상한. 렌더 스레드가 늦어 더 밀리면 남은 단계는 다음 프레임에 돈다.
#define SW_VFX_MAX_STEPS_PER_DISPATCH 32u

#define SW_VFX_PARAM_ROW_HEADER                  0u  // (풀 크기, 단계 수, 플래그 SW_VFX_FLAG_*, 입자 하나의 정점 수)
#define SW_VFX_PARAM_ROW_SEED                    1u  // (방출기 씨앗의 mix64 lo, hi, 황금비 lo, hi)
#define SW_VFX_PARAM_ROW_MIXER                   2u  // (splitmix64 첫 곱수 lo, hi, 둘째 곱수 lo, hi)
#define SW_VFX_PARAM_ROW_VELOCITY_MIN            3u  // (초기 속도 최소 xyz, 수명 최소)
#define SW_VFX_PARAM_ROW_VELOCITY_MAX            4u  // (초기 속도 최대 xyz, 수명 최대)
#define SW_VFX_PARAM_ROW_GRAVITY                 5u  // (중력 xyz, 단계 하나의 시간)
#define SW_VFX_PARAM_ROW_CURVE_SIZE_RED          6u  // (크기 커브의 첫 행, 키 수, 빨강 커브의 첫 행, 키 수)
#define SW_VFX_PARAM_ROW_CURVE_GREEN_BLUE        7u  // (초록 첫 행, 키 수, 파랑 첫 행, 키 수)
#define SW_VFX_PARAM_ROW_CURVE_ALPHA             8u  // (알파 첫 행, 키 수, 0, 0)
#define SW_VFX_PARAM_ROW_COLLISION               9u  // (반발 계수, 마찰, 띄움 거리, 깊이 두께) — 실수
#define SW_VFX_PARAM_ROW_DEPTH_INFO              10u // (깊이 히스토리 폭, 높이, RW 텍스처 번호 — 네이티브는 bindless UAV, 에뮬은 서수 0, 0)
#define SW_VFX_PARAM_ROW_DEPTH_EYE               11u // (히스토리 시점의 눈 위치 xyz, 0)
#define SW_VFX_PARAM_ROW_WORLD                   12u // 방출기 행렬 행 넷(12..15)
#define SW_VFX_PARAM_ROW_INVERSE_WORLD           16u // 방출기 행렬의 역 행 넷(16..19)
#define SW_VFX_PARAM_ROW_DEPTH_VIEW_PROJ         20u // 히스토리 시점의 뷰-투영 행 넷(20..23)
#define SW_VFX_PARAM_ROW_DEPTH_INVERSE_VIEW_PROJ 24u // 그 역 행 넷(24..27)
#define SW_VFX_PARAM_ROW_LIGHT                   28u // 파티클 라이트 (라이트 버퍼 안의 첫 슬롯 번호, 슬롯 수 — 0 이면 빛 없음, 월드 반경 실수, 세기 실수)
#define SW_VFX_PARAM_ROW_FIRST_STEP              29u // 단계마다 한 행 (만들 입자 수, 첫 입자 번호 lo, hi, 0) — 커브 키는 그 뒤
// 커브 키 하나의 행 수 — (시각, 값, 들어오는 접선, 나가는 접선), (상수면 1, 직선이면 1, 0, 0). 둘 다 0 이면 에르미트다(FloatCurve 와 같은 식).
#define SW_VFX_PARAM_ROWS_PER_KEY 2u

#define SW_VFX_FLAG_RESET          1u  // 이번 디스패치 전에 입자를 모두 지운다(재생 시작)
#define SW_VFX_FLAG_RIBBON         2u  // 띠 — 그릴 인스턴스는 이웃 입자 둘이 한 마디라 살아 있는 수보다 하나 적다
#define SW_VFX_FLAG_COLLIDE_BOUNCE 4u  // 깊이 충돌 — 닿으면 튄다
#define SW_VFX_FLAG_COLLIDE_KILL   8u  // 깊이 충돌 — 닿으면 사라진다
#define SW_VFX_FLAG_DEPTH_READY    16u // 이번 프레임에 깊이 히스토리가 있다(없으면 충돌을 보지 않는다 — 첫 프레임 · 크기가 바뀐 프레임)

// 메시 인스턴스 쓰기(vfxmeshinstance.hlsl)의 구간 — 같은 매개변수 버퍼의 다른 구간이고, 루트 상수 g_VfxMeshParamBase 가 첫 행이다.
#define SW_VFX_MESH_ROW_HEADER     0u // (풀 크기, 0, 메시 반지름, 방출기 최대 축 배율) — 뒤 둘은 실수
#define SW_VFX_MESH_ROW_WORLD      1u // 방출기 행렬 행 넷(1..4)
#define SW_VFX_MESH_ROW_FIRST_SLOT 5u // 풀 자리마다 인스턴스 칸(행 하나에 넷, SW_VFX_INVALID_SLOT 이면 쓰지 않는다)
#define SW_VFX_INVALID_SLOT        0xFFFFFFFFu

#define SW_VFX_ARGS_WORD_ALIVE   4u
#define SW_VFX_ARGS_WORD_HALF    5u
#define SW_VFX_ARGS_WORD_DROPPED 6u
#define SW_VFX_ARGS_WORD_COUNT   8u

#endif // SW_ENGINE_VFXSIMULATE_HLSLI
