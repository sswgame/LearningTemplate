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
     * @enum SchemaOrphanPolicy
     * @brief 버전은 같은데 **모르는 필드(orphan)만** 있을 때 migrate 없이 통과시킬지 정합니다.
     *
     * @details **포맷마다 답이 다르고, 그것이 의도인지 사고인지 오래 분명하지 않았습니다.** 셋이 같은 스무 줄을
     *          각자 복사해 갖고 있었고 이 판단만 슬쩍 달랐습니다. Binary 는 거절하고 JSON · XML 은 조용히
     *          통과시킵니다. 실측으로 확인한 사실입니다(`ReflectionSerializationTest` 의
     *          `OrphanOnlyPolicyDiffersByFormat`). 어느 쪽이 옳은지는 정하지 않고, **동작을 바꾸지 않은 채** 이름을
     *          붙여 부르는 쪽에 드러냈습니다. 텍스트를 엄격하게 바꾸면 모르는 필드가 하나만 있어도 씬 · 프리팹이
     *          통째로 로드에 실패합니다. 바꿀 값이 있는지는 백로그에 질문으로 남겼습니다.
     */
    enum class SchemaOrphanPolicy : uint8
    {
        Ignore, ///< orphan 은 버리고 성공으로 봅니다(JSON/XML. 손으로 고치는 파일이라 관대합니다).
        Reject  ///< orphan 이 있으면 migrate 없이는 실패합니다(Binary. 스키마가 바뀐 것이 확실합니다).
    };

    /**
     * @brief 공통 절차가 포맷에 맡기는 **유일한 일**입니다. 본문을 읽어 값과 orphan 을 채웁니다.
     * @details 인자는 (대상 · 그 타입 · orphan 수집함 · 버전 출력)입니다. 버전을 본문에서 얻지 않는
     *          포맷(Binary)은 마지막 인자를 건드리지 않습니다.
     */
    using SoftDeserializeFn = Delegate<bool( void*, const TypeInfo&, vector<SchemaOrphanValue>&, uint32& )>;

    /**
     * @brief `deserializeVersioned` 의 **공통 절차**입니다. 세 포맷에 글자까지 같게 있던 스무 줄입니다.
     *
     * @param outVersion `SchemaVersionSource::Stream` 이면 이미 읽어 온 버전을 넣어 들어옵니다.
     *                   `Payload` 면 0 으로 들어와 이 함수가 채웁니다.
     * @param softDeserialize (대상 · 타입 · orphan 수집함 · 버전 출력) → 성공 여부.
     *                        `Stream` 포맷은 버전 출력을 건드리지 않습니다.
     * @details 하는 일: 레거시 스테이징 인스턴스 준비 → (있으면) 레거시로 soft 역직렬화 →
     *          현재 타입으로 soft 역직렬화 → 버전 확정 → `runSchemaMigrateStep`.
     *          예전에는 이 절차가 JSON · XML 에 **주석까지 똑같이** 복사돼 있었고 Binary 에 한 벌 더
     *          있었습니다. 한쪽을 고치면 다른 쪽은 그대로인 구조였습니다.
     */
    SW_API bool runVersionedDeserialize( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                         uint32 currentVersion, SchemaMigrateFn migrate,
                                         const TypeInfo* pLegacyTypeInfo, const SerializeContext& ctx,
                                         SchemaVersionSource versionSource, SchemaOrphanPolicy orphanPolicy,
                                         const SoftDeserializeFn& softDeserialize );

    /** @brief 직렬화기 TU 들이 함께 쓰는 도우미입니다. */
    struct SerializerUtil
    {
        /** @brief 값을 바이너리로 직렬화합니다. */
        SW_API static void serializeValueBinary( const void* pValuePtr, const hashed_string& typeName,
                                                 vector<uint8>& listBuffer, const SerializeContext& ctx );
        /** @brief 바이너리에서 값을 역직렬화합니다. */
        SW_API static bool deserializeValueBinary( void* pValuePtr, const hashed_string& typeName,
                                                   const uint8* pData, size_t dataSize, size_t& offset,
                                                   const SerializeContext& ctx );

        /** @brief 중첩 컨테이너를 바이너리로 직렬화합니다. */
        SW_API static void serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
                                                           vector<uint8>& listBuffer, const SerializeContext& ctx );
        /** @brief 바이너리에서 중첩 컨테이너를 역직렬화합니다. */
        SW_API static bool deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
                                                             const uint8* pData, size_t dataSize, size_t& offset,
                                                             const SerializeContext& ctx );

        /** @brief 값을 텍스트로 씁니다(중첩 타입은 중첩 문자열). 바깥 따옴표는 붙이지 않습니다. XML · JSON · SchemaMigrate 가 함께 쓰는 기본 조각입니다. */
        static void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const void* pValPtr, const hashed_string& typeName,
                                 const SerializeContext& ctx );

        /** @brief 텍스트 토큰을 값으로 파싱합니다. */
        static bool parseTextValue( void* pValPtr, const hashed_string& typeName, string_view valStr,
                                    const SerializeContext& ctx );

        /** @brief 프로퍼티 기본값을 적용합니다. */
        static bool applyPropertyDefault( void* pPropPtr, const PropertyInfo& prop, const SerializeContext& ctx );

        /** @brief 컨테이너 TypeInfo 이름을 태그로 바꿉니다(`vector`, `map`). */
        static const utf8* containerTypeTagName( hashed_string typeName );

        /** @brief 키 문자열이 같은지 비교합니다(대소문자 무시 옵션 지원). */
        static bool keysEqual( string_view left, string_view right, bool bIgnoreCase );

        /**
         * @brief Alias · 옛 이름을 `SerializeContext` 핸들러가 아는 정본 이름(`_name`)으로 바꿉니다.
         * @details 두 TU(`SerializerUtil` · `JsonSerializer`)가 같은 열세 줄을 각자 들고 있었습니다.
         *          핸들러 조회 규칙이 바뀌면 모두를 같이 고쳐야 했습니다.
         */
        SW_API static hashed_string resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& ctx );

        /**
         * @brief 이 타입 이름이 **중첩 객체**(프로퍼티를 풀어 쓰는 REFLECT 타입)면 그 TypeInfo 를, 아니면 nullptr 를 반환합니다.
         * @details 텍스트 핸들러가 있거나(값 한 줄로 씁니다) enum 이거나 기본형이면 중첩 객체가 아닙니다. `JsonSerializer` 와
         *          `XmlSerializer` 가 같은 함수를 각자 들고 있었습니다. 한쪽만 규칙이 바뀌면 두 포맷이 같은 필드를 다르게 씁니다.
         */
        SW_API static const TypeInfo* findNestedObjectType( hashed_string typeName, const SerializeContext& ctx );

        /**
         * @brief 원소 타입 이름이 **소유 포인터**인지 봅니다(이름에 `*` 가 있으면 그렇습니다).
         * @details `JsonSerializer` 와 `XmlSerializer` 가 같은 함수를 각자 들고 있었습니다. 판정 기준이
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
