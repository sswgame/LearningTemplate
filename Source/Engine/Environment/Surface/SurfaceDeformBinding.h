/**
 * @file SurfaceDeformBinding.h
 * @brief 씬의 표면 변형 층을 렌더러에 거는 값(텍스처 핸들 + 창)입니다 — 렌더 패킷(`RenderFramePacket::_surfaceDeform`)이 렌더 스레드로 나른다.
 * @details 텍스처는 엔진 텍스처 칸 `SurfaceDeform`, 창은 패스 상수 `g_SurfaceDeformParams` 로 걸려 지면 · 지형 머티리얼의 정점 셰이더와 그림자 패스가 같은 오프셋을 읽는다
 *          (언리얼 런타임 가상 텍스처 변위 · 유니티 `Shader.SetGlobalTexture` 와 같은 자리 — 머티리얼 인스턴스마다 싣지 않는다).
 *          렌더러가 서비스 헤더(`SurfaceDeformSystem.h`)를 보지 않도록 값 타입만 따로 둔다(`SkyGradient` 와 같다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    /** @brief 표면 변형 층 한 벌입니다(파일 머리말 참고). 층이 없으면 핸들이 비고 `_params.z` 가 0 이다. */
    struct SurfaceDeformBinding
    {
        TextureHandle _texture; ///< 높이 텍스처(CPU 텍스처 — R = 1 - 꺼짐 / 범위, G = 1 - 솟음 / 범위)
        float4        _params;  ///< (창 최소 x, 창 최소 z, 1 / 창 너비, 깊이 범위)
    };
} // namespace sw
