/**
 * @file StringPool.h
 * @brief PredefinedNameType 사전 정의 표준 타입을 기본 탑재한 양방향 문자열 인터닝 풀
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
	class Archive;

	/**
	 * @class StringPool
	 * @brief PredefinedNameType 사전 정의 타입을 기본 탑재하고, 중복 문자열을 1개의 인덱스로 치환하여 직렬화 용량을 최소화하는 문자열 인터닝 풀
	 */
	class SW_API StringPool
	{
	public:
		static constexpr uint32 kPredefinedCount = static_cast<uint32>( PredefinedNameType::Count );

		StringPool();
		~StringPool()								   = default;
		StringPool( const StringPool& )				   = default;
		StringPool& operator=( const StringPool& )	   = default;
		StringPool( StringPool&& ) noexcept			   = default;
		StringPool& operator=( StringPool&& ) noexcept = default;

		/** @brief 문자열을 풀에 등록하고 고유 인덱스를 반환합니다 (Predefined 포함, 기존에 있으면 기존 인덱스 반환). */
		uint32 internString( string_view str );

		/** @brief 인덱스로부터 문자열을 조회합니다 (범위 초과 시 빈 string_view 반환). */
		string_view getString( uint32 index ) const;

		/** @brief 풀에 등록된 고유 문자열 총 개수를 반환합니다 (Predefined 포함). */
		size_t getCount() const { return _listString.size(); }

		/** @brief 동적으로 등록된 고유 문자열 개수를 반환합니다 (Predefined 제외). */
		size_t getDynamicCount() const { return _listString.size() > kPredefinedCount ? ( _listString.size() - kPredefinedCount ) : 0; }

		/** @brief 동적 풀이 비어있는지 확인합니다. */
		bool empty() const { return getDynamicCount() == 0; }

		/** @brief 동적 문자열을 비우고 Predefined 상태로 초기화합니다. */
		void clear();

		/** @brief 동적으로 등록된 문자열 테이블만 Archive에 기록합니다 (Predefined는 0바이트 생략). */
		void saveToArchive( Archive& outArchive ) const;

		/** @brief Archive로부터 동적 문자열 테이블을 읽어 풀을 채웁니다. */
		bool loadFromArchive( Archive& inArchive );

		/** @brief 동적으로 등록된 문자열 테이블만 바이트 벡터에 기록합니다. */
		void saveToBinaryBuffer( vector<uint8>& outBytes ) const;

		/** @brief 바이너리 버퍼로부터 동적 문자열 테이블을 로드합니다. */
		bool loadFromBinaryBuffer( const uint8* pData, size_t dataSize, size_t& inoutOffset );

	private:
		void initPredefined();

		vector<string>				  _listString;
		unordered_map<string, uint32> _mapStringToId;
	};
} // namespace sw
