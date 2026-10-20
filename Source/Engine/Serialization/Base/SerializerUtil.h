/**
 * @file SerializerUtil.h
 * @brief 직렬화기(Binary, JSON, XML, ObjectDiff, Archive)가 함께 쓰는 도우미, 트랜스코딩 엔진, 스크래치 RAII 입니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/TextAddress.h"

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
     * @details 인자는 (대상, 그 타입, orphan 수집함, 버전 출력)입니다. 버전을 본문에서 얻지 않는
     *          포맷(Binary)은 마지막 인자를 건드리지 않습니다.
     */
    using SoftDeserializeFn = Delegate<bool( void*, const TypeInfo&, vector<SchemaOrphanValue>&, uint32& )>;

    /**
     * @brief `deserializeVersioned` 의 **공통 절차**입니다. 세 포맷이 함께 씁니다.
     *
     * @param outVersion `SchemaVersionSource::Stream` 이면 이미 읽어 온 버전을 넣어 들어옵니다.
     *                   `Payload` 면 0 으로 들어와 이 함수가 채웁니다.
     * @param softDeserialize (대상, 타입, orphan 수집함, 버전 출력)을 받아 성공 여부를 반환합니다.
     *                        `Stream` 포맷은 버전 출력을 건드리지 않습니다.
     * @details 처리 순서는 다음과 같다. 레거시 스테이징 인스턴스를 준비하고, (있으면) 레거시로 soft 역직렬화한 뒤,
     *          현재 타입으로 soft 역직렬화하고, 버전을 확정한 다음 `runSchemaMigrateStep` 을 부른다.
     */
    SW_API bool runVersionedDeserialize( uint32& outVersion, void* pInstance, const TypeInfo& typeInfo,
                                         uint32          currentVersion,
                                         const TypeInfo* pLegacyTypeInfo, const SerializeContext& context,
                                         SchemaVersionSource versionSource, SchemaOrphanPolicy orphanPolicy,
                                         const SoftDeserializeFn& softDeserialize );

    /** @brief 직렬화기 TU 들이 함께 쓰는 도우미입니다. */
    struct SerializerUtil
    {
        /**
         * @brief 값을 바이너리로 직렬화합니다(이 빌드의 버전, `kCurrentBinaryWireVersion`). @p value 는 값 하나 자체의 주소입니다(프로퍼티 오프셋을 더하지 않는다).
         * @details enum 은 **열거자 정체성**으로 적는다. 값이 아니라 열거자 이름의 해시(uint32)를 적고, 비트플래그는 켜진 이름의 수(uint32)와 그 해시들을
         *          (해시 오름차순이므로 같은 값이면 같은 바이트다) 적는다. 이름이 없는 값이나 이름이 덮지 못한 비트는 해시 위치에 0 을 두고 int64 값을 이어 붙인다.
         *          XML 과 JSON 이 이름을 적는 것과 같은 규칙이라, 열거자 순서를 바꾸거나 사이에 넣어도 저장된 뜻이 그대로다.
         * @return 바이너리 처리기 · 열거형 · 구조체 · 텍스트 처리기 어디에도 없는 타입이면(`hasBinaryValueCodec` 이 거짓) 아무 바이트도 쓰지 않고 false 입니다.
         *         예전에는 0 바이트 하나를 쓰고 성공해, 세이브는 써지고 로드에서 값이 빠졌습니다. 구조체 안의 값이 실패해도 false 입니다.
         */
        [[nodiscard]] SW_API static bool serializeValueBinary( ValueAddress value, const hashed_string& typeName,
                                                               vector<uint8>& listBuffer, const SerializeContext& context );
        /** @brief 값 하나를 바이너리로 쓰고 읽을 수 있는 타입인가입니다. `serializeValueBinary` · `deserializeValueBinary` 가 고르는 순서와 같습니다. */
        [[nodiscard]] SW_API static bool hasBinaryValueCodec( const hashed_string& typeName, const SerializeContext& context );
        /**
         * @brief 바이너리에서 값을 역직렬화합니다.
         * @param value 값 하나 자체의 주소입니다(프로퍼티 오프셋을 더하지 않는다). 프로퍼티는 `applyPropertyBinary` 로 읽습니다.
         * @param wireVersion 바이트가 적힌 버전입니다(스트림 머리가 알려 줍니다). 지금은 버전이 하나라 분기가 없고, 버전을 올리면 여기서 분기합니다.
         *                    기본값은 이 빌드가 방금 쓴 바이트(복사, 비교, diff)에만 맞습니다.
         * @details 모르는 열거자 이름(`Removed` 없이 지웠거나 `ValueAlias` 없이 이름을 바꾼 경우)이면 그 필드는 현재 값을 유지하고 false 를 반환한다. XML 의 모르는 이름과 같다.
         *          바이트는 끝까지 읽었으므로 부른 쪽이 다음 필드로 갈 수 있다(경고는 enum 과 이름마다 한 번이다).
         *          바이너리 처리기 · 열거형 · 구조체 · 텍스트 처리기 어디에도 없는 타입이면 바이트를 건너뛰지 않고 false 를 반환한다.
         *          여기서는 알리지 않는다. 엄격 읽기는 실패로, 소프트 읽기는 orphan 으로 부른 쪽이 다룬다(프로세스 정적 "한 번만" 로그는 테스트 순서에 따라 결과가 달라진다).
         */
        [[nodiscard]] SW_API static bool deserializeValueBinary( MutableValueAddress value, const hashed_string& typeName,
                                                                 const uint8* pData, size_t dataSize, size_t& offset,
                                                                 const SerializeContext& context,
                                                                 BinaryWireVersion       wireVersion = kCurrentBinaryWireVersion );

        /** @brief 중첩 컨테이너를 바이너리로 직렬화합니다. 원소 · 키 · 값 하나라도 쓰지 못하면(`serializeValueBinary`) false 이고 @p listBuffer 는 쓰면 안 됩니다. */
        [[nodiscard]] SW_API static bool serializeNestedContainerBinary( const void* pContainerPtr, const NestedContainerInfo& nested,
                                                                         vector<uint8>& listBuffer, const SerializeContext& context );
        /**
         * @brief 바이너리에서 중첩 컨테이너를 역직렬화합니다. 원소, 키, 값은 `wireVersion` 으로 읽습니다(`deserializeValueBinary`).
         * @details 원소와 항목이 실패했을 때의 규칙은 `ContainerVisitor` 를 따른다(모르는 열거자는 그 원소만 빼고 필드 전체는 실패로 처리한다).
         */
        [[nodiscard]] SW_API static bool deserializeNestedContainerBinary( void* pContainerPtr, const NestedContainerInfo& nested,
                                                                           const uint8* pData, size_t dataSize, size_t& offset,
                                                                           const SerializeContext& context,
                                                                           BinaryWireVersion       wireVersion = kCurrentBinaryWireVersion );

        /** @brief 값을 텍스트로 씁니다(중첩 타입은 중첩 문자열). 바깥 따옴표는 붙이지 않습니다. XML, JSON, SchemaMigrate 가 함께 쓰는 기본 조각입니다. @p value 는 값 하나 자체의 주소입니다. */
        static void valueToText( StringBuilder<constant::kMaxBuffer8192>& ss, ValueAddress value, const hashed_string& typeName,
                                 const SerializeContext& context );

        /** @brief 텍스트 토큰을 값 하나 자체(객체 주소가 아니다)에 읽어 씁니다. 값 주소 쪽 텍스트 쓰기이며, 짝은 `formatValueText` 입니다. */
        [[nodiscard]] SW_API static bool parseTextValue( MutableValueAddress value, const hashed_string& typeName, string_view valStr,
                                                         const SerializeContext& context );

        /**
         * @brief 프로퍼티에 선언된 기본값(PROPERTY `Default=`)이 있으면 적용합니다. 없으면 아무것도 하지 않습니다.
         * @details "선언된 기본값을 읽지 못함"(코드 결함)은 경고한다. 그 필드는 현재 값을 유지한다. 값은 `applyPropertyText` 로 쓴다
         *          (비트필드는 그 비트만 쓴다. 값 주소에 bool 을 통째로 쓰면 같은 바이트의 다른 플래그까지 지워진다).
         */
        static void applyPropertyDefault( const PropertyInfo& prop, void* pInstance, const SerializeContext& context );

        // ------------------------------------------------------------------------------
        // 프로퍼티 값 하나: 비트필드, 컨테이너, 값 세 경우를 한 벌로 처리한다. 직렬화기 셋, 프리팹 오버라이드 도구, 인스펙터가 함께 쓴다
        // (비트필드를 바이트째 비교하고 옮기면 같은 바이트의 다른 플래그까지 바뀐다).
        // ------------------------------------------------------------------------------
        /** @brief 한 인스턴스의 프로퍼티 값을 다른 인스턴스로 옮깁니다(같은 타입). 비트필드는 그 비트만, 컨테이너는 원소째 옮깁니다. 두 주소는 프로퍼티를 가진 **객체**의 주소입니다. */
        [[nodiscard]] SW_API static bool copyPropertyValue( const PropertyInfo& prop, ObjectAddress srcInstance, MutableObjectAddress dstInstance, const SerializeContext& context );
        /** @brief 두 인스턴스의 프로퍼티 값이 같은지 확인합니다. 비트필드는 그 비트만 비교합니다. */
        SW_API static bool arePropertyValuesEqual( const PropertyInfo& prop, const void* pInstanceA, const void* pInstanceB, const SerializeContext& context );
        /** @brief 텍스트 하나를 프로퍼티 값으로 씁니다(비트필드는 그 비트만). 읽지 못하면 false 이고 값은 그대로입니다. 컨테이너는 받지 않습니다. */
        [[nodiscard]] SW_API static bool applyPropertyText( const PropertyInfo& prop, MutableObjectAddress instance, string_view text, const SerializeContext& context );
        /**
         * @brief 바이너리 페이로드 하나(그 프로퍼티 값 전부)를 프로퍼티에 읽어 씁니다. 페이로드를 **끝까지** 읽어야 true 입니다. @p instance 는 프로퍼티를 가진 **객체**의 주소입니다.
         * @details 바이트가 남으면 스트림이 이 필드를 다른 형태로 적은 것이다(`int32` 0 을 `vector<int32>` 로 읽으면 원소 수 0 으로 성공한다). 그래서 오프셋을
         *          부른 쪽에 돌려주지 않고 여기서 끝을 본다 — 태그 스트림, diff, 값 복사가 "읽힌 만큼" 받는 사본을 따로 두지 않게.
         */
        [[nodiscard]] SW_API static bool applyPropertyBinary( const PropertyInfo& prop, MutableObjectAddress instance, const uint8* pPayload, size_t payloadSize,
                                                              const SerializeContext& context, BinaryWireVersion wireVersion = kCurrentBinaryWireVersion );
        /**
         * @brief 이 프로퍼티의 값을 세 형식(XML, JSON, 바이너리)이 모두 실어 나를 수 있으면 true 입니다. 직렬화기의 분기와 같은 판정입니다.
         * @details 직렬화기는 다룰 줄 모르는 타입을 **조용히** 텍스트 `null`, 바이너리 0 바이트로 쓰고, 읽을 때는 그 필드가 기본값이 된다(저장한 줄 알았던
         *          값이 사라진다). 컨테이너는 원소와 키 타입을, 소유 포인터 원소는 런타임 팩토리를 믿는다. 모든 PROPERTY 를 이 함수로 훑는 테스트가 있다.
         */
        SW_API static bool canCarryProperty( const PropertyInfo& prop, const SerializeContext& context );
        /** @brief 값 타입 하나를 세 형식이 모두 실어 나를 수 있으면 true 입니다(텍스트 핸들러, enum, 리플렉션 구조체). */
        SW_API static bool canCarryValueType( hashed_string typeName, const SerializeContext& context );

        /**
         * @brief 객체 안 프로퍼티 값을 사람이 읽을 수 있는 텍스트로 씁니다(XML 속성 값과 같은 형태). 비트필드는 `true` 나 `false`, 컨테이너는 `[n]`(원소 수)입니다.
         * @param instance 프로퍼티를 가진 **객체**의 주소입니다. 프로퍼티 오프셋(또는 접근자)으로 값을 찾습니다. 값 자체의 주소(`ValueAddress`)는 받지 않고 `formatValueText` 로 넘깁니다.
         */
        [[nodiscard]] SW_API static string formatPropertyText( const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context );
        /**
         * @brief 값 하나 자체를 텍스트로 씁니다. `formatPropertyText` 의 값 부분과 같은 형태이고, 짝은 `parseTextValue` 입니다.
         * @details 객체 주소와 값 주소를 한 함수로 받으면 값 주소에 오프셋이 한 번 더 더해진다(인스펙터 핸들 선택기가 고른 값을 `0:0` 으로 썼다).
         *          그래서 값 주소(`ValueAddress`)는 프로퍼티를 받지 않는 이 함수가 받고, 객체 주소와는 형식이 달라 서로 넘길 수 없다.
         *          오프셋 0 짜리 가짜 `PropertyInfo` 를 만들어 `formatPropertyText` 에 넘기지 않는다.
         */
        [[nodiscard]] SW_API static string formatValueText( ValueAddress value, const hashed_string& typeName, const SerializeContext& context );

        /** @brief 키 문자열이 같은지 비교합니다(대소문자 무시 옵션 지원). */
        static bool keysEqual( string_view left, string_view right, bool bIgnoreCase );

        /**
         * @brief Alias 로 들어온 이름을 `SerializeContext` 핸들러가 아는 원본 이름(`_name`)으로 바꿉니다.
         * @details `SerializerUtil` 과 `JSONSerializer` 가 함께 쓰는 핸들러 조회 규칙이다.
         */
        SW_API static hashed_string resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& context );

        /**
         * @brief 이 타입 이름이 **중첩 객체**(프로퍼티를 풀어 쓰는 REFLECT 타입)이면 그 TypeInfo 를, 아니면 nullptr 를 반환합니다.
         * @details 텍스트 핸들러가 있거나(값을 한 줄로 쓴다) enum 이거나 기본형이면 중첩 객체가 아니다. `JSONSerializer` 와
         *          `XMLSerializer` 가 함께 쓴다. 한쪽만 규칙이 바뀌면 두 포맷이 같은 필드를 다르게 쓴다.
         */
        SW_API static const TypeInfo* findNestedObjectType( hashed_string typeName, const SerializeContext& context );

        /**
         * @brief 원소 타입 이름이 **소유 포인터**인지 확인합니다(이름에 `*` 가 있으면 소유 포인터입니다).
         * @details `JSONSerializer` 와 `XMLSerializer` 가 함께 쓴다. 판정 기준이
         *          "이름에 별표가 있는가"라는 문자열 규칙이라, 한쪽만 고치면 두 포맷이 서로 다른
         *          컨테이너를 소유로 보게 된다.
         */
        SW_API static bool isOwnedPointerElementType( hashed_string elementTypeName );

        /**
         * @brief `Min`/`Max` 밖 숫자 칸마다 "경로 = 값 is outside [아래, 위]" 한 줄을 모읍니다(로드 시점 계약 검사 — 설정 JSON 과 데이터 XML 이 함께 씁니다).
         * @details 구조체 칸과 시퀀스(vector, 고정 배열) 원소는 늘 들어가 본다(구조체 원소는 그 칸, 숫자 원소는 그 프로퍼티의 범위). 비트필드 · 접근자 칸은 보지 않는다.
         *          맵과 중첩 컨테이너는 원소 경로를 하나로 정할 수 없어 보지 않는다.
         *          범위를 어디까지 보는지 부르는 쪽이 고르지 않는다. 고르게 하면 읽기 경로마다 계약이 달라진다(설정 JSON · 데이터 XML · 세이브가 같은 `Min` · `Max` 를 다르게 지켰다).
         *          경계는 인스펙터와 같은 함수로 푼다(`PropertyMetadata::computeAllowedRange`) — 숫자 경계와 필드 경계(`Min = "-_halfSize"`)를 함께 지킨다.
         */
        SW_API static void collectRangeErrors( const TypeInfo& typeInfo, const void* pInstance, const string& prefix, vector<string>& outListError );
        /**
         * @brief 읽기를 끝낸 인스턴스가 `Min` · `Max` 를 지키는지 봅니다. 어기면 칸마다 오류 로그를 남기고 false 입니다.
         * @details 세 포맷(바이너리 · JSON · XML)의 읽기 공통 끝이 부릅니다 — 호출부마다 따로 부르면 부르지 않는 경로의 값이 범위 밖인 채 성공으로 돌아온다(A-19).
         *          위반이 없으면 글을 만들지 않고 첫 위반에서 멈추는 빠른 길로 끝납니다.
         */
        [[nodiscard]] SW_API static bool isWithinDeclaredRanges( const TypeInfo& typeInfo, const void* pInstance );

        /** @brief 프로퍼티 목록에서 키에 맞는 프로퍼티 메타데이터를 찾습니다. */
        static const PropertyInfo* matchProperty( const vector<PropertyInfo>& listProp, string_view keyRaw,
                                                  bool bIgnoreCaseKeys, bool& bCaseVariant );

        /**
         * @brief 이름 해시(바이너리 태그 · diff 항목)에 맞는 프로퍼티의 인덱스입니다. 없으면 `listProp.size()` 입니다.
         * @details 모든 프로퍼티의 지금 이름을 먼저 보고, 없을 때만 옛 이름(`PROPERTY( Alias = … )`)을 봅니다. 그래서 정상 경로는 옛 이름을 보지 않고,
         *          지금 이름이 늘 옛 이름보다 먼저입니다. 바이너리 태그 스트림과 오브젝트 diff 가 함께 씁니다.
         */
        static size_t findPropertyIndexByNameHash( const vector<PropertyInfo>& listProp, uint32 nameHash );

        /**
         * @brief 이 문맥의 저장 목적에서 이 타입의 프로퍼티를 쓰고 읽는지 판정합니다. 세 형식(XML · JSON · 바이너리)과 diff 가 모두 이 함수를 지납니다.
         * @details 표는 `SerializePurpose` 설명에 있다. `Transient` 는 어느 목적도 쓰지 않고, `RuntimeState` 는 저작(Authoring)만 쓰지 않으며,
         *          세이브(SaveGame)는 `SaveGame` 필드가 하나라도 있는 타입(옵트인)이면 그 필드만 쓴다. 읽을 때 이 함수가 false 인 필드는 지금 값 그대로 둔다.
         * @param owner 프로퍼티를 가진 타입(상속 목록을 순회하는 그 타입). 옵트인 판정은 상속분까지 봅니다(`TypeInfo::hasSaveGameProperty`).
         */
        static bool shouldSerializeProperty( const TypeInfo& owner, const PropertyInfo& prop, const SerializeContext& context )
        {
            if ( prop._metadata._bTransient == SW_TRUE )
                return false;
            switch ( context.getPurpose() )
            {
                case SerializePurpose::Authoring:
                    return prop._metadata._bRuntimeState == SW_FALSE;
                case SerializePurpose::Snapshot:
                    return true;
                case SerializePurpose::SaveGame:
                    return prop._metadata._bSaveGame == SW_TRUE || owner.hasSaveGameProperty() == false;
            }
            return false;
        }

        /**
         * @brief 읽는 파일에 이 필드가 있으면 로드 오류인지 판정합니다. 저작 읽기(Authoring)의 `RuntimeState` 필드입니다.
         * @details 조용히 건너뛰면 저작 파일에 섞인 런타임 상태가 다음 저장까지 남아 있는 것처럼 보인다. 옛 데이터는 다시 쓴다(옛 형식 리더를 두지 않는다).
         *          부르는 쪽은 `reportForbiddenProperty` 로 이름을 남기고 읽기를 실패로 끝낸다.
         */
        static bool isForbiddenInFile( const PropertyInfo& prop, const SerializeContext& context )
        {
            return context.getPurpose() == SerializePurpose::Authoring && prop._metadata._bTransient == SW_FALSE && prop._metadata._bRuntimeState == SW_TRUE;
        }

        /** @brief 저작 파일에 있으면 안 되는 필드(`isForbiddenInFile`)를 타입과 필드 이름으로 오류 로그에 남깁니다. */
        SW_API static void reportForbiddenProperty( const TypeInfo& owner, const PropertyInfo& prop );

        /** @brief JSON 문자열을 바이너리 버퍼로 트랜스코딩합니다. */
        SW_API static bool transcodeJSONToBinary( string_view jsonStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                  const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 바이너리 데이터를 JSON 문자열로 트랜스코딩합니다. */
        SW_API static string transcodeBinaryToJSON( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo, bool bPretty = false,
                                                    const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief XML 문자열을 바이너리 버퍼로 트랜스코딩합니다. */
        SW_API static bool transcodeXMLToBinary( string_view xmlStr, const TypeInfo& typeInfo, vector<uint8>& outBinary,
                                                 const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );

        /** @brief 바이너리 데이터를 XML 문자열로 트랜스코딩합니다. */
        SW_API static string transcodeBinaryToXML( const uint8* pData, size_t dataSize, const TypeInfo& typeInfo,
                                                   const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring ) );
    };
} // namespace sw
