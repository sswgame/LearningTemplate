#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionCore.h"

/**
 * @file Serializer.h
 * @brief Binary, JSON, XML 및 CDO 기반 Object Diff 직렬화/역직렬화 엔진 정의
 */

namespace sw
{
	using XmlArrayItemDelegate = Delegate<void( std::string_view itemStr )>;
	using XmlMapItemDelegate   = Delegate<void( std::string_view keyStr, std::string_view valStr )>;

	/**
	 * @class SerializeContext
	 * @brief 타입별 커스텀 바이너리/텍스트 직렬화 핸들러 등록 및 검색 레지스트리
	 */
	class SW_API SerializeContext
	{
	public:
		using BinaryWriteFn = Delegate<void( const void* valPtr, std::vector<uint8>& outBuf )>;
		using BinaryReadFn	= Delegate<bool( void* valPtr, const uint8* data, size_t size, size_t& offset )>;

		using TextWriteFn = Delegate<std::string( const void* valPtr )>;
		using TextReadFn  = Delegate<bool( void* valPtr, std::string_view valStr )>;

		/** @brief 바이너리 읽기/쓰기 커스텀 핸들러 등록 */
		void registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn );

		/** @brief 텍스트 읽기/쓰기 커스텀 핸들러 등록 */
		void registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn );

		const BinaryWriteFn* findBinaryWriter( hashed_string typeName ) const;
		const BinaryReadFn*	 findBinaryReader( hashed_string typeName ) const;
		const TextWriteFn*	 findTextWriter( hashed_string typeName ) const;
		const TextReadFn*	 findTextReader( hashed_string typeName ) const;

		/** @brief 기본 전역 직렬화 컨텍스트 반환 */
		static const SerializeContext& getDefault();

	private:
		std::unordered_map<hashed_string, BinaryWriteFn> _binaryWriters;
		std::unordered_map<hashed_string, BinaryReadFn>	 _binaryReaders;
		std::unordered_map<hashed_string, TextWriteFn>	 _textWriters;
		std::unordered_map<hashed_string, TextReadFn>	 _textReaders;
	};

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
	 * @class BinarySerializer
	 * @brief 타입 정보를 참조하여 객체를 콤팩트 바이너리 버퍼로 직렬화/역직렬화하는 클래스
	 */
	class SW_API BinarySerializer
	{
	public:
		/** @brief 기본 바이너리 직렬화 */
		static void serialize( const void* instance, const TypeInfo& typeInfo, std::vector<uint8>& outBuffer,
							   const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 기본 바이너리 역직렬화 */
		static bool deserialize( void* instance, const TypeInfo& typeInfo, const uint8* data, size_t dataSize,
								 const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 버퍼 헤더를 포함하는 바이너리 직렬화 */
		static void serializeVersioned( uint32 version, const void* instance, const TypeInfo& typeInfo, std::vector<uint8>& outBuffer,
										const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 버전 정보를 검증하는 바이너리 역직렬화 */
		static bool deserializeVersioned( uint32& outVersion, void* instance, const TypeInfo& typeInfo, const uint8* data, size_t dataSize,
										  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief POD memcpy 또는 binary serialize/deserialize로 객체 복제 */
		static bool cloneObject( void* dstData, const void* srcData, const TypeInfo& typeInfo );
	};

	/**
	 * @class JsonSerializer
	 * @brief TypeInfo 리플렉션을 이용한 JSON 직렬화/역직렬화 클래스
	 */
	class SW_API JsonSerializer
	{
	public:
		/** @brief 한 줄 단축 JSON 직렬화 */
		static std::string serialize( const void* instance, const TypeInfo& typeInfo,
									  const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief 들여쓰기가 포함된 Pretty JSON 직렬화 */
		static std::string serializePretty( const void* instance, const TypeInfo& typeInfo, uint32 indentSpaces = 4,
											const SerializeContext& ctx = SerializeContext::getDefault() );

		/** @brief JSON 문자열 역직렬화 */
		static bool		   deserialize( void* instance, const TypeInfo& typeInfo, std::string_view jsonStr,
										const SerializeContext& ctx = SerializeContext::getDefault() );
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

	/**
	 * @class ObjectDiffSerializer
	 * @brief CDO(Class Default Object) 기본값 대비 수정된 프로퍼티 델타(Delta)만 직렬화하는 최적화 클래스
	 */
	class SW_API ObjectDiffSerializer
	{
	public:
		/** @brief CDO 객체와 변경된 객체를 비교하여 델타 바이너리 추출 */
		static bool serializeDiff( std::vector<uint8>& outDiffBuffer, const void* cdoInstance, const void* modifiedInstance, const TypeInfo& typeInfo );

		/** @brief 델타 바이너리를 타깃 인스턴스에 적용 */
		static bool deserializeDiff( void* targetInstance, const TypeInfo& typeInfo, const uint8* diffData, size_t diffSize );
	};
}
