/**
 * deferred_tile_class.hlsli — 디퍼드 조명 타일 분류의 표 한 벌(언리얼 Substrate 타일 분류 · 유니티 HDRP 디퍼드 타일 변형과 같은 자리).
 *
 * 분류 컴퓨트(deferred_tile_classify.hlsl)가 타일(한 변 SW_DEFERRED_TILE_SIZE px)마다 G버퍼 알베도 알파의 셰이딩 모델 번호를 비트로 모아 종류 하나를 고르고,
 * 디퍼드 조명(deferred_lighting.hlsl)이 종류마다 그 종류의 엽만 컴파일한 변형(SW_DEFERRED_TILE_CLASS)으로 타일 사각형을 간접 그리기 한다.
 *
 * 숫자 #define 만 둔다 — C++(RenderPassTypeInfo.h)가 그대로 include 해 종류 수 · 타일 크기를 같은 값으로 읽는다(shading_model_ids.hlsli 와 같은 길).
 * 셰이딩 모델을 더할 때: 기본은 COMPLEX(모든 엽)라 아무것도 고치지 않아도 맞게 그린다. 엽이 없는 번호면 SW_DEFERRED_TILE_SIMPLE_MODELS 한 줄에 더한다 — 이 파일 한 곳이다.
 * 종류를 더할 때: 번호 · 수와 deferred_lighting.hlsl 머리의 엽 고르기 한 갈래를 더한다(C++ 는 수만 읽는다).
 */

#ifndef SW_ENGINE_DEFERREDTILECLASS_HLSLI
#define SW_ENGINE_DEFERREDTILECLASS_HLSLI

#include "shading_model_ids.hlsli"

#define SW_DEFERRED_TILE_SIZE          8 // 타일 한 변(px) — 분류 컴퓨트의 스레드 그룹 한 변과 같다(그룹 하나 = 타일 하나)
#define SW_DEFERRED_TILE_CLASS_COMPLEX 0 // 모든 엽(코트 · 시엔 · 피부 · 투과 · 비등방) — 표에 없는 번호가 오는 안전한 기본
#define SW_DEFERRED_TILE_CLASS_SIMPLE  1 // 엽 없음 — 금속도 · 거칠기 PBR 과 Unlit 만 있는 타일
#define SW_DEFERRED_TILE_CLASS_COUNT   2
#define SW_DEFERRED_TILE_VERTEX_COUNT  6 // 타일 사각형 하나 = 삼각형 둘(정점 버퍼 없이 SV_VertexID)

// SIMPLE 종류가 받는 셰이딩 모델 번호의 비트 — 타일의 모든 번호가 이 안이면 SIMPLE, 하나라도 밖이면 COMPLEX 다(NONE 은 셈하지 않는다 — 그리지 않은 픽셀).
#define SW_DEFERRED_TILE_SIMPLE_MODELS ( ( 1u << SW_SHADING_MODEL_ID_UNLIT ) | ( 1u << SW_SHADING_MODEL_ID_DEFAULT_LIT ) )

#endif // SW_ENGINE_DEFERREDTILECLASS_HLSLI
