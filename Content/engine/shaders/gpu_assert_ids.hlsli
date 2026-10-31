/**
 * gpu_assert_ids.hlsli — 셰이더 assert 계약의 **유일한 정본**(언리얼 ShaderPrint / ShaderDebug 의 조건 번호표와 같은 자리).
 *
 * 이 파일은 순수 전처리기 정의만 담는다(#define 정수 + 주석). 그래서 HLSL 과 C++ 가 **같은 파일을 include** 한다 —
 * C++ 쪽은 Source/Engine/Graphics/Debug/GPUAssert.h 가 이 파일을 include 해 id 별 심각도 · 메시지 표(GPUAssert.cpp)를 만든다.
 * 규칙은 binding_slots.hlsli 와 같다: #define 과 주석만, 값은 숫자 리터럴(정수 · `u` 접미)만.
 *
 * 버퍼 배치(uint 원소): [0] 지금까지 적은 기록 수(누적 · 지우지 않는다) · [1..3] 예약 · 이어서 기록 SW_GPU_ASSERT_RING_CAPACITY 칸(한 칸 SW_GPU_ASSERT_RECORD_UINTS 개).
 * 기록 칸: [0] 조건 id · [1] 스레드 식별 · [2] 값 비트 · [3] 순번 + 1(0 = 빈 칸 — 마지막에 쓴다). 순번 s 의 칸은 s % 용량 이다 — 링이 한 바퀴 돌면 오래된 기록이 덮이고,
 * CPU 가 지난번 읽은 누적 수와의 차에서 덮인 수를 센다(버려진 수만 센다).
 */

#ifndef SW_ENGINE_GPUASSERTIDS_HLSLI
#define SW_ENGINE_GPUASSERTIDS_HLSLI

// 버퍼 배치
#define SW_GPU_ASSERT_RING_CAPACITY 256
#define SW_GPU_ASSERT_HEADER_UINTS  4
#define SW_GPU_ASSERT_RECORD_UINTS  4
#define SW_GPU_ASSERT_BUFFER_UINTS  1028 // 헤더 4 + 256 칸 x 4 — 위 세 값의 식이다(산술식 금지라 값으로 적고 GPUAssert.h 가 static_assert 로 확인한다)

// 컴퓨트 쓰기 슬롯 — SW_SLOT_COMPUTE_ASSERT_UAV(binding_slots.hlsli 3 절, u4). 셰이더 소유 쓰기 버퍼 u0..u3 와 겹치지 않으므로 어느 컴퓨트 셰이더(vfx_simulate 포함)든 assert 를 가질 수 있다.
#include "binding_slots.hlsli"

// 조건 id — 값은 기록 칸에 비트로 싣고, 값의 종류(정수 · 실수)와 심각도 · 메시지는 id 표(GPUAssert.cpp)가 정한다.
// ------------------------------------------------------------------------------
// gpu_cull.hlsl — LOD 단계 번호 · 가시 구간 범위
// ------------------------------------------------------------------------------
#define SW_GPU_ASSERT_ID_CULL_LOD_LEVEL      1u // 고른 LOD 단계가 머리 배치의 단계 수 이상이다(값 = 단계)
#define SW_GPU_ASSERT_ID_CULL_LOD_PREVIOUS   2u // 직전 LOD 단계가 단계 수 이상이다(값 = 직전 단계)
#define SW_GPU_ASSERT_ID_CULL_LOD_LEVEL_BITS 3u // LOD 단계가 가시 항목의 3 비트 칸(0 ~ 7)에 들어가지 않는다(값 = 단계)
#define SW_GPU_ASSERT_ID_CULL_FADE_LEVEL     4u // 크로스페이드 진행이 4 비트 칸(0 ~ 15)에 들어가지 않는다(값 = 진행)
#define SW_GPU_ASSERT_ID_CULL_BATCH_RANGE    5u // 가시 목록에 적을 배치 번호(머리 + 단계)가 배치 표 밖이다(값 = 배치 번호)
#define SW_GPU_ASSERT_ID_CULL_VISIBLE_RANGE  6u // 가시 목록의 쓰기 자리가 가시 칸 수 이상이다(값 = 자리) — 목록이 작아 항목을 버렸다
#define SW_GPU_ASSERT_ID_CULL_INSTANCE_ID    7u // 인스턴스 번호가 가시 항목의 24 비트 칸에 들어가지 않는다(값 = 번호)

// ------------------------------------------------------------------------------
// canvas_marker.hlsl — 투영 NaN · 인스턴스 칸 범위
// ------------------------------------------------------------------------------
#define SW_GPU_ASSERT_ID_MARKER_CLIP_NAN       20u // 마커 월드 점의 투영 클립 좌표가 NaN 또는 무한이다(값 = 클립 w)
#define SW_GPU_ASSERT_ID_MARKER_INSTANCE_RANGE 21u // 마커의 인스턴스 칸이 GPUScene 인스턴스 수 이상이다(값 = 칸)
#define SW_GPU_ASSERT_ID_MARKER_ROW_RANGE      22u // 마커의 입력 행이 행 버퍼 밖이다(값 = 행 번호)
#define SW_GPU_ASSERT_ID_MARKER_WORLD_NAN      23u // 인스턴스 변환에서 읽은 마커 월드 점이 NaN 또는 무한이다(값 = x)

// ------------------------------------------------------------------------------
// mesh_morph.hlsl · mesh_modifier.hlsli — 프로그램 표 · 삼각형 표 · BVH 스택 범위
// ------------------------------------------------------------------------------
#define SW_GPU_ASSERT_ID_MORPH_VERTEX_RANGE      40u // 레스트 정점 원소가 레스트 버퍼 밖이다(값 = 원소)
#define SW_GPU_ASSERT_ID_MORPH_PROGRAM_SLOT      41u // 변형 프로그램 칸 번호가 프로그램 표 밖이다(값 = 칸)
#define SW_GPU_ASSERT_ID_MODIFIER_PROGRAM_RANGE  42u // 프로그램의 시작 원소가 프로그램 표 밖이다(값 = 시작 원소)
#define SW_GPU_ASSERT_ID_MODIFIER_RECORD_RANGE   43u // 모디파이어 기록의 위치가 프로그램 표 밖이다(값 = 위치)
#define SW_GPU_ASSERT_ID_MODIFIER_RAW_RANGE      44u // 프로그램 표를 읽은 원소가 표 밖이다(값 = 원소)
#define SW_GPU_ASSERT_ID_MODIFIER_NODE_RANGE     45u // BVH 노드 번호가 노드 수 이상이다(값 = 노드 번호)
#define SW_GPU_ASSERT_ID_MODIFIER_TRIANGLE_RANGE 46u // BVH 잎의 삼각형 번호가 삼각형 표 밖이다(값 = 삼각형 시작 원소)
#define SW_GPU_ASSERT_ID_MODIFIER_STACK_OVERFLOW 47u // BVH 스택이 칸 수를 넘으려 한다(값 = 쌓인 수)
#define SW_GPU_ASSERT_ID_MODIFIER_UV_TABLE_RANGE 48u // 정점 UV 표의 원소가 프로그램 표 밖이다(값 = 원소)

#endif // SW_ENGINE_GPUASSERTIDS_HLSLI
