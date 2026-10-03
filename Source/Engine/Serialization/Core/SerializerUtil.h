/**
 * @file SerializerUtil.h
 * @brief 직렬화기(Binary/JSON/XML/ObjectDiff/Archive)가 함께 쓰는 도우미, 트랜스코딩 엔진, 스크래치 RAII 입니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
    /**
     * @struct PresenceMaskUtil
     * @brief 적응형 밀집 비트마스크(Dense Bitmask)와 희소 인덱스(Sparse Index)를 비트 단위로 싸고 푸는 도구입니다.
     */
    struct PresenceMaskUtil
    {
        static constexpr uint8 kModeDense  = 0x01;
        static constexpr uint8 kModeSparse = 0x02;

        /** @brief 밀집 모드를 고르는 기준입니다. 기록할(수정된) 프로퍼티가 3개 이상이고 전체의 25% 이상이면 밀집 모드입니다. */
        static bool shouldUseDenseMode( size_t modifiedCount, size_t totalCount )
        {
            return ( modifiedCount >= 3 && modifiedCount * 4 >= totalCount );
        }

        /** @brief 비트마스크 바이트 수를 계산합니다: ceil(totalBits / 8) */
        static size_t computeBitmaskBytes( size_t totalBits )
        {
            return ( totalBits + 7 ) / 8;
        }

        /** @brief 비트마스크의 특정 비트를 켭니다. */
        static void setBit( uint8* pBitmask, size_t bitIndex )
        {
            pBitmask[bitIndex / 8] |= static_cast<uint8>( 1 << ( bitIndex % 8 ) );
        }

        /** @brief 비트마스크의 특정 비트가 켜져 있는지 봅니다. */
        static bool testBit( const uint8* pBitmask, size_t bitIndex )
        {
            return ( pBitmask[bitIndex / 8] & ( 1 << ( bitIndex % 8 ) ) ) != 0;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ScopedScratchInstance
     * @brief 임시 스크래치 인스턴스를 만들고, 범위를 벗어나면 반드시 파괴하는 RAII 래퍼입니다.
     */
    struct ScopedScratchInstance
    {
        explicit ScopedScratchInstance( const TypeInfo* pTypeInfo )
            : _pTypeInfo{ pTypeInfo }
            , _listStorage{}
            , _pInstance{ ( pTypeInfo != nullptr && pTypeInfo->_size > 0 ) ? createScratchInstance( *pTypeInfo, _listStorage ) : nullptr }
        {
        }

        explicit ScopedScratchInstance( const TypeInfo& typeInfo )
            : ScopedScratchInstance( &typeInfo )
        {
        }

        ~ScopedScratchInstance()
        {
            if ( _pInstance != nullptr && _pTypeInfo != nullptr )
                destroyScratchInstance( _pInstance, *_pTypeInfo );
        }

        ScopedScratchInstance( const ScopedScratchInstance& )                = delete;
        ScopedScratchInstance& operator=( const ScopedScratchInstance& )     = delete;
        ScopedScratchInstance( ScopedScratchInstance&& ) noexcept            = delete;
        ScopedScratchInstance& operator=( ScopedScratchInstance&& ) noexcept = delete;

        void* get() const { return _pInstance; }
        bool  isValid() const { return _pInstance != nullptr; }

    private:
        const TypeInfo* _pTypeInfo{ nullptr };
        vector<uint8>   _listStorage;
        void*           _pInstance{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @enum SchemaVersionSource
     * @brief 버전 번호를 **어디서 얻는지** 나타냅니다. 포맷마다 다릅니다.
     */
    enum class SchemaVersionSource : uint8
    {
        Stream, ///< 본문 앞에서 따로 읽어 옵니다(Binary). 공통 절차에 들어올 때 이미 채워져 있습니다.
        Payload ///< 본문 안의 필드입니다(JSON/XML 의 `_schemaVersion`). soft 역직렬화가 알려 줍니다.
    };

    /**
     * @brief 공통 절차가 포맷에 맡기는 **유일한 일**입니다. 본문을 읽어 값과 orphan 을 채웁니다.
     * @details 인자는 (대상 · 그 타입 · orphan 수집함 · 버전 출력)입니다. 버전을 본문에서 얻지 않는
     *          포맷(Binary)은 마지막 인자를 건드리지 않습니다.
     */
    using SoftDeserializeFn = Delegate<bool( void*, const TypeInfo&, vector<SchemaOrphanValue>&, uint32& )>;

    /**
     * @brief `deserializeVersioned` 의 **공통 절차**입니다. 세 포맷이 함께 씁니다.
     *
     * @param outVersion `SchemaVersionSource::Stream` 이면 이미 읽어 온 버전을 넣어 들어옵니다.
     *                   `Payload` 면 0 으로 들어와 이 함수가 채웁니다.
     * @param softDeserialize (대상 · 타입 · orphan 수집함 · 버전 출력) → 성공 여부.
     *                        `Stream` 포맷은 버전 출력을 건드리지 않습니다.
     * @details 하는 일: 레거시 스테이징 인스턴스 준비 → (있으면) 레거시로 soft 역직렬화 →
     *          현재 타입으로 soft 역직렬화 → 버전 확정 → `runSchemaMigrateStep`.
     */
    SW_API bool runVersionedDeserialize( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                         uint32 currentVersion, SchemaMigrateFn migrate,
                                         const TypeInfo* pLegacyTypeInfo, const SerializeContext& ctx,
                                         SchemaVersionSource versionSource, SchemaOrphanPolicy orphanPolicy,
                                         const SoftDeserializeFn& softDeserialize );

    /** @brief 직렬화기 TU 들이 함께 쓰는 도우미입니다. */
    struct SerializerUtil
    {
        /**
         * @brief 값을 바이너리로 직렬화합니다(이 빌드의 판 — `kCurrentBinaryWireVersion`).
         * @details enum 은 **열거자 정체성**으로 적습니다 — 값이 아니라 열거자 이름의 해시(uint32), 비트플래그는 켜진 이름의 수(uint32)와 그 해시들
         *          (해시 오름차순 — 같은 값이면 같은 바이트). 이름이 없는 값 · 이름이 덮지 못한 비트는 해시 자리에 0 을 두고 int64 값을 잇습니다.
         *          XML · JSON 이 이름을 적는 것과 같은 규칙이라, 열거자 순서를 바꾸거나 사이에 넣어도 저장된 뜻이 그대로입니다.
         */
        SW_API static void serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
                                                 vector<uint8>& listBuffer, const SerializeContext& ctx );
        /**
         * @brief 바이너리에서 값을 역직렬화합니다.
         * @param wireVersion 바이트가 적힌 판입니다(스트림 머리가 말한다). 지금은 판이 하나라 갈림이 없고, 판을 올리면 여기서 갈린다.
         *                    기본값은 이 빌드가 방금 쓴 바이트(복사 · 비교 · diff)에만 맞습니다.
         * @details 모르는 열거자 이름(지웠거나 ValueAlias 없이 이름을 바꿨다)이면 그 칸은 지금 값을 지키고 false 입니다 — XML 의 모르는 이름과 같다.
         *          바이트는 끝까지 읽었으므로 부른 쪽이 다음 칸으로 갈 수 있습니다(경고는 enum · 이름마다 한 번).
         */
        [[nodiscard]] SW_API static bool deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
                                                                 const uint8* pData, size_t dataSize, size_t& offset,
                                                                 const SerializeContext& ctx,
                                                                 BinaryWireVersion       wireVersion = kCurrentBinaryWireVersion );

        /** @brief 중첩 컨테이너를 바이너리로 직렬화합니다. */
        SW_API static void serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
                                                           vector<uint8>& listBuffer, const SerializeContext& ctx );
        /** @brief 바이너리에서 중첩 컨테이너를 역직렬화합니다. 원소 · 키 · 값은 `wireVersion` 으로 읽습니다(`deserializeValueBinary`). */
        [[nodiscard]] SW_API static bool deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
                                                                           const uint8* pData, size_t dataSize, size_t& offset,
                                                                           const SerializeContext& ctx,
                                                                           BinaryWireVersion       wireVersion = kCurrentBinaryWireVersion );

        /** @brief 값을 텍스트로 씁니다(중첩 타입은 중첩 문자열). 바깥 따옴표는 붙이지 않습니다. XML · JSON · SchemaMigrate 가 함께 쓰는 기본 조각입니다. */
        static void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
                                 const SerializeContext& ctx );

        /** @brief 텍스트 토큰을 값으로 파싱합니다. */
        [[nodiscard]] static bool parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
                                                  const SerializeContext& ctx );

        /**
         * @brief 프로퍼티에 선언된 기본값(PROPERTY `Default=`)이 있으면 적용합니다. 없으면 아무것도 하지 않습니다.
         * @details "선언된 기본값을 읽지 못함"(코드 결함)은 경고합니다 — 그 필드는 지금 값을 지킵니다. 값은 `applyPropertyText` 로 씁니다
         *          (비트필드는 그 비트만 — 값 주소에 bool 을 통째로 쓰면 같은 바이트의 다른 플래그까지 지운다).
         */
        static void applyPropertyDefault( const PropertyInfo& prop, void* pInstance, const SerializeContext& ctx );

        // ------------------------------------------------------------------------------
        // 프로퍼티 값 하나 — 비트필드 · 컨테이너 · 값의 세 갈래를 한 벌로. 직렬화기 셋 · 프리팹 오버라이드 도구 · 인스펙터가 함께 쓴다
        // (비트필드를 바이트째 견주고 옮기면 같은 바이트의 다른 플래그까지 바뀐다).
        // ------------------------------------------------------------------------------
        /** @brief 한 인스턴스의 프로퍼티 값을 다른 인스턴스로 옮깁니다(같은 타입). 비트필드는 그 비트만, 컨테이너는 원소째 옮깁니다. */
        [[nodiscard]] SW_API static bool copyPropertyValue( const PropertyInfo& prop, const void* pSrcInstance, void* pDstInstance, const SerializeContext& ctx );
        /** @brief 두 인스턴스의 프로퍼티 값이 같은지 봅니다. 비트필드는 그 비트만 견줍니다. */
        SW_API static bool arePropertyValuesEqual( const PropertyInfo& prop, const void* pInstanceA, const void* pInstanceB, const SerializeContext& ctx );
        /** @brief 글 하나를 프로퍼티 값으로 씁니다(비트필드는 그 비트만). 못 읽으면 false 이고 값은 그대로입니다. 컨테이너는 받지 않습니다. */
        [[nodiscard]] SW_API static bool applyPropertyText( const PropertyInfo& prop, void* pInstance, string_view text, const SerializeContext& ctx );
        /**
         * @brief 이 프로퍼티의 값을 세 형식(XML · JSON · 바이너리)이 모두 실어 나를 수 있으면 true 입니다 — 직렬화기의 분기와 같은 판정입니다.
         * @details 직렬화기는 다룰 줄 모르는 타입을 **조용히** 텍스트 `null` · 바이너리 0 바이트로 쓰고, 읽을 때는 그 칸이 기본값이 된다(저장한 줄 알았던
         *          값이 사라진다). 컨테이너는 원소 · 키 타입을, 소유 포인터 원소는 런타임 팩토리를 믿습니다. 모든 PROPERTY 를 이것으로 훑는 시험이 있다.
         */
        SW_API static bool canCarryProperty( const PropertyInfo& prop, const SerializeContext& ctx );
        /** @brief 값 타입 하나를 세 형식이 모두 실어 나를 수 있으면 true 입니다(글 처리기 짝 · enum · 반사 구조체). */
        SW_API static bool canCarryValueType( hashed_string typeName, const SerializeContext& ctx );

        /** @brief 프로퍼티 값을 사람이 읽는 글로 씁니다(XML 속성 값과 같은 꼴). 비트필드는 `true` · `false`, 컨테이너는 `[n]`(원소 수)입니다. */
        SW_API static string formatPropertyText( const PropertyInfo& prop, const void* pInstance, const SerializeContext& ctx );

        /** @brief 키 문자열이 같은지 비교합니다(대소문자 무시 옵션 지원). */
        static bool keysEqual( string_view left, string_view right, bool bIgnoreCase );

        /**
         * @brief Alias 로 들어온 이름을 `SerializeContext` 핸들러가 아는 정본 이름(`_name`)으로 바꿉니다.
         * @details `SerializerUtil` · `JsonSerializer` 가 함께 쓰는 핸들러 조회 규칙입니다.
         */
        SW_API static hashed_string resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& ctx );

        /**
         * @brief 이 타입 이름이 **중첩 객체**(프로퍼티를 풀어 쓰는 REFLECT 타입)면 그 TypeInfo 를, 아니면 nullptr 를 반환합니다.
         * @details 텍스트 핸들러가 있거나(값 한 줄로 씁니다) enum 이거나 기본형이면 중첩 객체가 아닙니다. `JsonSerializer` 와
         *          `XmlSerializer` 가 함께 씁니다 — 한쪽만 규칙이 바뀌면 두 포맷이 같은 필드를 다르게 씁니다.
         */
        SW_API static const TypeInfo* findNestedObjectType( hashed_string typeName, const SerializeContext& ctx );

        /**
         * @brief 원소 타입 이름이 **소유 포인터**인지 봅니다(이름에 `*` 가 있으면 그렇습니다).
         * @details `JsonSerializer` 와 `XmlSerializer` 가 함께 씁니다. 판정 기준이
         *          "이름에 별표가 있는가" 라는 문자열 규칙이라, 한쪽만 고치면 두 포맷이 서로 다른
         *          컨테이너를 소유로 보게 됩니다.
         */
        SW_API static bool isOwnedPointerElementType( hashed_string elementTypeName );

        /** @brief 프로퍼티 목록에서 키에 맞는 프로퍼티 메타데이터를 찾습니다. */
        static const PropertyInfo* matchProperty( const vector<PropertyInfo>& listProp, string_view keyRaw,
                                                  bool bIgnoreCaseKeys, bool& bCaseVariant );

        /** @brief 프로퍼티가 직렬화 대상인지 검사합니다(Transient 제외). */
        static bool shouldSerializeProperty( const PropertyInfo& prop )
        {
            return prop._metadata._bTransient == SW_FALSE;
        }

        /** @brief JSON 문자열을 바이너리 버퍼로 트랜스코딩합니다. */
        SW_API static bool transcodeJsonToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                  const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 바이너리 데이터를 JSON 문자열로 트랜스코딩합니다. */
        SW_API static string transcodeBinaryToJson( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty = false,
                                                    const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief XML 문자열을 바이너리 버퍼로 트랜스코딩합니다. */
        SW_API static bool transcodeXmlToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                 const SerializeContext& ctx = SerializeContext::getDefault() );

        /** @brief 바이너리 데이터를 XML 문자열로 트랜스코딩합니다. */
        SW_API static string transcodeBinaryToXml( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                   const SerializeContext& ctx = SerializeContext::getDefault() );
    };
} // namespace sw
