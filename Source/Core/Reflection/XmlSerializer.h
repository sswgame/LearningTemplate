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
		 * @brief XML 직렬화를 시작합니다
		 */
		virtual void initXmlSerialization( const utf8* rootTagName ) = 0;
		/**
		 * @brief 값을 XML로 씁니다
		 */
		virtual void writeValue( const utf8* tagName, const utf8* valueString ) = 0;
		/**
		 * @brief 배열 구간을 시작합니다
		 */
		virtual void beginArray( const utf8* tagName ) = 0;
		/**
		 * @brief 배열 항목을 씁니다
		 */
		virtual void writeArrayItem( const utf8* valueString ) = 0;
		/**
		 * @brief 배열 구간을 끝냅니다
		 */
		virtual void endArray() = 0;
		/**
		 * @brief 맵 구간을 시작합니다
		 */
		virtual void beginMap( const utf8* tagName ) = 0;
		/**
		 * @brief 맵 항목을 시작합니다
		 */
		virtual void beginMapEntry() = 0;
		/**
		 * @brief 맵 키를 씁니다
		 */
		virtual void writeMapKey( const utf8* keyString ) = 0;
		/**
		 * @brief 맵 값을 씁니다
		 */
		virtual void writeMapValue( const utf8* valueString ) = 0;
		/**
		 * @brief 맵 항목을 끝냅니다
		 */
		virtual void endMapEntry() = 0;
		/**
		 * @brief 맵 구간을 끝냅니다
		 */
		virtual void endMap() = 0;
		/**
		 * @brief 직렬화를 마무리합니다
		 */
		virtual std::string endSerialize() = 0;

		/**
		 * @brief XML 역직렬화를 시작합니다
		 */
		virtual bool initXmlDeserialization( const utf8* xmlStr, const utf8* rootTagName ) = 0;
		/**
		 * @brief XML에서 값을 읽습니다
		 */
		virtual bool readValue( const utf8* tagName, std::string& outValue ) = 0;
		/**
		 * @brief 배열 요소를 순회합니다
		 */
		virtual bool iterateArray( const utf8* tagName, const XmlArrayItemDelegate& callback ) = 0;
		/**
		 * @brief 맵 항목을 순회합니다
		 */
		virtual bool iterateMap( const utf8* tagName, const XmlMapItemDelegate& callback ) = 0;
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
		 * @brief XML 직렬화를 시작합니다
		 */
		void initXmlSerialization( const utf8* rootTagName ) override;
		/**
		 * @brief 값을 XML로 씁니다
		 */
		void writeValue( const utf8* tagName, const utf8* valueString ) override;
		/**
		 * @brief 배열 구간을 시작합니다
		 */
		void beginArray( const utf8* tagName ) override;
		/**
		 * @brief 배열 항목을 씁니다
		 */
		void writeArrayItem( const utf8* valueString ) override;
		/**
		 * @brief 배열 구간을 끝냅니다
		 */
		void endArray() override;
		/**
		 * @brief 맵 구간을 시작합니다
		 */
		void beginMap( const utf8* tagName ) override;
		/**
		 * @brief 맵 항목을 시작합니다
		 */
		void beginMapEntry() override;
		/**
		 * @brief 맵 키를 씁니다
		 */
		void writeMapKey( const utf8* keyString ) override;
		/**
		 * @brief 맵 값을 씁니다
		 */
		void writeMapValue( const utf8* valueString ) override;
		/**
		 * @brief 맵 항목을 끝냅니다
		 */
		void endMapEntry() override;
		/**
		 * @brief 맵 구간을 끝냅니다
		 */
		void endMap() override;
		/**
		 * @brief 직렬화를 마무리합니다
		 */
		std::string endSerialize() override;

		/**
		 * @brief XML 역직렬화를 시작합니다
		 */
		bool initXmlDeserialization( const utf8* xmlStr, const utf8* rootTagName ) override;
		/**
		 * @brief XML에서 값을 읽습니다
		 */
		bool readValue( const utf8* tagName, std::string& outValue ) override;
		/**
		 * @brief 배열 요소를 순회합니다
		 */
		bool iterateArray( const utf8* tagName, const XmlArrayItemDelegate& callback ) override;
		/**
		 * @brief 맵 항목을 순회합니다
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

} // namespace sw
