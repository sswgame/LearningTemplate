/**
 * shading_model_ids.hlsli — 셰이딩 모델 번호 표 한 벌. G버퍼 알베도 알파(8 비트)에 실리고, 디퍼드 조명이 번호마다 사용자 칸(GBufferCustomData)을 푼다.
 *
 * 숫자 #define 만 둔다 — C++ `GBufferLayout`(Renderer/Pipeline/GBufferLayout.h)이 그대로 include 해 `ShadingModelID` 를 만들고 static_assert 로 맞춘다
 * (vfx_simulate.hlsli 와 같은 길 — 표가 두 벌이면 한쪽만 고친 번호가 조용히 다른 모델로 풀린다). 언리얼 ShadingModelID(4 비트, GBufferB.a)와 같은 자리다.
 * 0 은 클리어 값이라 "G버퍼 패스가 그리지 않았다" 의 뜻이다(옛 머티리얼 칸 알파의 그려짐 표시를 대신한다).
 * 병합 지점: W13 의 머티리얼 그래프 Output ShadingModel 설정(DefaultLit | Hair)이 이 번호를 쓴다 — 그 enum 을 이 표로 넓히고 따로 두지 않는다.
 *
 * 사용자 칸(RGBA8)의 뜻 — 번호마다(binding_output.hlsli SurfaceShading · deferred_lighting.hlsl 이 쓰고 읽는다):
 * | 번호              | r                 | g                 | b                 | a                    |
 * |-------------------|-------------------|-------------------|-------------------|----------------------|
 * | SUBSURFACE        | 감쌈 R            | 감쌈 G            | 감쌈 B            | 투과 세기            |
 * | CLOTH             | 시엔 색 R         | 시엔 색 G         | 시엔 색 B         | 시엔 거칠기          |
 * | CLEAR_COAT        | 코트 무게         | 코트 거칠기       | —                 | —                    |
 * | HAIR              | W13 이 정한다(탄젠트 옥타헤드럴 · 옮김) — 병합 지점                                         |
 * | EYE               | 코트(각막 젖음)   | 코트 거칠기       | 공막 감쌈         | 홍채 마스크          |
 * | THIN_TRANSLUCENT  | 투과 색 R         | 투과 색 G         | 투과 색 B         | 감쌈                 |
 * | ANISOTROPIC       | 탄젠트 옥타 u     | 탄젠트 옥타 v     | 비등방 (0..1 → -1..1) | —                |
 */

#ifndef SW_ENGINE_SHADINGMODELIDS_HLSLI
#define SW_ENGINE_SHADINGMODELIDS_HLSLI

#define SW_SHADING_MODEL_ID_NONE             0 // 클리어 — 그리지 않았다
#define SW_SHADING_MODEL_ID_UNLIT            1 // 알베도 그대로(보기 모드 Unlit · 조명 없는 머티리얼)
#define SW_SHADING_MODEL_ID_DEFAULT_LIT      2 // 금속도 · 거칠기 PBR
#define SW_SHADING_MODEL_ID_SUBSURFACE       3 // 피부 — 채널마다 감싼 확산 + 투과
#define SW_SHADING_MODEL_ID_CLOTH            4 // 천 — Charlie 시엔
#define SW_SHADING_MODEL_ID_CLEAR_COAT       5 // 코트 — 기름막 · 니스 · 젖음
#define SW_SHADING_MODEL_ID_HAIR             6 // 머리카락(W13)
#define SW_SHADING_MODEL_ID_EYE              7 // 눈 — 각막 젖음 + 공막 감쌈
#define SW_SHADING_MODEL_ID_THIN_TRANSLUCENT 8 // 얇은 면 — 뒤에서 오는 빛
#define SW_SHADING_MODEL_ID_ANISOTROPIC      9 // 비등방 GGX — 솔질 금속 · 비단
#define SW_SHADING_MODEL_ID_COUNT            10
#define SW_SHADING_MODEL_ID_SCALE            255.0f // 8 비트 UNORM 에 번호를 그대로 싣는다(번호 / 255)

#endif // SW_ENGINE_SHADINGMODELIDS_HLSLI
