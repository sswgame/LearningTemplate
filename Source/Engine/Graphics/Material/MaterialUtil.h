/**
 * @file MaterialUtil.h
 * @brief 머티리얼 패킹, XML, define 을 함께 다루는 도우미입니다(Engine TU 전용).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    struct ShaderVariableInfo;

    /** @brief 머티리얼 패킹, XML, 퍼뮤테이션 define 을 공유하는 도우미입니다. */
    struct SW_API MaterialUtil
    {
        /**
         * @brief 퍼뮤테이션(셰이더 define)이 바뀔 때마다 증가하는 전역 세대입니다.
         * @details **정지한 씬을 위한 신호입니다.** `GPUScene` 은 "등록과 해제도 없고 움직인 것도 없으면"
         *          수집 자체를 건너뜁니다. 그런데 머티리얼과 인스턴스의 정적 스위치, 키워드, 멀티 컴파일을
         *          런타임에 바꾸면 프리미티브는 하나도 더러워지지 않으므로, 그 건너뛰기에 걸려
         *          **바뀐 퍼뮤테이션이 영원히 반영되지 않습니다**(움직이는 씬에서는 가려집니다).
         *          누가 어떤 프리미티브를 쓰는지 역참조를 만드는 대신 세대 하나만 증가시킵니다. 바뀌는 일이
         *          드물고, 읽는 쪽은 프레임당 한 번입니다.
         */
        static uint64 getPermutationGeneration();
        /** @brief 세대를 증가시킵니다. define 이 실제로 달라질 수 있는 설정에서만 부릅니다. */
        static void bumpPermutationGeneration();

        static MaterialPropertyType stringToType( string_view str, uint32& outSize );
        static const utf8*          typeToString( MaterialPropertyType type );
        static uint32               packedSizeOf( MaterialPropertyType type );
        static bool                 isTextureType( MaterialPropertyType type );
        static bool                 isNonBufferType( MaterialPropertyType type );
        static MaterialPropertyType defaultShaderTypeFor( MaterialPropertyType cpuType );
        static MaterialPropertyType shaderTypeFromReflectionName( string_view typeName, uint32 byteSize );
        static uint32               alignOffset( uint32 offset, uint32 typeSize );

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
        static bool               packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer );

        static string fieldText( XMLNode node, const utf8* pName );
        /**
         * @brief 선택 필드를 읽습니다. 필드가 없으면 @p absentValue(형식의 "적지 않음" 값)이고, 있는데 읽지 못하면 오류를 남기고 false 입니다.
         * @details 쓰기(`appendBoolAttribute`)가 기본값과 같은 값을 적지 않는 필드만 이 함수로 읽습니다.
         */
        [[nodiscard]] static bool parseOptionalBoolField( XMLNode node, const utf8* pName, bool absentValue, bool& outValue );
        /** @brief 프로퍼티 한 줄을 읽습니다. 읽지 못한 필드가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePropertyNode( XMLNode item, MaterialProperty& outProp );

        static void appendAttribute( XMLNode parent, const utf8* pName, string_view value );
        static void appendBoolAttribute( XMLNode parent, const utf8* pName, bool value );

        /** @brief 블렌드 모드 이름(`Opaque` · `Transparent` · `AlphaBlend`, 대소문자 무시)을 읽습니다. 모르는 이름이면 오류를 남기고 false 이며 @p outMode 는 그대로입니다. */
        [[nodiscard]] static bool parseBlendMode( string_view modeName, RHIBlendMode& outMode );
        static const utf8*        blendModeToString( RHIBlendMode mode );
        /** @brief 품질 이름을 읽습니다. 모르는 이름이면 오류를 남기고 false 이며 @p outQuality 는 그대로입니다. */
        [[nodiscard]] static bool parseQuality( string_view qualityName, MaterialQualityLevel& outQuality );
        static const utf8*        qualityToString( MaterialQualityLevel quality );

        /** @brief 순열 설정을 읽습니다. 읽지 못한 불리언 필드가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parsePermutationNode( XMLNode root, MaterialPermutationDesc& outDesc );
        static void               appendPermutationNode( XMLNode root, const MaterialPermutationDesc& permutationDesc );

        static void   appendUniqueDefine( vector<string>& outListDefine, string_view define );
        static void   appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );
        static void   appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine );
        static uint64 hashDefines( const vector<string>& listDefine );
    };
} // namespace sw
