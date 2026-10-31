/**
 * @file MaterialPacking.h
 * @brief 머티리얼 프로퍼티 타입 · 크기 · 정렬, 셰이더 리플렉션 타입 대응, 불리언 글 읽기, 프로퍼티 값을 상수 버퍼 바이트로 묶기를 담당합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Material/Material.h"

namespace sw
{
    struct ShaderVariableInfo;
} // namespace sw

namespace sw
{
    /** @brief 머티리얼 프로퍼티 타입 · 크기 · 정렬, 셰이더 리플렉션 타입 대응, 불리언 글 읽기, 프로퍼티 값을 상수 버퍼 바이트로 묶기를 담당합니다. */
    struct SW_API MaterialPacking
    {
        static MaterialPropertyType stringToType( string_view str, uint32& outSize );

        static const utf8* typeToString( MaterialPropertyType type );

        static uint32 packedSizeOf( MaterialPropertyType type );

        static bool isTextureType( MaterialPropertyType type );

        static bool isNonBufferType( MaterialPropertyType type );

        static MaterialPropertyType defaultShaderTypeFor( MaterialPropertyType cpuType );

        static MaterialPropertyType shaderTypeFromReflectionName( string_view typeName, uint32 byteSize );

        static uint32 alignOffset( uint32 offset, uint32 typeSize );

        /// @brief 셰이더 머티리얼 원소(`SW_MATERIAL_BEGIN`)에서 텍스처 인덱스 필드 이름의 접미사입니다(`albedoMap`, `normalMap`).
        static constexpr const utf8* kTextureIndexMemberSuffix = "Map";

        /**
         * @brief 셰이더 머티리얼 원소의 멤버가 텍스처 인덱스 필드인지 판정합니다. `uint` 이고 이름이 `kTextureIndexMemberSuffix` 로 끝나면 텍스처 인덱스 필드입니다.
         * @details 리플렉션은 텍스처 인덱스도 그냥 `uint` 로 알려 주므로(`pointFilter` 같은 플래그와 구분되지 않습니다) 셰이더 쪽 이름 규칙이 구분 기준입니다.
         *          유니티 셰이더의 `_MainTex`, `_BumpMap` 처럼 이름이 텍스처 슬롯을 말합니다. 규칙은 `binding.hlsli` 의 `SW_MATERIAL_BEGIN` 옆에 적혀 있습니다.
         */
        static bool isTextureIndexMember( string_view memberName, string_view reflectedType, uint32 byteSize );

        /**
         * @brief 머티리얼 없는 배치에 쓰는 폴백 원소 하나의 바이트를 만듭니다. 텍스처 인덱스 필드는 `kInvalidDescriptorIndex`, 나머지는 0 입니다.
         * @details 0 은 Vulkan bindless 의 첫 실제 텍스처이고 DX11 과 GL 에서는 머티리얼 텍스처 서수 0 이므로 **유효한** 인덱스입니다.
         *          `kInvalidDescriptorIndex` 는 셰이더의 `MaterialTexture::sampleIndex` 가 "텍스처 없음"(흰색)으로 읽습니다. 네 백엔드가 같은 값을 냅니다.
         */
        static vector<uint8> makeFallbackElement( const vector<ShaderVariableInfo>& listMember, uint32 stride );

        /**
         * @brief 머티리얼의 불리언 글(`true` · `false` · `1` · `0` · `yes` · `no` · `on` · `off`)을 읽습니다. 머티리얼의 불리언 글은 모두 여기를 지납니다.
         * @details 비었거나 불리언이 아닌 글이면 `name`(파라미터 · 필드 이름)과 함께 오류를 남기고 false 이며 @p outValue 는 그대로입니다.
         *          대신할 값을 받지 않습니다 — 읽지 못한 글을 어떤 값으로 읽든 `bSrgb="ture"` 가 데이터와 다른 값이 됩니다.
         */
        [[nodiscard]] static bool parseBoolToken( string_view token, string_view name, const utf8* pID, bool& outValue );

        static bool packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer );
    };
} // namespace sw
