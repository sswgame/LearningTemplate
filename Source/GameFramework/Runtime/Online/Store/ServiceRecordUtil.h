/**
 * @file ServiceRecordUtil.h
 * @brief 저장소 레코드 값(`ServiceRecord::_bytes`)을 `REFLECT` 타입 하나에서 읽고 씁니다. 형식은 버전 헤더가 붙은 자기 기술형(A, `BinarySerializer`)입니다.
 * @details 저장소 레코드는 쓴 빌드와 다른 빌드가 읽으므로 고정 배치형(B)을 쓰지 않습니다(`Source/Engine/Serialization/Fixed/README.md` 2절).
 *          **판은 레코드 타입 한 곳이다** — `REFLECT( Version = N )`(없으면 1, `TypeMetadata::getVersion`). 호출부가 판 상수를 따로 넘기지 않는다(그릇마다 판을 두면
 *          같은 타입을 싣는 두 곳이 판을 따로 올려 한쪽이 옛 판을 새 판으로 읽는다).
 *          A 는 필드를 이름으로 짝짓습니다. 필드를 더하면 옛 레코드의 그 필드는 타입의 기본값으로 채워집니다. 필드를 빼면 옛 레코드에 남은 그 필드는 이 빌드가 모르는 칸이 되고,
 *          영속 레코드는 그 칸을 들어(`_unknownProperties`) 다시 쓸 때 그대로 싣는다 — 판을 올리지 않아도 읽힌다. 칸의 **뜻**을 바꿀 때만 판을 올리고,
 *          옛 판 레코드는 그 타입에 등록한 패치 사슬(`SW_TYPE_VERSION_PATCH` — 상태 바이트의 `StateIO::reflected` 와 같은 사슬)이 올린 뒤 읽는다.
 *          패치는 옛 모양 타입에서 새 모양 타입으로 옮기는 함수 하나로 쓴다(`upgradeRecordBody`). 단계가 빠졌거나 새 판(내리기)이면 손상이다.
 *          못 읽은 레코드를 어떻게 할지는 레코드마다 손으로 정하지 않고 읽기 함수가 정합니다. 영속 레코드는 `readDurable` 로 읽어 손상이면
 *          오류 로그를 남기고 호출부가 실패를 돌려줍니다. 다시 만들 수 있는 캐시는 `readCache` 로 읽어 없는 것으로 봅니다(판이 다르면 사슬 없이 버리고 다시 만든다).
 *          캐시는 배포 중에 빌드가 다른 서버가 읽으므로 서버 버스와 같은 읽기(`MixedBuildCodec::decode`)입니다. 버전이 같으면 모르는 이름의 필드는 건너뛰고,
 *          읽지 못한 아는 필드(모르는 열거자)와 다른 버전은 없는 것으로 봅니다. 영속 레코드는 옛 빌드가 모르는 필드를 버리고 다시 쓰면 새 빌드의 값이 지워지므로
 *          **레코드가 모르는 칸을 든다**: 영속 레코드 타입은 `ServiceRecordUnknownFields _unknownProperties{};`(PROPERTY 가 아닌 칸)를 가져야 하고(`readDurable` 의 static_assert),
 *          `readDurable` 이 같은 버전의 모르는 칸을 거기 담고 `encode` 가 그대로 다시 싣는다. 레코드를 복사해 고쳐 쓰는 서비스 코드는 손대지 않아도 칸이 따라간다.
 *          A 는 `MaxCount` 를 보지 않으므로 쓰기와 두 읽기는 고정 배치형 코덱이 있는 타입이면 언제나 선언된 범위(`Min`, `Max`, `MaxCount`)도 확인합니다.
 *          코덱이 없는 타입(지역 목록 같은 문자열 목록)은 서비스가 쓰기 때와 같은 규칙으로 확인합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

#include "GameFramework/Runtime/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief @p T 가 모르는 칸을 드는 레코드(`ServiceRecordUnknownFields _unknownProperties`)이면 true 입니다. */
    template <typename T, typename = void>
    struct ServiceRecordKeepsUnknown : std::false_type
    {
    };

    template <typename T>
    struct ServiceRecordKeepsUnknown<T, std::void_t<decltype( std::declval<T&>()._unknownProperties )>>
        : std::is_same<decltype( std::declval<T&>()._unknownProperties ), ServiceRecordUnknownFields>
    {
    };
} // namespace sw

namespace sw
{
    /** @brief 저장소 레코드 값의 A 코덱입니다. @p T 는 `REFLECT_BODY()` 를 가진 타입이고 판은 그 타입의 `REFLECT( Version )` 입니다. */
    struct ServiceRecordUtil
    {
        /** @brief @p T 의 레코드 판입니다(`REFLECT( Version = N )`, 없으면 1). */
        template <typename T>
        [[nodiscard]] static uint32 getVersion()
        {
            return T::StaticType()->_metadata->getVersion();
        }

        /**
         * @brief 판 머리와 @p record 를 @p outBytes 에 씁니다. 못 쓰면 오류 로그를 남기고 false 이며 @p outBytes 는 비어 있습니다.
         * @details 읽기(`readDurable`)가 거절할 레코드는 쓰지 않습니다. 실을 수 없는 필드가 있거나, 코덱이 있는 타입인데 선언된 범위를 벗어나면 실패입니다.
         *          쓰고 나서 읽지 못하는 영속 레코드는 손상과 구별되지 않기 때문입니다. false 를 받은 호출부는 저장소 작업을 하지 않고 요청을 실패로 돌려줍니다.
         */
        template <typename T>
        [[nodiscard]] static bool encode( const T& record, vector<uint8>& outBytes )
        {
            const uint32 version = getVersion<T>();
            outBytes.clear();
            BitWriter  scratch;
            const bool bInRange = FixedLayout::findCodec<T>().has_value() == false || FixedLayout::write( record, scratch );
            bool       bWritten = false;
            // 모르는 칸을 드는 레코드는 읽을 때 담은 칸을 받은 그대로 뒤에 싣는다(옛 빌드가 고쳐 써도 새 빌드의 값이 남는다).
            if constexpr ( ServiceRecordKeepsUnknown<T>::value )
                bWritten = bInRange && BinarySerializer::serializeVersionedKeepingUnknown( version, &record, *T::StaticType(), record._unknownProperties._listField, outBytes );
            else
                bWritten = bInRange && BinarySerializer::serializeVersioned( version, &record, *T::StaticType(), outBytes );
            if ( bWritten )
                return true;
            outBytes.clear();
            SW_LOG_ERROR_ID( "ServiceRecordUtil.RecordVersionNotWritten", "record %# (version %#) is not written: %#", T::StaticType()->_fullyQualifiedName.c_str(), version,
                             bInRange ? "a field has no binary handler" : "a value is out of its declared range" );
            return false;
        }

        /**
         * @brief 영속 레코드(@p key)를 읽습니다. 못 읽으면 손상이므로 오류 로그를 남기고 false 이며 @p outRecord 는 그대로입니다.
         * @details 옛 판이면 그 타입의 패치 사슬(`SW_TYPE_VERSION_PATCH`)로 올린 뒤 읽는다. 지금 판이면 이 빌드가 모르는 칸을 거절하지 않고 `outRecord._unknownProperties` 에 담는다
         *          (`encode` 가 다시 싣는다). 아는 칸을 읽지 못했거나(모르는 열거자) 선언된 범위를 벗어나거나, 새 판 · 단계가 빠진 옛 판이면 손상이다.
         *          false 를 받은 호출부는 레코드를 건너뛰지 않고 요청을 실패로 돌려줍니다.
         */
        template <typename T>
        [[nodiscard]] static bool readDurable( const string_view key, const vector<uint8>& bytes, T& outRecord )
        {
            static_assert( ServiceRecordKeepsUnknown<T>::value,
                           "a durable record keeps the fields this build does not know - add `ServiceRecordUnknownFields _unknownProperties{};` (not a PROPERTY) to the record type" );
            const uint32         version = getVersion<T>();
            vector<uint8>        upgraded;
            const vector<uint8>* pBytes      = &bytes;
            uint32               readVersion = 0;
            if ( readVersionHeader( bytes, readVersion ) && readVersion != version && upgradeRecord( *T::StaticType(), bytes, readVersion, version, upgraded ) )
                pBytes = &upgraded;
            T decoded;
            if ( readKeepingUnknown( *pBytes, version, decoded, decoded._unknownProperties._listField ) )
            {
                outRecord = std::move( decoded );
                return true;
            }
            SW_LOG_ERROR_ID( "ServiceRecordUtil.DurableRecordVersionBytes", "durable record %# (%#, version %#, %# bytes) is corrupt", string( key ).c_str(), T::StaticType()->_fullyQualifiedName.c_str(), version,
                             static_cast<uint64>( bytes.size() ) );
            return false;
        }

        /** @brief 다시 만들 수 있는 캐시 레코드를 읽습니다(혼합 빌드 읽기). 못 읽으면 없는 것으로 보고 false 이며 @p outRecord 는 그대로입니다. 로그는 남기지 않습니다. */
        template <typename T>
        [[nodiscard]] static bool readCache( const vector<uint8>& bytes, T& outRecord )
        {
            uint32 readVersion = 0;
            return MixedBuildCodec::decode( bytes.data(), bytes.size(), getVersion<T>(), outRecord, readVersion ) == MixedBuildReadResult::Read;
        }

        /**
         * @brief 레코드 패치 한 단계의 본문입니다 — 옛 판 본문을 옛 모양 타입 @p TOld 로 읽어 @p Convert 로 새 모양 @p TNew 에 옮겨 씁니다. `SW_TYPE_VERSION_PATCH` 에 넘깁니다.
         * @details 옛 모양 타입은 그 판의 칸을 그대로 든 `REFLECT` 타입이다(ezEngine GraphPatch 가 옛 그래프를 고치는 것을 타입 둘 사이의 함수로 둔 자리).
         *          옛 본문에 옛 모양이 모르는 칸이 있으면(그 판을 쓴 빌드의 칸 — 패치가 잃는다) 실패다.
         */
        template <typename TOld, typename TNew, bool ( *Convert )( const TOld&, TNew& )>
        [[nodiscard]] static bool upgradeRecordBody( Archive& inOldBody, Archive& outNewBody )
        {
            const uint64              size  = inOldBody.getRemainingBytes();
            const uint8*              pData = inOldBody.readBytesView( size );
            TOld                      oldRecord{};
            vector<SchemaOrphanValue> listOrphan;
            if ( pData == nullptr || BinarySerializer::deserializeSoft( &oldRecord, *TOld::StaticType(), pData, static_cast<size_t>( size ), &listOrphan ) != SoftReadResult::Read ||
                 listOrphan.empty() == false )
                return false;
            TNew newRecord{};
            if ( Convert( oldRecord, newRecord ) == false )
                return false;
            vector<uint8> newBody;
            if ( BinarySerializer::serialize( &newRecord, *TNew::StaticType(), newBody ) == false )
                return false;
            outNewBody.writeBytes( newBody.data(), newBody.size() );
            return true;
        }

    private:
        /** @brief 레코드 머리의 판을 읽습니다(`BinarySerializer::serializeVersioned` 가 쓴 머리와 같은 배치). 머리가 잘렸으면 false 입니다. */
        static bool readVersionHeader( const vector<uint8>& bytes, uint32& outVersion )
        {
            if ( bytes.size() < sizeof( uint32 ) )
                return false;
            Memory::copy( &outVersion, bytes.data(), sizeof( uint32 ) );
            return true;
        }

        /** @brief 레코드 머리의 버전이 @p version 이면 true 입니다. */
        static bool isVersion( const vector<uint8>& bytes, const uint32 version )
        {
            uint32 readVersion = 0;
            return readVersionHeader( bytes, readVersion ) && readVersion == version;
        }

        /**
         * @brief 판 @p fromVersion 레코드 @p bytes 를 타입 @p type 의 패치 사슬로 판 @p toVersion 까지 올려 @p outBytes(머리 포함)에 담습니다.
         * @return 단계가 빠졌거나 실패했거나 새 판이면(사슬이 오류를 남긴다) false 입니다.
         */
        static bool upgradeRecord( const TypeInfo& type, const vector<uint8>& bytes, const uint32 fromVersion, const uint32 toVersion, vector<uint8>& outBytes )
        {
            vector<uint8>        body( bytes.begin() + sizeof( uint32 ), bytes.end() );
            FormatUpgradePayload payload{};
            payload._pBytes = &body;
            if ( FormatUpgradeStep::upgrade( FormatUpgradeDomain::ReflectedType, type._fullyQualifiedName.c_str(), fromVersion, toVersion, payload ) == false )
                return false;
            outBytes.resize( sizeof( uint32 ) + body.size() );
            Memory::copy( outBytes.data(), &toVersion, sizeof( uint32 ) );
            if ( body.empty() == false )
                Memory::copy( outBytes.data() + sizeof( uint32 ), body.data(), body.size() );
            return true;
        }

        /** @brief 버전 머리가 @p version 과 같고, 아는 칸을 모두 읽었고, 선언된 범위 안이면 true 입니다. 모르는 칸(이름이 빈 orphan)은 @p outListUnknown 에 담습니다. */
        template <typename T>
        [[nodiscard]] static bool readKeepingUnknown( const vector<uint8>& bytes, const uint32 version, T& outRecord, vector<SchemaOrphanValue>& outListUnknown )
        {
            constexpr size_t kVersionSize = sizeof( uint32 );
            if ( bytes.size() <= kVersionSize || isVersion( bytes, version ) == false )
                return false;
            const SoftReadResult result = BinarySerializer::deserializeSoft( &outRecord, *T::StaticType(), bytes.data() + kVersionSize, bytes.size() - kVersionSize, &outListUnknown );
            if ( result != SoftReadResult::Read )
                return false;
            // 이름이 있는 orphan 은 이 빌드가 아는 칸인데 읽지 못한 것이다(모르는 열거자) — 담아 다시 쓰면 그 칸의 제 값이 사라진다. 손상으로 본다.
            for ( const SchemaOrphanValue& orphan : outListUnknown )
            {
                if ( orphan._name.empty() == false )
                    return false;
            }
            return isWithinDeclaredRange( outRecord );
        }

        /** @brief 고정 배치형 코덱이 있는 타입이면 선언된 범위(`Min`, `Max`, `MaxCount`) 안인가입니다. 코덱이 없는 타입은 서비스가 확인하므로 true 입니다. */
        template <typename T>
        [[nodiscard]] static bool isWithinDeclaredRange( const T& record )
        {
            if ( FixedLayout::findCodec<T>().has_value() == false )
                return true;
            // 범위 확인은 고정 배치형 코덱의 쓰기 검사가 맡으므로 범위의 원본은 PROPERTY 선언 하나다
            BitWriter scratch;
            return FixedLayout::write( record, scratch );
        }
    };
} // namespace sw
