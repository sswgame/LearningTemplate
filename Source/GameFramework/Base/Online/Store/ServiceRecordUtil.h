/**
 * @file ServiceRecordUtil.h
 * @brief 저장소 레코드 값(`ServiceRecord::_bytes`)을 `REFLECT` 타입 하나에서 읽고 씁니다. 형식은 판 머리가 붙은 자기 기술형(A, `BinarySerializer`)입니다.
 * @details 저장소 레코드는 쓴 빌드와 다른 빌드가 읽으므로 고정 배치형(B)을 쓰지 않습니다(`docs/plans/ReflectionFormats.md` 2절).
 *          필드를 더하거나 빼도 A 가 이름으로 짝을 맞추고, 뜻이 바뀌어 옛 레코드를 변환해야 하면 판을 올리고 변환(`SchemaMigrateFn`)을 붙입니다.
 *          A 는 `MaxCount` 를 보지 않으므로 상한이 중요한 값은 읽은 뒤 다시 확인합니다. 고정 배치형 코덱도 있는 타입은 `decodeWithinRange` 가 선언된 범위로,
 *          코덱이 없는 타입(지역 목록 같은 문자열 목록)은 서비스가 쓰기 때와 같은 규칙으로 확인합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

namespace sw
{
    /** @brief 저장소 레코드 값의 A 코덱입니다. @p T 는 `REFLECT_BODY()` 를 가진 타입입니다. */
    struct ServiceRecordUtil
    {
        /** @brief @p version 판 머리와 @p record 를 씁니다. */
        template <typename T>
        static vector<uint8> encode( const uint32 version, const T& record )
        {
            vector<uint8> bytes;
            BinarySerializer::serializeVersioned( version, &record, *T::StaticType(), bytes );
            return bytes;
        }

        /** @brief @p version 판의 레코드를 읽습니다. 판이 다르거나 모르는 필드가 있거나 깨졌으면 false 이고 @p outRecord 는 그대로입니다. */
        template <typename T>
        [[nodiscard]] static bool decode( const vector<uint8>& bytes, const uint32 version, T& outRecord )
        {
            T      decoded;
            uint32 readVersion = 0;
            if ( BinarySerializer::deserializeVersioned( readVersion, &decoded, *T::StaticType(), bytes.data(), bytes.size(), version ) == false )
                return false;
            outRecord = std::move( decoded );
            return true;
        }

        /**
         * @brief `decode` 한 뒤 선언된 범위(`Min` · `Max` · `MaxCount`)도 확인합니다. 범위를 벗어난 값이 있으면 false 이고 @p outRecord 는 그대로입니다.
         * @details 확인은 고정 배치형 코덱의 쓰기 검사(`FixedLayout::write`)가 맡으므로 범위의 정본은 `PROPERTY` 선언 하나입니다. 그 코덱이 있는 타입만 받습니다.
         */
        template <typename T>
        [[nodiscard]] static bool decodeWithinRange( const vector<uint8>& bytes, const uint32 version, T& outRecord )
        {
            T decoded;
            if ( decode( bytes, version, decoded ) == false )
                return false;
            BitWriter scratch;
            if ( FixedLayout::write( decoded, scratch ) == false )
                return false;
            outRecord = std::move( decoded );
            return true;
        }
    };
} // namespace sw
