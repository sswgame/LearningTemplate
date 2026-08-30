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
		static MaterialPropertyType stringToType( string_view str, uint32& outSize );
		static const utf8*			typeToString( MaterialPropertyType type );
		static uint32				packedSizeOf( MaterialPropertyType type );
		static bool					isTextureType( MaterialPropertyType type );
		static bool					isNonBufferType( MaterialPropertyType type );
		static MaterialPropertyType defaultShaderTypeFor( MaterialPropertyType cpuType );
		static MaterialPropertyType shaderTypeFromReflectionName( string_view typeName, uint32 byteSize );
		static uint32				alignOffset( uint32 offset, uint32 typeSize );

		static bool parseBoolToken( string_view token );
		static bool packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer );

		static string			fieldText( XmlNode node, const utf8* pName );
		static bool				parseBoolField( XmlNode node, const utf8* pName, bool defaultValue );
		static MaterialProperty parsePropertyNode( XmlNode item );

		static void appendAttr( XmlNode parent, const utf8* pName, string_view value );
		static void appendBoolAttr( XmlNode parent, const utf8* pName, bool value );

		static RHIBlendMode			parseBlendMode( string_view s );
		static const utf8*			blendModeToString( RHIBlendMode mode );
		static MaterialQualityLevel parseQuality( string_view s );
		static const utf8*			qualityToString( MaterialQualityLevel q );

		static void parsePermutationNode( XmlNode root, MaterialPermutationDesc& out );
		static void appendPermutationNode( XmlNode root, const MaterialPermutationDesc& perm );

		static void	  appendUniqueDefine( vector<string>& outListDefine, string_view def );
		static void	  appendUsageDefines( MaterialUsageFlags usage, vector<string>& outListDefine );
		static void	  appendQualityDefines( MaterialQualityLevel q, vector<string>& outListDefine );
		static uint64 hashDefines( const vector<string>& listDefine );
	};
} // namespace sw
