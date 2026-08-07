#pragma once
/**
 * @file XmlSerializer.h
 * @brief XML backend interface and TypeInfo-based XML serialize/deserialize
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Reflection/SerializeContext.h"

namespace sw
{

	using XmlArrayItemDelegate = Delegate<void( std::string_view itemStr )>;
	using XmlMapItemDelegate   = Delegate<void( std::string_view keyStr, std::string_view valStr )>;

	/**
	 * @class IXmlBackend
	 * @brief XML 직렬화/역직렬화 백엔드 추상 인터페이스
	 */
	class IXmlBackend
	{
	public:
		virtual ~IXmlBackend() = default;

		/**
		 * @brief initXmlSerialization 처리를 수행합니다.
		 */
		virtual void		initXmlSerialization( const utf8* rootTagName )			   = 0;
		/**
		 * @brief writeValue 처리를 수행합니다.
		 */
		virtual void		writeValue( const utf8* tagName, const utf8* valueString ) = 0;
		/**
		 * @brief beginArray 처리를 수행합니다.
		 */
		virtual void		beginArray( const utf8* tagName )						   = 0;
		/**
		 * @brief writeArrayItem 처리를 수행합니다.
		 */
		virtual void		writeArrayItem( const utf8* valueString )				   = 0;
		/**
		 * @brief endArray 처리를 수행합니다.
		 */
		virtual void		endArray()												   = 0;
		/**
		 * @brief beginMap 처리를 수행합니다.
		 */
		virtual void		beginMap( const utf8* tagName )							   = 0;
		/**
		 * @brief beginMapEntry 처리를 수행합니다.
		 */
		virtual void		beginMapEntry()											   = 0;
		/**
		 * @brief writeMapKey 처리를 수행합니다.
		 */
		virtual void		writeMapKey( const utf8* keyString )					   = 0;
		/**
		 * @brief writeMapValue 처리를 수행합니다.
		 */
		virtual void		writeMapValue( const utf8* valueString )				   = 0;
		/**
		 * @brief endMapEntry 처리를 수행합니다.
		 */
		virtual void		endMapEntry()											   = 0;
		/**
		 * @brief endMap 처리를 수행합니다.
		 */
		virtual void		endMap()												   = 0;
		/**
		 * @brief endSerialize 처리를 수행합니다.
		 */
		virtual std::string endSerialize()											   = 0;

		/**
		 * @brief initXmlDeserialization 처리를 수행합니다.
		 */
		virtual bool initXmlDeserialization( const utf8* xmlStr, const utf8* rootTagName )	   = 0;
		/**
		 * @brief readValue 처리를 수행합니다.
		 */
		virtual bool readValue( const utf8* tagName, std::string& outValue )				   = 0;
		/**
		 * @brief iterateArray 처리를 수행합니다.
		 */
		virtual bool iterateArray( const utf8* tagName, const XmlArrayItemDelegate& callback ) = 0;
		/**
		 * @brief iterateMap 처리를 수행합니다.
		 */
		virtual bool iterateMap( const utf8* tagName, const XmlMapItemDelegate& callback )	   = 0;
	};

	/**
	 * @class RapidXmlBackend
	 * @brief RapidXML 파서를 이용한 XML 백엔드 구현체
	 */
	class RapidXmlBackend : public IXmlBackend
	{
	public:
		RapidXmlBackend();
		~RapidXmlBackend() override;

		/**
		 * @brief initXmlSerialization 처리를 수행합니다.
		 */
		void		initXmlSerialization( const utf8* rootTagName ) override;
		/**
		 * @brief writeValue 처리를 수행합니다.
		 */
		void		writeValue( const utf8* tagName, const utf8* valueString ) override;
		/**
		 * @brief beginArray 처리를 수행합니다.
		 */
		void		beginArray( const utf8* tagName ) override;
		/**
		 * @brief writeArrayItem 처리를 수행합니다.
		 */
		void		writeArrayItem( const utf8* valueString ) override;
		/**
		 * @brief endArray 처리를 수행합니다.
		 */
		void		endArray() override;
		/**
		 * @brief beginMap 처리를 수행합니다.
		 */
		void		beginMap( const utf8* tagName ) override;
		/**
		 * @brief beginMapEntry 처리를 수행합니다.
		 */
		void		beginMapEntry() override;
		/**
		 * @brief writeMapKey 처리를 수행합니다.
		 */
		void		writeMapKey( const utf8* keyString ) override;
		/**
		 * @brief writeMapValue 처리를 수행합니다.
		 */
		void		writeMapValue( const utf8* valueString ) override;
		/**
		 * @brief endMapEntry 처리를 수행합니다.
		 */
		void		endMapEntry() override;
		/**
		 * @brief endMap 처리를 수행합니다.
		 */
		void		endMap() override;
		/**
		 * @brief endSerialize 처리를 수행합니다.
		 */
		std::string endSerialize() override;

		/**
		 * @brief initXmlDeserialization 처리를 수행합니다.
		 */
		bool initXmlDeserialization( const utf8* xmlStr, const utf8* rootTagName ) override;
		/**
		 * @brief readValue 처리를 수행합니다.
		 */
		bool readValue( const utf8* tagName, std::string& outValue ) override;
		/**
		 * @brief iterateArray 처리를 수행합니다.
		 */
		bool iterateArray( const utf8* tagName, const XmlArrayItemDelegate& callback ) override;
		/**
		 * @brief iterateMap 처리를 수행합니다.
		 */
		bool iterateMap( const utf8* tagName, const XmlMapItemDelegate& callback ) override;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};

	/**
	 * @class XmlSerializer
	 * @brief TypeInfo 리플렉션을 이용한 XML 직렬화/역직렬화 클래스
	 */
	class SW_API XmlSerializer
	{
	public:
		static std::string serialize( const void* instance, const TypeInfo& typeInfo,
									  IXmlBackend&			  backend,
									  const SerializeContext& ctx = SerializeContext::getDefault() );
		static bool		   deserialize( void* instance, const TypeInfo& typeInfo,
										IXmlBackend& backend, std::string_view xmlStr,
										const SerializeContext& ctx = SerializeContext::getDefault() );

		static std::string serialize( const void* instance, const TypeInfo& typeInfo,
									  const SerializeContext& ctx = SerializeContext::getDefault() );
		static bool		   deserialize( void* instance, const TypeInfo& typeInfo, std::string_view xmlStr,
										const SerializeContext& ctx = SerializeContext::getDefault() );
	};

}
