/**
 * @file MixedBuildCodec.h
 * @brief 배포 중에 빌드가 다른 서버가 읽는 값(서버 버스 본문, 캐시 레코드)의 자기 기술형(A) 코덱입니다.
 * @details
 *          - 버전 머리가 기대한 버전과 다르면 거절합니다. 이관 레지스트리를 보지 않습니다(`BinarySerializer::deserializeExactVersion`).
 *          - 타입에 없는 이름의 필드는 건너뛰고(한 번 경고), 타입에 있는데 읽지 못한 필드(모르는 열거자)는 거절합니다. 중첩 구조체까지 같은 정책입니다.
 *          - 읽은 값과 쓸 값이 선언된 범위(`Min`, `Max`, `MaxCount`)를 지키는지 고정 배치형 쓰기 검사로 봅니다. A 는 `MaxCount` 를 보지 않기 때문입니다.
 *            타입에 고정 배치형 코덱이 없으면 `FixedLayout::write` 가 이유와 함께 프로세스를 멈춥니다(대신 쓸 검사가 없습니다).
 *          영속 레코드는 이 코덱을 쓰지 않습니다. 옛 빌드가 모르는 필드를 버리고 다시 쓰면 새 빌드의 값이 지워지기 때문입니다(`ServiceRecordUtil::readDurable`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief `MixedBuildCodec::decode` 의 결과입니다. */
    enum class [[nodiscard]] MixedBuildReadResult : uint8
    {
        Read,
        VersionMismatch,
        Unreadable,
        OutOfRange
    };
} // namespace sw

namespace sw
{
    /** @struct MixedBuildCodec @brief 혼합 빌드 사이의 A 코덱입니다. @p T 는 `REFLECT_BODY()` 와 고정 배치형 코덱을 가진 타입입니다. */
    struct SW_GF_API MixedBuildCodec
    {
        /** @brief 모르는 이름만 건너뛰는 읽기 컨텍스트입니다(`setSkipUnknownProperties( true )`, `setSkipUnreadableProperties( false )`). */
        static const SerializeContext& getReadContext();

        /** @brief @p version 머리와 @p value 를 @p outBytes 에 씁니다. 범위 밖이거나 실을 수 없는 필드가 있으면 오류 로그를 남기고 false 이며 @p outBytes 는 비어 있습니다. */
        template <typename T>
        [[nodiscard]] static bool encode( const uint32 version, const T& value, vector<uint8>& outBytes )
        {
            outBytes.clear();
            BitWriter  scratch;
            const bool bInRange = FixedLayout::write( value, scratch );
            if ( bInRange && BinarySerializer::serializeVersioned( version, &value, *T::StaticType(), outBytes ) )
                return true;
            outBytes.clear();
            logEncodeFailure( T::StaticType()->_fullyQualifiedName, version, bInRange );
            return false;
        }

        /** @brief @p pData 를 읽습니다. `Read` 일 때만 @p outValue 를 바꿉니다. 로그는 부르는 쪽이 정합니다(받은 바이트입니다). */
        template <typename T>
        static MixedBuildReadResult decode( const uint8* pData, const size_t dataSize, const uint32 expectedVersion, T& outValue, uint32& outReadVersion )
        {
            T decoded{};
            switch ( BinarySerializer::deserializeExactVersion( expectedVersion, &decoded, *T::StaticType(), pData, dataSize, outReadVersion, getReadContext() ) )
            {
                case BinaryExactVersionResult::Read:
                    break;
                case BinaryExactVersionResult::VersionMismatch:
                    return MixedBuildReadResult::VersionMismatch;
                case BinaryExactVersionResult::Unreadable:
                    return MixedBuildReadResult::Unreadable;
            }
            BitWriter scratch;
            if ( FixedLayout::write( decoded, scratch ) == false )
                return MixedBuildReadResult::OutOfRange;
            outValue = std::move( decoded );
            return MixedBuildReadResult::Read;
        }

    private:
        static void logEncodeFailure( const hashed_string& typeFqn, uint32 version, bool bInRange );
    };
} // namespace sw
