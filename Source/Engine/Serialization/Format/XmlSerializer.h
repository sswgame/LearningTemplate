/**
 * @file XmlSerializer.h
 * @brief XML 백엔드 인터페이스와 TypeInfo 기반 XML 직렬화/역직렬화
 * @note 리플렉션이 아닌 콘텐츠(테이블, 타일맵, 툴)는 Utility/Xml/XmlDocument를 사용합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
	struct TypeInfo;

	using XmlArrayItemDelegate = Delegate<void( string_view itemStr )>;
	using XmlMapItemDelegate   = Delegate<void( string_view keyStr, string_view valStr )>;

	/**
	 * @class IXmlBackend
	 * @brief XML 직렬화/역직렬화 백엔드
	 */
	class IXmlBackend
	{
	public:
		/** @brief 가상 소멸. */
		virtual ~IXmlBackend() = default;

		// ------------------------------------------------------------------------------
		// 1) 쓰기 — 루트, 값/속성, 배열, 맵
		// ------------------------------------------------------------------------------
		/** @brief XML 직렬화를 시작합니다. */
		virtual void initXmlSerialization( const utf8* pRootTagName ) = 0;
		/** @brief 값을 XML 자식 요소로 씁니다. */
		virtual void writeValue( const utf8* pTagName, const utf8* pValueString ) = 0;
		/** @brief 값을 현재 부모 요소의 XML attribute로 씁니다. */
		virtual void writeAttribute( const utf8* pAttrName, const utf8* pValueString ) = 0;
		/** @brief 배열 구간을 시작합니다. */
		virtual void beginArray( const utf8* pTagName ) = 0;
		/** @brief 배열 항목을 씁니다. */
		virtual void writeArrayItem( const utf8* pValueString ) = 0;
		/** @brief 배열 구간을 끝냅니다. */
		virtual void endArray() = 0;
		/** @brief 맵 구간을 시작합니다. */
		virtual void beginMap( const utf8* pTagName ) = 0;
		/** @brief 맵 항목을 시작합니다. */
		virtual void beginMapEntry() = 0;
		/** @brief 맵 키를 씁니다. */
		virtual void writeMapKey( const utf8* pKeyString ) = 0;
		/** @brief 맵 값을 씁니다. */
		virtual void writeMapValue( const utf8* pValueString ) = 0;
		/** @brief 맵 항목을 끝냅니다. */
		virtual void endMapEntry() = 0;
		/** @brief 맵 구간을 끝냅니다. */
		virtual void endMap() = 0;
		/** @brief 직렬화를 마무리하고 XML 문자열을 반환합니다. */
		virtual string endSerialize() = 0;

		// ------------------------------------------------------------------------------
		// 2) 읽기 — 루트, 값/속성, 배열/맵 순회
		// ------------------------------------------------------------------------------
		/** @brief XML 역직렬화를 시작합니다. */
		virtual bool initXmlDeserialization( const utf8* pXmlStr, const utf8* pRootTagName ) = 0;
		/** @brief XML 자식 요소에서 값을 읽습니다. */
		virtual bool readValue( const utf8* pTagName, string& outValue ) = 0;
		/** @brief 현재 부모 요소의 XML attribute를 읽습니다. */
		virtual bool readAttribute( const utf8* pAttrName, string& outValue ) = 0;
		/** @brief Attribute 우선, 없으면 자식 요소로 읽습니다 (호환 로드). */
		virtual bool readValueOrAttribute( const utf8* pName, string& outValue )
		{
			if ( readAttribute( pName, outValue ) )
				return true;
			return readValue( pName, outValue );
		}

		/** @brief 배열 요소를 순회합니다. */
		virtual bool iterateArray( const utf8* pTagName, const XmlArrayItemDelegate& callback ) = 0;
		/** @brief 맵 항목을 순회합니다. */
		virtual bool iterateMap( const utf8* pTagName, const XmlMapItemDelegate& callback ) = 0;

		// ------------------------------------------------------------------------------
		// 3) 키 정책 — 태그/속성 이름만, 값에는 영향 없음
		// ------------------------------------------------------------------------------
		/** @brief 태그/속성 이름 조회 시 대소문자 무시 여부 (기본 true). */
		bool ignoreCaseKeys() const { return _bIgnoreCaseKeys != 0; }
		/** @brief 태그/속성 이름 조회 시 대소문자 무시 여부를 설정합니다. */
		void setIgnoreCaseKeys( bool bIgnoreCaseKeys ) { _bIgnoreCaseKeys = bIgnoreCaseKeys ? 1 : 0; }

	protected:
		/** @brief 기본은 키 대소문자 무시. */
		IXmlBackend() noexcept
			: _bIgnoreCaseKeys{ 1 }
			, _reservedIgnoreCase{ 0 } {}

		uint8				   _bIgnoreCaseKeys	   : 1;
		[[maybe_unused]] uint8 _reservedIgnoreCase : 7;
	};

	/**
	 * @class XmlDocumentBackend
	 * @brief XmlDocument를 쓰는 XML 백엔드
	 */
	class SW_API XmlDocumentBackend : public IXmlBackend
	{
	public:
		/** @brief 빈 XmlDocument 백엔드를 만듭니다. */
		XmlDocumentBackend();
		/** @brief 구현을 정리합니다. */
		virtual ~XmlDocumentBackend() override;

		/** @brief XML 직렬화를 시작합니다. */
		void initXmlSerialization( const utf8* pRootTagName ) override;
		/** @brief 값을 XML 자식 요소로 씁니다. */
		void writeValue( const utf8* pTagName, const utf8* pValueString ) override;
		/** @brief 값을 현재 부모 요소의 XML attribute로 씁니다. */
		void writeAttribute( const utf8* pAttrName, const utf8* pValueString ) override;
		/** @brief 배열 구간을 시작합니다. */
		void beginArray( const utf8* pTagName ) override;
		/** @brief 배열 항목을 씁니다. */
		void writeArrayItem( const utf8* pValueString ) override;
		/** @brief 배열 구간을 끝냅니다. */
		void endArray() override;
		/** @brief 맵 구간을 시작합니다. */
		void beginMap( const utf8* pTagName ) override;
		/** @brief 맵 항목을 시작합니다. */
		void beginMapEntry() override;
		/** @brief 맵 키를 씁니다. */
		void writeMapKey( const utf8* pKeyString ) override;
		/** @brief 맵 값을 씁니다. */
		void writeMapValue( const utf8* pValueString ) override;
		/** @brief 맵 항목을 끝냅니다. */
		void endMapEntry() override;
		/** @brief 맵 구간을 끝냅니다. */
		void endMap() override;
		/** @brief 직렬화를 마무리하고 XML 문자열을 반환합니다. */
		string endSerialize() override;

		/** @brief XML 역직렬화를 시작합니다. */
		bool initXmlDeserialization( const utf8* pXmlStr, const utf8* pRootTagName ) override;
		/** @brief XML 자식 요소에서 값을 읽습니다. */
		bool readValue( const utf8* pTagName, string& outValue ) override;
		/** @brief 현재 부모 요소의 XML attribute를 읽습니다. */
		bool readAttribute( const utf8* pAttrName, string& outValue ) override;
		/** @brief 배열 요소를 순회합니다. */
		bool iterateArray( const utf8* pTagName, const XmlArrayItemDelegate& callback ) override;
		/** @brief 맵 항목을 순회합니다. */
		bool iterateMap( const utf8* pTagName, const XmlMapItemDelegate& callback ) override;

	private:
		struct Impl;
		unique_ptr<Impl> _impl;
	};

	/**
	 * @class XmlSerializer
	 * @brief TypeInfo 리플렉션으로 XML을 쓰고 읽습니다
	 */
	class SW_API XmlSerializer
	{
	public:
		// ------------------------------------------------------------------------------
		// 4) 백엔드 지정 / 기본 XmlDocumentBackend
		// ------------------------------------------------------------------------------
		/** @brief 지정 백엔드로 객체를 XML에 직렬화합니다. */
		static string serialize( const void* pInstance, const TypeInfo& typeInfo,
								 IXmlBackend&			 backend,
								 const SerializeContext& ctx = SerializeContext::getDefault() );
		/** @brief 지정 백엔드로 XML에서 객체를 역직렬화합니다. */
		static bool deserialize( void* pInstance, const TypeInfo& typeInfo,
								 IXmlBackend& backend, string_view xmlStr,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 기본 XmlDocumentBackend로 객체를 XML에 직렬화합니다. */
		static string serialize( const void* pInstance, const TypeInfo& typeInfo,
								 const SerializeContext& ctx = SerializeContext::getDefault() );
		/** @brief 기본 XmlDocumentBackend로 XML에서 객체를 역직렬화합니다. */
		static bool deserialize( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		// ------------------------------------------------------------------------------
		// 5) Soft · 버전 — orphan 수집, 루트 _schemaVersion
		// ------------------------------------------------------------------------------
		/** @brief Soft 역직렬화 — coerce 실패 필드를 orphan으로 수집. */
		static bool deserializeSoft( void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
									 vector<SchemaOrphanValue>* pOutOrphans = nullptr, uint32* pOutVersion = nullptr,
									 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 루트 attribute `_schemaVersion`을 포함한 XML 직렬화. */
		static string serializeVersioned( uint32 version, const void* pInstance, const TypeInfo& typeInfo,
										  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 헤더 + soft deserialize, 필요 시 migrate. */
		static bool deserializeVersioned( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo, string_view xmlStr,
										  uint32 currentVersion = 0, SchemaMigrateFn migrate = nullptr,
										  const TypeInfo*		  pLegacyTypeInfo = nullptr,
										  const SerializeContext& ctx			  = SerializeContext::getDefault() );
	};

} // namespace sw
