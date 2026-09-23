/**
 * @file StringPool.h
 * @brief PredefinedNameType 의 사전 정의 표준 타입 이름을 기본으로 싣는 양방향 문자열 intern 풀입니다.
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
     * @brief PredefinedNameType 의 사전 정의 타입 이름을 기본으로 싣고, 중복 문자열을 인덱스 하나로 바꿔 직렬화 크기를 줄이는 문자열 intern 풀입니다.
     */
    class SW_API StringPool
    {
    public:
        static constexpr uint32 kPredefinedCount   = static_cast<uint32>( PredefinedNameType::Count );
        static constexpr uint32 kMaxDynamicStrings = 1000000;

        StringPool();
        ~StringPool()                                  = default;
        StringPool( const StringPool& )                = default;
        StringPool& operator=( const StringPool& )     = default;
        StringPool( StringPool&& ) noexcept            = default;
        StringPool& operator=( StringPool&& ) noexcept = default;

        /** @brief 문자열을 풀에 등록하고 고유 인덱스를 반환합니다(Predefined 포함. 이미 있으면 기존 인덱스를 반환합니다). */
        uint32 internString( string_view str );

        /** @brief 인덱스로 문자열을 찾습니다(범위를 넘으면 빈 string_view). */
        string_view getString( uint32 index ) const;

        /** @brief 풀에 등록된 고유 문자열의 총 개수를 반환합니다(Predefined 포함). */
        size_t getCount() const { return _listString.size(); }

        /** @brief 동적으로 등록된 고유 문자열 개수를 반환합니다(Predefined 제외). */
        size_t getDynamicCount() const { return _listString.size() > kPredefinedCount ? ( _listString.size() - kPredefinedCount ) : 0; }

        /** @brief 동적 풀이 비어 있는지 확인합니다. */
        bool empty() const { return getDynamicCount() == 0; }

        /** @brief 동적 문자열을 비우고 Predefined 만 있는 상태로 되돌립니다. */
        void clear();

        /** @brief 동적으로 등록된 문자열 표만 Archive 에 기록합니다(Predefined 는 적지 않습니다). */
        void saveToArchive( Archive& outArchive ) const;

        /** @brief Archive 에서 동적 문자열 표를 읽어 풀을 채웁니다. */
        bool loadFromArchive( Archive& inArchive );

        /** @brief 동적으로 등록된 문자열 표만 바이트 벡터에 기록합니다. */
        void saveToBinaryBuffer( vector<uint8>& outBytes ) const;

        /** @brief 바이너리 버퍼에서 동적 문자열 표를 읽어 옵니다. */
        bool loadFromBinaryBuffer( const uint8* pData, size_t dataSize, size_t& inoutOffset );

    private:
        void initializePredefined();

        vector<string>                _listString;
        unordered_map<string, uint32> _mapStringToId;
    };
} // namespace sw
