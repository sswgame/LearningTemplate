/**
 * @file ScheduleSystemInternal.h
 * @brief 일정 시스템의 해시 키 · 시각 · 바이트 도우미입니다. `ScheduleSystem.cpp` 와 개발 전용 `Dev/ScheduleSystemDebug.cpp` 만 포함합니다.
 * @details 계획과 디버그 설명이 같은 확률 키를 써야 해서 두 .cpp 가 한 정의를 나눈다. 모듈 밖에서는 포함하지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

#include <cmath>
#include <cstring>

namespace sw
{
    /**
     * @struct ScheduleHash
     * @brief 일정 시스템이 쓰는 해시 함수 모음입니다(구간의 식별 키, 이름 키, 키 섞기). 모두 static 입니다.
     * @details 계획, 실행, 저장, 디버그가 같은 구간에서 같은 키를 얻어야 합니다. 사건, 경로 캐시, 계획 비교, 저장 해시가 이 키를 씁니다.
     *          바이트를 섞는 규칙과 상수는 `HashUtil` 에 있습니다. 저장과 네트워크로 나가는 값이라 섞는 순서를 바꾸지 않습니다.
     */
    struct ScheduleHash
    {
        static constexpr uint32 kGoldenRatio32 = 0x9e3779b1u; ///< 2^32 / 황금비 — 키 섞기의 곱수(저장 · 네트워크 값이라 바꾸지 않는다)
        static constexpr uint32 kMixOffset     = 0x7f4a7c15u; ///< 둘째 키에 더하는 값(저장 · 네트워크 값이라 바꾸지 않는다)

        /** @brief 이름의 글자 바이트를 @p hash 에 이어 섞습니다. */
        [[nodiscard]] static uint64 appendName( const uint64 hash, const hashed_string& name ) { return HashUtil::appendBytes64( hash, name.c_str(), name.size() ); }

        /** @brief 위치의 x, y, z 를 @p hash 에 이어 섞습니다. */
        [[nodiscard]] static uint64 appendPosition( uint64 hash, const float3& position )
        {
            hash = HashUtil::appendValue64( hash, position._x );
            hash = HashUtil::appendValue64( hash, position._y );
            return HashUtil::appendValue64( hash, position._z );
        }

        /** @brief 이름 하나의 32 비트 키입니다(64 비트 해시의 아래 32 비트). */
        [[nodiscard]] static uint32 computeNameKey( const hashed_string& name ) { return static_cast<uint32>( appendName( HashUtil::kFNVOffset64, name ) ); }

        /** @brief 두 32 비트 키를 섞습니다(순서가 있다). */
        [[nodiscard]] static uint32 mixKey( const uint32 lhs, const uint32 rhs ) { return HashUtil::mix32( lhs * kGoldenRatio32 ^ HashUtil::mix32( rhs + kMixOffset ) ); }

        /** @brief 구간의 식별 키입니다. 출처, 블록, 시간, 위치, 출발이 같으면 같은 값입니다. 사건, 경로 캐시, 계획 비교가 씁니다. 0 은 쓰지 않습니다. */
        [[nodiscard]] static uint64 computeSegmentKey( const ScheduleSegment& segment )
        {
            uint64 hash = HashUtil::kFNVOffset64;
            hash        = appendName( hash, segment._sourceID );
            hash        = appendName( hash, segment._activity );
            hash        = HashUtil::appendValue64( hash, segment._startMinute );
            hash        = HashUtil::appendValue64( hash, segment._endMinute );
            hash        = HashUtil::appendValue64( hash, segment._blockIndex );
            hash        = HashUtil::appendValue64( hash, segment._ownerIndex );
            hash        = HashUtil::appendValue64( hash, segment._source );
            hash        = appendPosition( hash, segment._target._position );
            hash        = HashUtil::appendValue64( hash, segment._departMinute );
            hash        = HashUtil::appendValue64( hash, segment._travelMinutes );
            return hash == 0 ? 1 : hash;
        }
    };

    struct ScheduleSystemInternal
    {
        static int32 toDay( int32 absoluteMinute ) { return absoluteMinute / kScheduleMinutesPerDay; }

        /** @brief 마지막으로 나선 칸 — 나선 시각이 @p minuteOfDay 이하인 마지막 칸입니다(출발 시각은 순서대로 늘어난다). */
        static optional<int32> findActiveSegment( const vector<ScheduleSegment>& listSegment, float32 minuteOfDay )
        {
            optional<int32> activeIndex;
            optional<int32> firstIndex;
            for ( int32 segmentIndex = 0; segmentIndex < static_cast<int32>( listSegment.size() ); ++segmentIndex )
            {
                const ScheduleSegment& segment = listSegment[static_cast<size_t>( segmentIndex )];
                if ( segment._bPast != SW_FALSE )
                    continue;
                if ( firstIndex.has_value() == false )
                    firstIndex = segmentIndex;
                if ( static_cast<float32>( segment._departMinute ) <= minuteOfDay )
                    activeIndex = segmentIndex;
            }
            return activeIndex.has_value() ? activeIndex : firstIndex;
        }
        static int32 toMinuteOfDay( int32 absoluteMinute ) { return absoluteMinute % kScheduleMinutesPerDay; }

        template <uint32 Capacity>
        static void appendClock( StringBuilder<Capacity>& inoutText, int32 minuteOfDay )
        {
            constexpr int32 kPadBelow = 10; // 한 자리 수는 앞에 0 을 붙여 hh:mm 두 자리로 쓴다
            const int32     hours     = minuteOfDay / kScheduleMinutesPerHour;
            const int32     minutes   = minuteOfDay % kScheduleMinutesPerHour;
            inoutText.appendFormat( "%#%#:%#%#", hours < kPadBelow ? "0" : "", hours, minutes < kPadBelow ? "0" : "", minutes );
        }
        static constexpr float32 kSamePlaceDistance = 1.0e-3f;

        static bool isSamePlace( const ScheduleLocation& lhs, const ScheduleLocation& rhs )
        {
            return float3::getDistance( lhs._position, rhs._position ) <= kSamePlaceDistance;
        }

        static int32 ceilMinutes( float32 minutes )
        {
            if ( minutes <= 0.0f )
                return 0;
            return static_cast<int32>( ::ceilf( minutes - MathUtil::kLooseEpsilon ) );
        }

        static constexpr int32 kNoWake = 0x7fffffff;

        static int32 roundUpToStep( int32 minute, int32 step )
        {
            if ( step <= 1 )
                return minute;
            return ( ( minute + step - 1 ) / step ) * step;
        }

        static constexpr size_t kNetHeaderSize = 6;  ///< 분 int32 + 수 uint16
        static constexpr size_t kNetEntrySize  = 26; ///< 자리 3 × float32 + 지역 · 활동 해시 2 × uint32 + NPC uint16 + 칸 int16 + 상태 2 × uint8

        static void writeBytes( vector<uint8>& outBytes, const void* pData, size_t size )
        {
            const uint8* pByte = static_cast<const uint8*>( pData );
            outBytes.insert( outBytes.end(), pByte, pByte + size );
        }

        template <typename T>
        static void writeValue( vector<uint8>& outBytes, const T& value )
        {
            // 엔진은 리틀 엔디언(x64 · ARM64)에서만 돈다 — 바이트 그대로가 리틀 엔디언이다.
            writeBytes( outBytes, &value, sizeof( T ) );
        }

        template <typename T>
        static T readValue( const vector<uint8>& bytes, size_t& inoutOffset )
        {
            T value{};
            ::memcpy( &value, bytes.data() + inoutOffset, sizeof( T ) );
            inoutOffset += sizeof( T );
            return value;
        }
    };
} // namespace sw
