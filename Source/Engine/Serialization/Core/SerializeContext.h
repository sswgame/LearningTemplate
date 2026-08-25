/**
 * @file SerializeContext.h
 * @brief 타입별 커스텀 바이너리/텍스트 직렬화 핸들러 레지스트리
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{

	/**
	 * @class SerializeContext
	 * @brief 타입별 커스텀 바이너리/텍스트 직렬화 핸들러 등록·검색
	 */
	class SW_API SerializeContext
	{
	public:
		using BinaryWriteFn = Delegate<void( const void* pValPtr, vector<uint8>& listOutBuf )>;
		using BinaryReadFn	= Delegate<bool( void* pValPtr, const uint8* pData, size_t size, size_t& offset )>;

		using TextWriteFn = Delegate<string( const void* pValPtr )>;
		using TextReadFn  = Delegate<bool( void* pValPtr, string_view valStr )>;

		// ------------------------------------------------------------------------------
		// 1) 수명 — 기본은 키 대소문자 무시
		// ------------------------------------------------------------------------------
		/** @brief 기본 컨텍스트 (키 대소문자 무시). */
		SerializeContext() noexcept
			: _bIgnoreCaseKeys{ 1 }
			, _bAllowUnknownProperties{ 0 }
			, _reservedFlags{ 0 } {}

		// ------------------------------------------------------------------------------
		// 2) 핸들러 등록 · 키 정책
		// ------------------------------------------------------------------------------
		/** @brief 바이너리 읽기/쓰기 커스텀 핸들러를 등록합니다. */
		void registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn );
		/** @brief 텍스트 읽기/쓰기 커스텀 핸들러를 등록합니다. */
		void registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn );

		/** @brief Xml/Json 키·태그·속성 이름 조회 시 대소문자 무시 여부를 설정합니다. */
		SerializeContext& setIgnoreCaseKeys( bool bIgnoreCaseKeys )
		{
			_bIgnoreCaseKeys = bIgnoreCaseKeys ? 1 : 0;
			return *this;
		}

		/** @brief 바이너리/텍스트 역직렬화 시 스키마에 없는 알 수 없는 프로퍼티를 허용(스킵)할지 여부를 설정합니다. */
		SerializeContext& setAllowUnknownProperties( bool bAllow )
		{
			_bAllowUnknownProperties = bAllow ? 1 : 0;
			return *this;
		}

		// ------------------------------------------------------------------------------
		// 3) 조회 — 타입 이름 → 등록된 writer/reader
		// ------------------------------------------------------------------------------
		/** @brief 등록된 바이너리 writer를 찾습니다. */
		const BinaryWriteFn* findBinaryWriter( hashed_string typeName ) const;
		/** @brief 등록된 바이너리 reader를 찾습니다. */
		const BinaryReadFn* findBinaryReader( hashed_string typeName ) const;
		/** @brief 등록된 텍스트 writer를 찾습니다. */
		const TextWriteFn* findTextWriter( hashed_string typeName ) const;
		/** @brief 등록된 텍스트 reader를 찾습니다. */
		const TextReadFn* findTextReader( hashed_string typeName ) const;
		/**
		 * @brief Xml/Json 키·태그·속성 이름 조회 시 대소문자 무시 (기본 true). 값 비교에는 영향 없음.
		 * @details XmlSerializer::deserialize가 이 값을 IXmlBackend에 전달합니다.
		 *          끄려면: `SerializeContext ctx = SerializeContext::getDefault(); ctx.setIgnoreCaseKeys(false);`
		 */
		bool ignoreCaseKeys() const { return _bIgnoreCaseKeys != 0; }
		/** @brief 알 수 없는 프로퍼티 허용(스킵) 여부. */
		bool allowUnknownProperties() const { return _bAllowUnknownProperties != 0; }

		/** @brief 기본 전역 직렬화 컨텍스트를 반환합니다. */
		static const SerializeContext& getDefault();

	private:
		unordered_map<hashed_string, BinaryWriteFn> _mapBinaryWriters;
		unordered_map<hashed_string, BinaryReadFn>	_mapBinaryReaders;
		unordered_map<hashed_string, TextWriteFn>	_mapTextWriters;
		unordered_map<hashed_string, TextReadFn>	_mapTextReaders;
		uint8										_bIgnoreCaseKeys		 : 1;
		uint8										_bAllowUnknownProperties : 1;
		[[maybe_unused]] uint8						_reservedFlags			 : 6;
	};

} // namespace sw
