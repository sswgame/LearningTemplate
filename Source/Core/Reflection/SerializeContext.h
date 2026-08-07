#pragma once
/**
 * @file SerializeContext.h
 * @brief Type-specific custom binary/text serialization handler registry
 */

#include "Core/CoreMinimal.h"

namespace sw
{

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

}
