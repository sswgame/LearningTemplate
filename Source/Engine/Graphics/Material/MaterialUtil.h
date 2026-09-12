/**
 * @file MaterialUtil.h
 * @brief 머티리얼 패킹 / XML / define 공유 헬퍼 (Engine TU 전용)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    /** @brief 머티리얼 패킹·XML·permutation define 공유 헬퍼 */
    struct MaterialUtil
    {
        /**
         * @brief 퍼뮤테이션(셰이더 define)이 바뀔 때마다 오르는 전역 세대입니다.
         * @details **정지한 씬을 위한 신호다.** `GpuScene` 은 "등록·해제도 없고 움직인 것도 없으면"
         *          수집 자체를 건너뛴다. 그런데 머티리얼/인스턴스의 정적 스위치·키워드·멀티컴파일을
         *          런타임에 바꾸면 프리미티브는 하나도 더러워지지 않으므로, 그 건너뛰기에 걸려
         *          **바뀐 퍼뮤테이션이 영원히 반영되지 않는다**(움직이는 벤치에서는 우연히 가려져 있었다).
         *          누가 어떤 프리미티브를 쓰는지 역참조를 만드는 대신 세대 하나만 올린다 — 바뀌는 일이
         *          드물고, 읽는 쪽은 프레임당 한 번이다.
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

        static bool parseBoolToken( string_view token );
        static bool packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer );

        static string           fieldText( XmlNode node, const utf8* pName );
        static bool             parseBoolField( XmlNode node, const utf8* pName, bool defaultValue );
        static MaterialProperty parsePropertyNode( XmlNode item );

        static void appendAttr( XmlNode parent, const utf8* pName, string_view value );
        static void appendBoolAttr( XmlNode parent, const utf8* pName, bool value );

        static RHIBlendMode         parseBlendMode( string_view modeName );
        static const utf8*          blendModeToString( RHIBlendMode mode );
        static MaterialQualityLevel parseQuality( string_view qualityName );
        static const utf8*          qualityToString( MaterialQualityLevel quality );

        static void parsePermutationNode( XmlNode root, MaterialPermutationDesc& out );
        static void appendPermutationNode( XmlNode root, const MaterialPermutationDesc& perm );

        static void   appendUniqueDefine( vector<string>& outListDefine, string_view def );
        static void   appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );
        static void   appendQualityDefines( MaterialQualityLevel quality, vector<string>& outListDefine );
        static uint64 hashDefines( const vector<string>& listDefine );
    };
} // namespace sw
