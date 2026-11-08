/**
 * gpu_message_ids.hlsli — 셰이더 메시지 채널(SW_GPU_PRINT · SW_GPU_LOG · SW_GPU_ASSERT) 계약의 **유일한 정본**(언리얼 ShaderPrint 의 버퍼 배치 · 조건 번호표와 같은 자리).
 *
 * 이 파일은 순수 전처리기 정의만 담는다(#define 정수 + 주석). 그래서 HLSL 과 C++ 가 **같은 파일을 include** 한다 —
 * C++ 쪽은 Source/Engine/Graphics/Debug/GPUMessage.h 가 이 파일을 include 해 id 별 서식 표(GPUMessage.cpp · SW_GPU_MESSAGE_FORMAT)를 만든다.
 * 규칙은 binding_slots.hlsli 와 같다: #define 과 주석만, 값은 숫자 리터럴(정수 · `u` 접미)만. GPU 에는 문자열이 없다 — 서식은 CPU 표가 id 로 푼다.
 *
 * 버퍼 배치(uint 원소):
 *   머리 SW_GPU_MESSAGE_HEADER_UINTS 칸 — [0] 지금까지 적은 기록 수(누적 · GPU 만 쓴다 · 지우지 않는다) · [1] 고른 픽셀 x · [2] 고른 픽셀 y · [3] 고른 스레드 ·
 *     [4] SW_GPU_LOG 의 id 당 프레임 예산 · [5..7] 예약. [1..4] 는 CPU 가 프레임마다 올린다(GPUMessageChannel::updateControl).
 *   예산 칸 SW_GPU_MESSAGE_BUDGET_SLOTS 칸 — SW_GPU_LOG 가 id % 칸 수 자리를 원자적으로 올려 예산을 넘으면 적지 않는다. CPU 가 프레임마다 0 으로 올린다.
 *   기록 SW_GPU_MESSAGE_RING_CAPACITY 칸(한 칸 SW_GPU_MESSAGE_RECORD_UINTS 개) — [0] 메시지 id · [1] 픽셀 x 또는 스레드 · [2] 픽셀 y(스레드면 NO_COORD) ·
 *     [3..6] 인자 넷(비트 — 종류는 CPU 서식 표가 정한다) · [7] 순번 + 1(0 = 빈 칸 — 마지막에 쓴다).
 *   순번 s 의 칸은 s % 용량 이다 — 링이 한 바퀴 돌면 오래된 기록이 덮이고, CPU 가 지난번 읽은 누적 수와의 차에서 덮인 수를 센다(버려진 수만 센다).
 */

#ifndef SW_ENGINE_GPUMESSAGEIDS_HLSLI
#define SW_ENGINE_GPUMESSAGEIDS_HLSLI

// 버퍼 배치
#define SW_GPU_MESSAGE_RING_CAPACITY 512
#define SW_GPU_MESSAGE_HEADER_UINTS  8
#define SW_GPU_MESSAGE_BUDGET_SLOTS  64
#define SW_GPU_MESSAGE_RECORD_UINTS  8
#define SW_GPU_MESSAGE_ARG_COUNT     4
#define SW_GPU_MESSAGE_CONTROL_UINTS 71   // 머리 [1..7] 7 칸 + 예산 64 칸 — CPU 가 프레임마다 올리는 구간(머리 [0] 카운터는 건드리지 않는다)
#define SW_GPU_MESSAGE_BUFFER_UINTS  4168 // 머리 8 + 예산 64 + 512 칸 x 8 — 위 값들의 식이다(산술식 금지라 값으로 적고 GPUMessage.h 가 static_assert 로 확인한다)

// 머리 칸 번호
#define SW_GPU_MESSAGE_HEADER_COUNTER       0
#define SW_GPU_MESSAGE_HEADER_SELECT_X      1
#define SW_GPU_MESSAGE_HEADER_SELECT_Y      2
#define SW_GPU_MESSAGE_HEADER_SELECT_THREAD 3
#define SW_GPU_MESSAGE_HEADER_LOG_BUDGET    4

// 기록 칸 번호
#define SW_GPU_MESSAGE_RECORD_ID       0
#define SW_GPU_MESSAGE_RECORD_X        1
#define SW_GPU_MESSAGE_RECORD_Y        2
#define SW_GPU_MESSAGE_RECORD_ARG0     3
#define SW_GPU_MESSAGE_RECORD_SEQUENCE 7

// 좌표가 없다는 값 — 스레드 기록의 y, 아무것도 고르지 않은 선택 칸.
#define SW_GPU_MESSAGE_NO_COORD 4294967295u

// 쓰기 슬롯 — SW_SLOT_GPU_MESSAGE_UAV(binding_slots.hlsli 3 절, u4). 컴퓨트 · 그래픽스 단계가 같은 번호를 쓴다. 셰이더 소유 쓰기 버퍼 u0..u3 와 겹치지 않으므로 어느 컴퓨트 셰이더든
// 메시지를 가질 수 있다. 픽셀 셰이더는 DX11 에서 u 번호가 렌더 타깃 칸과 같은 자리라 렌더 타깃이 다섯 장 이상인 패스에서는 걸지 못한다(GPUMessageChannel::bindGraphics 가 그 패스를 건너뛴다).
#include "binding_slots.hlsli"

// 메시지 id — 값은 기록 칸에 인자 비트로 싣고, 인자의 종류(정수 · 실수)와 심각도 · 서식은 id 표(GPUMessage.cpp · SW_GPU_MESSAGE_FORMAT)가 정한다.
// assert 는 SW_GPU_ASSERT_ID_*, 출력(SW_GPU_PRINT · SW_GPU_LOG)은 SW_GPU_MESSAGE_ID_* 다 — 번호 공간은 하나다(겹치면 C++ 표 등록이 거절한다).
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

// ------------------------------------------------------------------------------
// 출력 메시지(SW_GPU_PRINT — 고른 픽셀 · 스레드만, SW_GPU_LOG — id 당 프레임 예산까지)
// ------------------------------------------------------------------------------
#define SW_GPU_MESSAGE_ID_PRESENT_PIXEL 1000u // fullscreen_blit.hlsl — 고른 픽셀이 화면에 내는 색(인자 = r · g · b, 넷째 없음). `gpu.inspect x y` 가 고른다

#endif // SW_ENGINE_GPUMESSAGEIDS_HLSLI
