/**
 * @file MaterialInternal.h
 * @brief 머티리얼 패킹 / XML / define 공유 헬퍼 (Engine TU 전용)
 */
#pragma once
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) 프로퍼티 타입 — 이름↔enum, 패킹 크기, 텍스처/논버퍼
	// ------------------------------------------------------------------------------
	/** @brief 문자열을 MaterialPropertyType과 패킹 크기로 해석합니다. */
	MaterialPropertyType stringToType( string_view str, uint32& outSize );
	/** @brief MaterialPropertyType의 안정 이름을 반환합니다. */
	const utf8* typeToString( MaterialPropertyType type );
	/** @brief 패킹 버퍼에서 차지하는 바이트 크기. */
	uint32 packedSizeOf( MaterialPropertyType type );
	/** @brief 텍스처 슬롯 타입이면 true. */
	bool isTextureType( MaterialPropertyType type );
	/** @brief 상수 버퍼에 넣지 않는 타입이면 true. */
	bool isNonBufferType( MaterialPropertyType type );
	/** @brief CPU 타입에 대응하는 기본 셰이더 타입. */
	MaterialPropertyType defaultShaderTypeFor( MaterialPropertyType cpuType );
	/** @brief 리플렉션 타입 이름과 바이트 크기로 셰이더 타입을 고릅니다. */
	MaterialPropertyType shaderTypeFromReflectionName( string_view typeName, uint32 byteSize );
	/** @brief typeSize 정렬에 맞게 오프셋을 올립니다. */
	uint32 alignOffset( uint32 offset, uint32 typeSize );

	// ------------------------------------------------------------------------------
	// 2) 패킹 · XML 필드
	// ------------------------------------------------------------------------------
	/** @brief true/false/1/0 토큰을 bool로 파싱합니다. */
	bool parseBoolToken( string_view token );
	/** @brief 프로퍼티 값을 패킹 버퍼에 씁니다. */
	bool packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer );

	/** @brief 자식 노드 텍스트를 읽습니다. */
	string fieldText( XmlNode node, const utf8* pName );
	/** @brief 자식 노드 bool 필드를 파싱합니다. */
	bool parseBoolField( XmlNode node, const utf8* pName, bool defaultValue );
	/** @brief Property XML 노드를 MaterialProperty로 파싱합니다. */
	MaterialProperty parsePropertyNode( XmlNode item );

	/** @brief 문자열 속성을 추가합니다. */
	void appendAttr( XmlNode parent, const utf8* pName, string_view value );
	/** @brief bool 속성을 추가합니다. */
	void appendBoolAttr( XmlNode parent, const utf8* pName, bool value );

	// ------------------------------------------------------------------------------
	// 3) 블렌드 · 품질 · permutation define
	// ------------------------------------------------------------------------------
	/** @brief 문자열을 RHIBlendMode로 파싱합니다. */
	RHIBlendMode parseBlendMode( string_view s );
	/** @brief RHIBlendMode의 안정 이름. */
	const utf8* blendModeToString( RHIBlendMode mode );
	/** @brief 문자열을 MaterialQualityLevel로 파싱합니다. */
	MaterialQualityLevel parseQuality( string_view s );
	/** @brief MaterialQualityLevel의 안정 이름. */
	const utf8* qualityToString( MaterialQualityLevel q );

	/** @brief permutation XML을 MaterialPermutationDesc로 파싱합니다. */
	void parsePermutationNode( XmlNode root, MaterialPermutationDesc& out );
	/** @brief MaterialPermutationDesc를 XML로 씁니다. */
	void appendPermutationNode( XmlNode root, const MaterialPermutationDesc& perm );

	/** @brief define이 없으면 out에 추가합니다. */
	void appendUniqueDefine( vector<string>& out, string_view def );
	/** @brief usage 플래그에 맞는 셰이더 define을 추가합니다. */
	void appendUsageDefines( MaterialUsageFlags usage, vector<string>& out );
	/** @brief 품질 레벨에 맞는 셰이더 define을 추가합니다. */
	void appendQualityDefines( MaterialQualityLevel q, vector<string>& out );
	/** @brief define 목록의 해시를 계산합니다. */
	uint64 hashDefines( const vector<string>& defs );

} // namespace sw
