/**
 * @file TextureBlockEncoder.h
 * @brief RGBA8 밉 하나를 모바일 압축 형식(ASTC 4x4 · ETC2 RGBA8 · EAC R11 · RG11)으로 인코딩합니다. BC 는 DirectXTex 가 한다(`TextureImporter`).
 * @details ASTC 는 Arm astc-encoder(`-medium` 품질, 언리얼 · 유니티와 같은 인코더), ETC2 · EAC 는 etcpak 이 한다(둘 다 ThirdParty 오버레이 포트, 에디터 전용).
 *          두 라이브러리 모두 -march 없이 지어 빌드 기계와 상관없이 같은 블록을 낸다(astcenc 부동 소수 불변 · etcpak 스칼라 길) — 결과가 DDC 키로 쓰인다.
 *          행은 빈틈없이 이어져야 하고, 4 로 나누어떨어지지 않는 가장자리 블록은 마지막 화소를 되풀이해 채운다(`computeRHITextureMipLayout` 과 같은 블록 수).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw::editor
{
    /**
     * @struct TextureBlockEncoder
     * @brief 파일 머리말 참고.
     */
    struct TextureBlockEncoder
    {
        /** @brief 이 인코더가 만드는 형식이면 true 입니다. */
        [[nodiscard]] static bool isEncodable( RHIFormat format );
        /**
         * @brief @p pRGBA(행 = 너비 × 4 바이트)를 @p format 으로 인코딩해 @p ioBytes 끝에 더합니다. 인코더가 모르는 형식이면 오류를 남기고 false 입니다.
         * @details EAC R11 · RG11 은 R · RG 채널만 쓴다. 입력의 다른 채널은 버린다.
         */
        [[nodiscard]] static bool encodeMip( RHIFormat format, const uint8* pRGBA, uint32 width, uint32 height, vector<uint8>& ioBytes );
    };
} // namespace sw::editor
