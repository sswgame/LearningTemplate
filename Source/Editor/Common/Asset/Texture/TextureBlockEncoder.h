/**
 * @file TextureBlockEncoder.h
 * @brief RGBA8 밉 하나를 모바일 압축 형식(ASTC 4x4 · ETC2 RGBA8 · EAC R11 · RG11)으로 인코딩합니다. BC 는 DirectXTex 가 한다(`TextureImporter`).
 * @details shortcut: 품질은 낮다 — ASTC 는 분할 없는 고정 모드(4x4 가중치 2 비트, 끝점 8 비트) 하나, ETC2 는 개별 모드(2x4 두 칸, 기본색 4 비트) 하나다. 대상을 실제로 출시할 때 astc-encoder · etcpak 으로
 *          바꾼다(바이트 형식은 같아 로더와 표는 그대로다). 지금은 쿠킹 → 고르기 → 업로드 길을 외부 의존 없이 끝까지 돌린다. 언리얼 · 유니티는 astcenc 를 쓴다(기억에 의존한 비교).
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
