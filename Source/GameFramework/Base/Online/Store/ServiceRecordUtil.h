/**
 * @file ServiceRecordUtil.h
 * @brief 저장소 레코드 값(`ServiceRecord::_bytes`)을 `REFLECT` 타입 하나에서 읽고 씁니다. 형식은 판 머리가 붙은 자기 기술형(A, `BinarySerializer`)입니다.
 * @details 저장소 레코드는 쓴 빌드와 다른 빌드가 읽으므로 고정 배치형(B)을 쓰지 않습니다(`docs/plans/ReflectionFormats.md` 2절).
 *          A 는 필드를 이름으로 짝짓습니다. 필드를 더하면 옛 레코드의 그 필드는 타입의 기본값으로 채워집니다. 필드를 빼면 옛 레코드에 남은 그 필드가
 *          모르는 필드(orphan)가 되고, 이 읽기는 orphan 을 거절하므로 옛 레코드 전부가 읽기 실패입니다. 필드를 빼거나 뜻을 바꿀 때는 판을 올리고
 *          데이터를 다시 씁니다. 값 계산이 불가피한 예외만 그 판의 이관을 `SW_SCHEMA_MIGRATION` 으로 등록합니다(결정 기록 5-4).
 *          못 읽은 레코드를 어떻게 할지는 레코드마다 손으로 정하지 않고 읽기 함수가 정합니다. 영속 레코드는 `readDurable` 로 읽어 손상이면
 *          오류 로그를 남기고 호출부가 실패를 돌려줍니다. 다시 만들 수 있는 캐시는 `readCache` 로 읽어 없는 것으로 봅니다.
 *          A 는 `MaxCount` 를 보지 않으므로 두 읽기는 고정 배치형 코덱이 있는 타입이면 언제나 선언된 범위(`Min` · `Max` · `MaxCount`)도 확인합니다.
 *          코덱이 없는 타입(지역 목록 같은 문자열 목록)은 서비스가 쓰기 때와 같은 규칙으로 확인합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{
    /** @brief 저장소 레코드 값의 A 코덱입니다. @p T 는 `REFLECT_BODY()` 를 가진 타입입니다. */
    struct ServiceRecordUtil
    {
        /**
         * @brief @p version 판 머리와 @p record 를 씁니다.
         * @details 쓰기 실패를 아직 돌려주지 못합니다(백로그 `A-17`). 레코드 타입은 모두 실을 수 있는 필드만 가지므로 실패는 코드 결함이고, Debug 에서 단언으로 멈춥니다.
         */
        template <typename T>
        static vector<uint8> encode( const uint32 version, const T& record )
        {
            vector<uint8>               bytes;
            [[maybe_unused]] const bool bWritten = BinarySerializer::serializeVersioned( version, &record, *T::StaticType(), bytes );
            SW_ASSERT( bWritten );
            return bytes;
        }

        /**
         * @brief 영속 레코드(@p key)를 읽습니다. 못 읽으면 손상이므로 오류 로그를 남기고 false 이며 @p outRecord 는 그대로입니다.
         * @details false 를 받은 호출부는 레코드를 건너뛰지 않고 요청을 실패로 돌려줍니다.
         */
        template <typename T>
        [[nodiscard]] static bool readDurable( const string_view key, const vector<uint8>& bytes, const uint32 version, T& outRecord )
        {
            if ( read( bytes, version, outRecord ) )
                return true;
            SW_LOG_ERROR( "durable record %# (%#, version %#, %# bytes) is corrupt", string( key ).c_str(), T::StaticType()->_fullyQualifiedName.c_str(), version,
                          static_cast<uint64>( bytes.size() ) );
            return false;
        }

        /** @brief 다시 만들 수 있는 캐시 레코드를 읽습니다. 못 읽으면 없는 것으로 보고 false 이며 @p outRecord 는 그대로입니다. 로그는 남기지 않습니다. */
        template <typename T>
        [[nodiscard]] static bool readCache( const vector<uint8>& bytes, const uint32 version, T& outRecord )
        {
            return read( bytes, version, outRecord );
        }

    private:
        /** @brief 판이 다르거나 모르는 필드가 있거나 깨졌거나, 코덱이 있는 타입인데 선언된 범위를 벗어나면 false 입니다. */
        template <typename T>
        [[nodiscard]] static bool read( const vector<uint8>& bytes, const uint32 version, T& outRecord )
        {
            T      decoded;
            uint32 readVersion = 0;
            if ( BinarySerializer::deserializeVersioned( readVersion, &decoded, *T::StaticType(), bytes.data(), bytes.size(), version ) == false )
                return false;
            string reason;
            if ( FixedLayout::findCodec<T>( reason ) != nullptr )
            {
                // 범위 확인은 고정 배치형 코덱의 쓰기 검사가 맡으므로 범위의 정본은 PROPERTY 선언 하나다
                BitWriter scratch;
                if ( FixedLayout::write( decoded, scratch ) == false )
                    return false;
            }
            outRecord = std::move( decoded );
            return true;
        }
    };
} // namespace sw
