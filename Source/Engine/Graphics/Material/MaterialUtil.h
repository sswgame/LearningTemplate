/**
 * @file MaterialUtil.h
 * @brief 머티리얼 패킹 · XML · define 을 함께 다루는 도우미입니다(Engine TU 전용).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    /** @brief 머티리얼 패킹 · XML · 퍼뮤테이션 define 공유 도우미입니다. */
    struct MaterialUtil
    {
        /**
         * @brief 퍼뮤테이션(셰이더 define)이 바뀔 때마다 오르는 전역 세대입니다.
         * @details **정지한 씬을 위한 신호입니다.** `GPUScene` 은 "등록 · 해제도 없고 움직인 것도 없으면"
         *          수집 자체를 건너뜁니다. 그런데 머티리얼 · 인스턴스의 정적 스위치 · 키워드 · 멀티 컴파일을
         *          런타임에 바꾸면 프리미티브는 하나도 더러워지지 않으므로, 그 건너뛰기에 걸려
         *          **바뀐 퍼뮤테이션이 영원히 반영되지 않습니다**(움직이는 씬에서는 가려집니다).
         *          누가 어떤 프리미티브를 쓰는지 역참조를 만드는 대신 세대 하나만 올립니다. 바뀌는 일이
         *          드물고, 읽는 쪽은 프레임당 한 번입니다.
         */
        static uint64 getPermutationGeneration();
        /** @brief 세대를 올립니다. define 이 실제로 달라질 수 있는 설정에서만 부릅니다. */
        static void bumpPermutationGeneration();

        static MaterialPropertyType stringToType( string_view str, uint32& outSize );
        static const utf8*          typeToString( MaterialPropertyType type );
        static uint32               packedSizeOf( MaterialPropertyType type );
        static bool                 isTextureType( MaterialPropertyType type );
        static bool                 isNonBufferType( MaterialPropertyType type );
        static MaterialPropertyType defaultShaderTypeFor( MaterialPropertyType cpuType );
        static MaterialPropertyType shaderTypeFromReflectionName( string_view typeName, uint32 byteSize );
        static uint32               alignOffset( uint32 offset, uint32 typeSize );

        /**
         * @brief 머티리얼의 불리언 글(`true` · `false` · `1` · `0` · `yes` · `no` · `on` · `off`)을 읽습니다. 머티리얼의 불리언 글은 모두 여기를 지납니다.
         * @details 비었으면 조용히 `fallback`, 불리언이 아닌 글이면 `name`(파라미터 · 필드 이름)과 함께 알리고 `fallback` 입니다.
         *          주의: 읽지 못한 글을 말없이 false 로 읽으면 `bSrgb="ture"` 가 기본값(true)도 아닌 false 가 됩니다.
         */
        [[nodiscard]] static bool parseBoolToken( string_view token, string_view name, bool fallback );
        static bool               packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer );

        static string fieldText( XMLNode node, const utf8* pName );
        /** @brief `fieldText` 를 `parseBoolToken` 으로 읽습니다. 필드가 없거나 비었으면 `defaultValue` 입니다. */
        [[nodiscard]] static bool parseBoolField( XMLNode node, const utf8* pName, bool defaultValue );
        static MaterialProperty   parsePropertyNode( XMLNode item );

        static void appendAttribute( XMLNode parent, const utf8* pName, string_view value );
        static void appendBoolAttribute( XMLNode parent, const utf8* pName, bool value );

        static RHIBlendMode         parseBlendMode( string_view modeName );
        static const utf8*          blendModeToString( RHIBlendMode mode );
        static MaterialQualityLevel parseQuality( string_view qualityName );
        static const utf8*          qualityToString( MaterialQualityLevel quality );

        static void parsePermutationNode( XMLNode root, MaterialPermutationDesc& outDesc );
        static void appendPermutationNode( XMLNode root, const MaterialPermutationDesc& permutationDesc );

        static void   appendUniqueDefine( vector<string>& outListDefine, string_view define );
        static void   appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );
        static void   appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine );
        static uint64 hashDefines( const vector<string>& listDefine );
    };
} // namespace sw
