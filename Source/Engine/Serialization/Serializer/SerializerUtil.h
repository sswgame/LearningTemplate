/**
 * @file SerializerUtil.h
 * @brief 직렬화기(Binary, JSON, XML, ObjectDiff, Archive)가 함께 쓰는 도우미, 트랜스코딩 엔진, 스크래치 RAII 입니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Expected.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    class ReflectValueBinding;
    enum class FieldReadResult : uint8;

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
     * @brief 버전 번호를 어디서 얻는지 나타냅니다. 포맷마다 다릅니다.
     */
    enum class SchemaVersionSource : uint8
    {
        Stream, ///< 본문 앞에서 따로 읽어 옵니다(Binary). 공통 절차에 들어올 때 이미 채워져 있습니다.
        Payload ///< 본문 안의 필드입니다(JSON/XML 의 `_schemaVersion`). soft 역직렬화가 알려 줍니다.
    };

    /**
     * @brief 공통 절차가 포맷에 맡기는 유일한 일입니다. 본문을 읽어 값과 orphan 을 채웁니다.
     * @details 인자는 (대상 객체의 주소, 그 타입, orphan 수집함, 버전 출력)입니다. 버전을 본문에서 얻지 않는
     *          포맷(Binary)은 마지막 인자를 건드리지 않습니다.
     */
    using SoftDeserializeFn = Delegate<bool( MutableObjectAddress, const TypeInfo&, vector<SchemaOrphanValue>&, uint32& )>;

    /**
     * @brief `deserializeVersioned` 의 공통 절차입니다. 세 포맷이 함께 씁니다.
     *
     * @param streamVersion `SchemaVersionSource::Stream` 이면 본문 앞에서 이미 읽은 판입니다. `Payload` 면 쓰지 않습니다(판은 soft 읽기가 본문에서 낸다).
     * @return 판이 @p currentVersion 과 같고 읽었으면 true 입니다. 읽은 판은 돌려주지 않는다 — 성공이면 @p currentVersion 이고, 다른 판의 거절은 `Schema.VersionMismatch` 로그가 판을 적는다.
     * @param softDeserialize (대상, 타입, orphan 수집함, 버전 출력)을 받아 성공 여부를 반환합니다.
     *                        `Stream` 포맷은 버전 출력을 건드리지 않습니다.
     * @details 처리 순서는 다음과 같다. 현재 타입으로 soft 역직렬화하고, 버전을 확정한 다음 `finishVersionedRead` 를 부른다.
     *          판이 현재 판과 다르면 이관하지 않고 실패한다.
     */
    [[nodiscard]] SW_API bool runVersionedDeserialize( uint32 streamVersion, MutableObjectAddress instance, const TypeInfo& typeInfo,
                                                       uint32 currentVersion, SchemaVersionSource versionSource, SchemaOrphanPolicy orphanPolicy,
                                                       const SoftDeserializeFn& softDeserialize );

    /** @brief 필수 판정의 범위입니다 — 원소가 부모를 가리키면(`PROPERTY( InheritParent )` 칸이 적힘) `RequiredAtRoot` 칸은 물려받은 값이다. */
    enum class RequiredScope : uint8
    {
        Root,      ///< 부모가 없다 — `Required` 와 `RequiredAtRoot` 칸이 모두 필수다.
        Inherited, ///< 부모를 가리킨다 — `Required` 칸만 필수다.
    };

    /** @brief 직렬화기 TU 들이 함께 쓰는 도우미입니다. */
    /**
     * @brief 소프트 읽기가 읽기 전 값으로 되돌린 칸 하나입니다(`PropertyReadRollback` 이 담는다). 범위 검사가 이 칸을 보지 않는다.
     * @details 값 주소만으로는 구조체 칸과 그 첫 필드(오프셋 0)를 가르지 못해 프로퍼티를 함께 든다.
     */
    struct RestoredPropertyValue
    {
        const void*         _pValue{ nullptr };
        const PropertyInfo* _pProperty{ nullptr };

        [[nodiscard]] bool operator==( const RestoredPropertyValue& other ) const { return _pValue == other._pValue && _pProperty == other._pProperty; }
    };

    struct SerializerUtil
    {
        /**
         * @brief 값을 바이너리로 직렬화합니다(이 빌드의 버전, `kCurrentBinaryWireVersion`). @p value 는 값 하나 자체의 주소입니다(프로퍼티 오프셋을 더하지 않는다).
         * @details enum 은 열거자 정체성으로 적는다. 값이 아니라 열거자 이름의 해시(uint32)를 적고, 비트플래그는 켜진 이름의 수(uint32)와 그 해시들을
         *          (해시 오름차순이므로 같은 값이면 같은 바이트다) 적는다. 이름이 없는 값이나 이름이 덮지 못한 비트는 해시 위치에 0 을 두고 int64 값을 이어 붙인다.
         *          XML 과 JSON 이 이름을 적는 것과 같은 규칙이라, 열거자 순서를 바꾸거나 사이에 넣어도 저장된 뜻이 그대로다.
         * @return 바이너리 처리기 · 열거형 · 구조체 · 텍스트 처리기 어디에도 없는 타입이면(`hasBinaryValueCodec` 이 거짓) 아무 바이트도 쓰지 않고 false 입니다.
         *         예전에는 0 바이트 하나를 쓰고 성공해, 세이브는 써지고 로드에서 값이 빠졌습니다. 구조체 안의 값이 실패해도 false 입니다.
         */
        /**
         * @brief 값 하나를 쓰고 읽을 처리기 · 형식입니다(바이너리 · 텍스트 · JSON · XML 이 함께 쓴다). 묶음(`ReflectValueBinding`)이 있으면 이름 조회 없이, 없으면 이름으로 찾는다.
         * @details 순서는 어느 형식이나 같다 — 처리기, 열거형, 구조체, 텍스트 처리기. 이름으로 찾는 길은 열거형 · 구조체를 처리기가 없을 때만(그때) 찾는다.
         *          `findValueCodecs` 로만 만든다(내장형 칸은 그것이 채운다).
         */
        struct SW_API ValueCodecs
        {
            hashed_string       _typeName{};       ///< 값의 형식 이름
            hashed_string       _handlerName{};    ///< 내장형이 아닐 때 처리기를 찾을 이름(별칭이면 형식의 정규 이름)
            const EnumInfo*     _pEnum{ nullptr }; ///< 묶음의 사실(이름으로 찾는 길이면 `findEnum` 이 그때 찾는다)
            const TypeInfo*     _pType{ nullptr }; ///< 묶음의 사실(이름으로 찾는 길이면 `findStructType` 이 그때 찾는다)
            ReflectBuiltinIndex _builtinIndex;     ///< 내장형이면 처리기는 문맥의 색인 배열에서 고른다(아니면 kNone)
            bool                _bByName{ false }; ///< 묶음 없이 이름으로 찾는 길.

            const SerializeContext::BinaryWriteFn* findBinaryWriter( const SerializeContext& context ) const;
            const SerializeContext::BinaryReadFn*  findBinaryReader( const SerializeContext& context ) const;
            const SerializeContext::TextWriteFn*   findTextWriter( const SerializeContext& context ) const;
            const SerializeContext::TextReadFn*    findTextReader( const SerializeContext& context ) const;
            /** @brief 처리기 이름입니다 — 내장형이면 정규 이름(`string` · `bool` …), 아니면 `_handlerName`. 형식 갈래(글 · 불리언)를 이름 없이 묻는 곳이 쓴다. */
            const hashed_string& getHandlerName() const;
            /** @brief 열거형 정보입니다. 이름으로 찾는 길이면 지금 레지스트리에서 찾습니다. */
            const EnumInfo* findEnum( const SerializeContext& context ) const;
            /** @brief 구조체(원시 형식이 아닌) 정보입니다. 아니면 nullptr 입니다. */
            const TypeInfo* findStructType( const SerializeContext& context ) const;
        };
        /** @brief @p typeName 값의 처리기 · 형식을 고릅니다. @p pBinding 은 그 이름에 묶은 사실이고 없으면(nullptr) 이름으로 찾습니다. */
        SW_API static ValueCodecs findValueCodecs( const hashed_string& typeName, const ReflectValueBinding* pBinding, const SerializeContext& context );

        /** @brief 값 하나를 바이너리로 씁니다. @p pBinding 은 @p typeName 에 묶은 사실(프로퍼티 · 컨테이너 원소 · 키)이고 nullptr 이면 이름으로 찾습니다. */
        [[nodiscard]] SW_API static bool serializeValueBinary( ValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding, vector<uint8>& listBuffer,
                                                               const SerializeContext& context );
        /** @brief 값 하나를 바이너리에서 읽습니다. @p pBinding 은 `serializeValueBinary` 와 같습니다. */
        [[nodiscard]] SW_API static FieldReadResult deserializeValueBinary( MutableValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding,
                                                                            const uint8* pData, size_t dataSize, size_t& offset, const SerializeContext& context,
                                                                            BinaryWireVersion wireVersion );
        [[nodiscard]] SW_API static bool            serializeValueBinary( ValueAddress value, const hashed_string& typeName,
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
         * @return 위의 실패는 모두 `FieldFailed` 입니다. 값 구조체 안에 객체째 실패여야 할 것(저작 파일의 금지된 필드, 되돌리지 못한 필드)이 있으면
         *         `ObjectRejected` 입니다(`BinarySerializer::deserializeNested`) — 부른 쪽이 그 필드만 넘기면 안의 금지된 필드가 orphan 으로 넘어간다.
         */
        [[nodiscard]] SW_API static FieldReadResult deserializeValueBinary( MutableValueAddress value, const hashed_string& typeName,
                                                                            const uint8* pData, size_t dataSize, size_t& offset,
                                                                            const SerializeContext& context,
                                                                            BinaryWireVersion       wireVersion = kCurrentBinaryWireVersion );

        /**
         * @brief 중첩 컨테이너를 바이너리로 직렬화합니다. 원소 · 키 · 값 하나라도 쓰지 못하면(`serializeValueBinary`) false 이고 @p listBuffer 는 쓰면 안 됩니다.
         * @param container 컨테이너 값 자체의 주소입니다(프로퍼티 오프셋을 더한 뒤). 객체 주소는 받지 않습니다.
         */
        [[nodiscard]] SW_API static bool serializeNestedContainerBinary( ValueAddress container, const NestedContainerInfo& nested,
                                                                         vector<uint8>& listBuffer, const SerializeContext& context );
        /**
         * @brief 바이너리에서 중첩 컨테이너를 역직렬화합니다. 원소, 키, 값은 `wireVersion` 으로 읽습니다(`deserializeValueBinary`).
         * @details 원소와 항목이 실패했을 때의 규칙은 `ContainerVisitor` 를 따른다(모르는 열거자는 그 원소만 빼고 필드 전체는 실패로 처리한다).
         * @return 컨테이너 결과를 필드 결과로 옮긴 것입니다(`ContainerVisitor::toFieldResult`). 원소 안의 객체째 실패는 `ObjectRejected` 로 올라갑니다.
         */
        [[nodiscard]] SW_API static FieldReadResult deserializeNestedContainerBinary( MutableValueAddress container, const NestedContainerInfo& nested,
                                                                                      const uint8* pData, size_t dataSize, size_t& offset,
                                                                                      const SerializeContext& context,
                                                                                      BinaryWireVersion       wireVersion = kCurrentBinaryWireVersion );

        /** @brief 값을 텍스트로 씁니다(중첩 타입은 중첩 문자열). 바깥 따옴표는 붙이지 않습니다. XML, JSON, SchemaMigrate 가 함께 쓰는 기본 조각입니다. @p value 는 값 하나 자체의 주소입니다. */
        static void valueToText( StringBuilder<constant::buffer::kMax8192>& ss, ValueAddress value, const hashed_string& typeName,
                                 const SerializeContext& context );
        /** @brief `valueToText` 의 묶음 판입니다. @p pBinding 은 @p typeName 에 묶은 사실이고 nullptr 이면 이름으로 찾습니다. */
        SW_API static void valueToText( StringBuilder<constant::buffer::kMax8192>& ss, ValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding,
                                        const SerializeContext& context );

        /** @brief 텍스트 토큰을 값 하나 자체(객체 주소가 아니다)에 읽어 씁니다. 값 주소 쪽 텍스트 쓰기이며, 짝은 `formatValueText` 입니다. */
        [[nodiscard]] SW_API static bool parseTextValue( MutableValueAddress value, const hashed_string& typeName, string_view valStr,
                                                         const SerializeContext& context );
        /** @brief `parseTextValue` 의 묶음 판입니다. */
        [[nodiscard]] SW_API static bool parseTextValue( MutableValueAddress value, const hashed_string& typeName, const ReflectValueBinding* pBinding, string_view valStr,
                                                         const SerializeContext& context );

        /**
         * @brief 프로퍼티에 선언된 기본값(PROPERTY `Default=`)이 있으면 적용합니다. 없으면 아무것도 하지 않습니다.
         * @details 값은 생성된 타입 있는 대입(`PropertyMetadata::_pApplyDefault`)이 넣는다 — 기본값의 철자 · 타입 결함은 컴파일 오류라 여기서 실패할 길이 없다.
         *          비트필드도 멤버 대입이라 그 비트만 바뀐다.
         */
        static void applyPropertyDefault( const PropertyInfo& prop, MutableObjectAddress instance );

        // ------------------------------------------------------------------------------
        // 프로퍼티 값 하나: 비트필드, 컨테이너, 값 세 경우를 한 벌로 처리한다. 직렬화기 셋, 프리팹 오버라이드 도구, 인스펙터가 함께 쓴다
        // (비트필드를 바이트째 비교하고 옮기면 같은 바이트의 다른 플래그까지 바뀐다).
        // ------------------------------------------------------------------------------
        /** @brief 한 인스턴스의 프로퍼티 값을 다른 인스턴스로 옮깁니다(같은 타입). 비트필드는 그 비트만, 컨테이너는 원소째 옮깁니다. 두 주소는 프로퍼티를 가진 객체의 주소입니다. */
        [[nodiscard]] SW_API static bool copyPropertyValue( const PropertyInfo& prop, ObjectAddress srcInstance, MutableObjectAddress dstInstance, const SerializeContext& context );
        /**
         * @brief 두 인스턴스의 프로퍼티 값이 같은지 확인합니다. 비트필드는 그 비트만 비교합니다. 두 주소는 프로퍼티를 가진 객체의 주소입니다.
         * @details 값 비교는 이 함수 하나다(diff · 프리팹 덮어쓰기 · 인스펙터의 기본값 · 혼합 값 표시) — 형식이 정한 길로 값을 바로 견주고, 처리기만 아는 값만 바이트로 옮겨 견준다.
         *          구조체 안의 칸은 @p context 의 저장 목적이 다루는 것만 본다. 옮길 수 없는 값(`canCarryProperty` 가 거짓)은 같은지 알 수 없어 다르다고 답한다.
         */
        SW_API static bool arePropertyValuesEqual( const PropertyInfo& prop, ObjectAddress instanceA, ObjectAddress instanceB, const SerializeContext& context );
        /** @brief 텍스트 하나를 프로퍼티 값으로 씁니다(비트필드는 그 비트만). 읽지 못하면 false 이고 값은 그대로입니다. 컨테이너는 받지 않습니다. */
        [[nodiscard]] SW_API static bool applyPropertyText( const PropertyInfo& prop, MutableObjectAddress instance, string_view text, const SerializeContext& context );
        /**
         * @brief 프로퍼티 값 하나(그 프로퍼티 값 전부)를 바이너리 페이로드로 @p outBytes 끝에 덧붙입니다. 짝은 `applyPropertyBinary` 입니다. 비트필드는 `bool` 로 씁니다.
         * @details 태그 스트림 쓰기, 값 복사, 읽기 실패 되돌리기(`PropertyReadRollback`)가 함께 쓴다. 비트필드 · 컨테이너 · 값 세 분기를 한 벌로 둔다.
         * @return 그 값이나 안의 값에 바이너리 · 열거형 · 구조체 · 텍스트 처리기가 없으면 false 이고, 그때 @p outBytes 는 쓰면 안 됩니다.
         */
        [[nodiscard]] SW_API static bool writePropertyBinary( const PropertyInfo& prop, ObjectAddress instance, vector<uint8>& outBytes, const SerializeContext& context );
        /**
         * @brief 바이너리 페이로드 하나(그 프로퍼티 값 전부)를 프로퍼티에 읽어 씁니다. 페이로드를 끝까지 읽어야 `Read` 입니다. @p instance 는 프로퍼티를 가진 객체의 주소입니다.
         * @details 바이트가 남으면 스트림이 이 필드를 다른 형태로 적은 것이다(`int32` 0 을 `vector<int32>` 로 읽으면 원소 수 0 으로 성공한다). 그래서 오프셋을
         *          부른 쪽에 돌려주지 않고 여기서 끝을 본다 — 태그 스트림, diff, 값 복사가 "읽힌 만큼" 받는 사본을 따로 두지 않게.
         *          실패하면 그 필드는 읽다 만 값일 수 있다(컨테이너 · 구조체는 원소와 필드를 채운 뒤 끝에서 실패한다). 실패한 뒤에도 이어 읽는 쪽은
         *          `PropertyReadRollback` 으로 읽기 전 값을 담아 두고 실패하면 되돌린다.
         * @return `FieldFailed` 는 이 필드만의 실패이고, `ObjectRejected` 는 안쪽(구조체 칸, 컨테이너 원소인 구조체 · 컴포넌트)에 객체째 실패여야 할 것이 있었다는 뜻입니다.
         *         받는 쪽은 `default` 없는 `switch` 로 세 값을 다 다룹니다(`FieldReadResult`).
         */
        [[nodiscard]] SW_API static FieldReadResult applyPropertyBinary( const PropertyInfo& prop, MutableObjectAddress instance, const uint8* pPayload, size_t payloadSize,
                                                                         const SerializeContext& context, BinaryWireVersion wireVersion = kCurrentBinaryWireVersion );
        /**
         * @brief 이 프로퍼티의 값을 세 형식(XML, JSON, 바이너리)이 모두 실어 나를 수 있으면 true 입니다. 직렬화기의 분기와 같은 판정입니다.
         * @details 직렬화기는 다룰 줄 모르는 타입을 조용히 텍스트 `null`, 바이너리 0 바이트로 쓰고, 읽을 때는 그 필드가 기본값이 된다(저장한 줄 알았던
         *          값이 사라진다). 컨테이너는 원소와 키 타입을, 소유 포인터 원소는 런타임 팩토리를 믿는다. 모든 PROPERTY 를 이 함수로 훑는 테스트가 있다.
         */
        SW_API static bool canCarryProperty( const PropertyInfo& prop, const SerializeContext& context );
        /** @brief 값 타입 하나를 세 형식이 모두 실어 나를 수 있으면 true 입니다(텍스트 핸들러, enum, 리플렉션 구조체). */
        SW_API static bool canCarryValueType( hashed_string typeName, const SerializeContext& context );
        /**
         * @brief 원소가 값 글 하나로 읽히는 시퀀스 칸(이름 · 태그 · 수 · 열거의 목록)이면 그 시퀀스입니다. 아니면 nullptr 입니다.
         * @details XML 속성의 한 줄 목록(`XMLFields`)과 `InstancedStruct` 의 글 형식이 같은 기준으로 목록 칸을 고릅니다.
         */
        [[nodiscard]] SW_API static const ISequenceContainerWrapper* findTextList( const PropertyInfo& prop, const SerializeContext& context );

        /**
         * @brief 객체 안 프로퍼티 값을 사람이 읽을 수 있는 텍스트로 씁니다(XML 속성 값과 같은 형태). 비트필드는 `true` 나 `false`, 컨테이너는 `[n]`(원소 수)입니다.
         * @param instance 프로퍼티를 가진 객체의 주소입니다. 프로퍼티 오프셋(또는 접근자)으로 값을 찾습니다. 값 자체의 주소(`ValueAddress`)는 받지 않고 `formatValueText` 로 넘깁니다.
         */
        [[nodiscard]] SW_API static string formatPropertyText( const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context );
        /**
         * @brief 값 하나 자체를 텍스트로 씁니다. `formatPropertyText` 의 값 부분과 같은 형태이고, 짝은 `parseTextValue` 입니다.
         * @details 객체 주소와 값 주소를 한 함수로 받으면 값 주소에 오프셋이 한 번 더 더해진다(인스펙터 핸들 선택기가 고른 값을 `0:0` 으로 썼다).
         *          그래서 값 주소(`ValueAddress`)는 프로퍼티를 받지 않는 이 함수가 받고, 객체 주소와는 형식이 달라 서로 넘길 수 없다.
         *          오프셋 0 짜리 가짜 `PropertyInfo` 를 만들어 `formatPropertyText` 에 넘기지 않는다.
         */
        [[nodiscard]] SW_API static string formatValueText( ValueAddress value, const hashed_string& typeName, const SerializeContext& context );

        /**
         * @brief 이름이 같은 칸끼리 값을 옮깁니다(@p srcType → @p dstType, 글로 옮기므로 타입은 글 형식이 같으면 된다). 짝이 없는 칸은 건너뜁니다.
         * @details 한 데이터를 종류마다 다른 스키마로 읽고 쓰는 곳(행동 트리 노드 — 종류마다 받는 칸이 다르다)이 칸마다 손으로 옮기지 않게 한다. 옮기지 못한 칸이 있으면 false 입니다.
         */
        [[nodiscard]] SW_API static bool copySameNamedProperties( const void* pSrc, const TypeInfo& srcType, void* pDst, const TypeInfo& dstType );

        /** @brief 키 문자열이 철자 그대로 같은지 비교합니다(대소문자가 다르면 다른 키 — 별칭 없음). */
        static bool isKeyEqual( string_view left, string_view right );

        /**
         * @brief Alias 로 들어온 이름을 `SerializeContext` 핸들러가 아는 원본 이름(`_name`)으로 바꿉니다.
         * @details `SerializerUtil` 과 `JSONSerializer` 가 함께 쓰는 핸들러 조회 규칙이다.
         */
        SW_API static hashed_string resolveHandlerTypeName( const hashed_string& typeName, const SerializeContext& context );

        /**
         * @brief 이 타입 이름이 중첩 객체(프로퍼티를 풀어 쓰는 REFLECT 타입)이면 그 TypeInfo 를, 아니면 nullptr 를 반환합니다.
         * @details 텍스트 핸들러가 있거나(값을 한 줄로 쓴다) enum 이거나 기본형이면 중첩 객체가 아니다. `JSONSerializer` 와
         *          `XMLSerializer` 가 함께 쓴다. 한쪽만 규칙이 바뀌면 두 포맷이 같은 필드를 다르게 쓴다.
         * @param pBinding @p typeName 에 묶은 사실(프로퍼티 · 컨테이너 원소)입니다. 값마다 불리므로 이름 조회 · 공유 잠금 없이 답하게 늘 넘기고, 묶음이 없을 때만 nullptr 입니다.
         */
        SW_API static const TypeInfo* findNestedObjectType( const hashed_string& typeName, const ReflectValueBinding* pBinding, const SerializeContext& context );

        /**
         * @brief 필수 칸(`PROPERTY( Required )`)이 빠졌는지 봅니다 — 적히지 않았거나, 이름 · 경로 형식(`isNameValueType`)인데 적힌 글이 비었다.
         * @details XML · JSON 의 `collectMissingRequired` 가 함께 쓰는 한 판정이다(한쪽만 바뀌면 두 포맷이 같은 데이터를 다르게 통과시킨다).
         *          빈 이름은 형식만 있고 값이 없는 칸이다 — 에디터가 빈 칸을 속성으로 적어도 데이터가 값을 준 것이 아니다.
         * @param pWrittenText 적힌 글(스칼라 칸). 적히지 않았으면 nullptr, 객체 · 목록으로 적혔으면 빈 값이 아닌 것으로 본다(@p bWritten 만 본다).
         * @param scope 원소가 부모를 가리키는지(`findRequiredScope`). 부모를 모르는 읽기(객체 · 인스턴스 구조체)는 `Root` 다.
         */
        SW_API static bool isRequiredValueMissing( const PropertyInfo& prop, bool bWritten, const string_view* pWrittenText, RequiredScope scope );
        /**
         * @brief 원소의 필수 판정 범위를 정합니다 — `InheritParent` 칸이 비어 있지 않게 적혔으면 `Inherited` 입니다.
         * @param fnWrittenText 칸 색인 → 적힌 글(적히지 않았으면 빈 글). 형식마다 원소를 읽는 법만 다르다.
         */
        template <typename TList, typename FnWrittenText>
        static RequiredScope findRequiredScope( const TList& listProperty, FnWrittenText&& fnWrittenText )
        {
            for ( size_t propIndex = 0; propIndex < listProperty.size(); ++propIndex )
            {
                if ( listProperty[propIndex]._metadata->_bInheritParent == SW_TRUE && fnWrittenText( propIndex ).empty() == false )
                    return RequiredScope::Inherited;
            }
            return RequiredScope::Root;
        }
        /** @brief 이름 · 경로 형식(hashed_string · TagID · DataPath · AssetID · CatalogKey · AssetRef<…>)인지 봅니다 — 빈 값이 "없음" 인 형식이다. 글(`string`)은 빈 값도 값이라 넣지 않는다. */
        SW_API static bool isNameValueType( const hashed_string& typeName );

        /**
         * @brief 원소 타입 이름이 소유 포인터인지 확인합니다(이름에 `*` 가 있으면 소유 포인터입니다).
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
         *          @p instance 는 @p typeInfo 객체의 주소다(필드 경계를 이 객체에서 읽는다).
         */
        SW_API static void collectRangeErrors( const TypeInfo& typeInfo, ObjectAddress instance, const string& prefix, vector<string>& outListError,
                                               const vector<RestoredPropertyValue>* pListRestored = nullptr );
        /**
         * @brief `collectRangeErrors` 의 칸 하나입니다. @p instance 는 @p prop 을 가진 객체의 주소입니다.
         * @details 파일에 적힌 칸만 보는 읽기(`XMLFields::readProperties`)가 부른다 — 적지 않은 칸은 코드의 초기값이라 데이터 오류가 아니다.
         */
        SW_API static void collectPropertyRangeErrors( const PropertyInfo& prop, ObjectAddress instance, const string& prefix, vector<string>& outListError,
                                                       const vector<RestoredPropertyValue>* pListRestored = nullptr );
        /**
         * @brief 읽기를 끝낸 인스턴스가 `Min` · `Max` 를 지키는지 봅니다. 어기면 칸마다 오류 로그를 남기고 false 입니다.
         * @details 세 포맷(바이너리 · JSON · XML)의 읽기 공통 끝이 부릅니다 — 호출부마다 따로 부르면 부르지 않는 경로의 값이 범위 밖인 채 성공으로 돌아온다(A-19).
         *          위반이 없으면 글을 만들지 않고 첫 위반에서 멈추는 빠른 길로 끝납니다.
         */
        [[nodiscard]] SW_API static bool isWithinDeclaredRanges( const TypeInfo& typeInfo, ObjectAddress instance );
        /**
         * @brief 로그 없이 범위만 봅니다. 남이 보낸 상태 바이트(롤백 · 넷 · 세이브)의 읽기용. 거절은 읽기 실패로만 알린다(손 `io.check` 와 같은 계약, 깨진 바이트로 오류 로그를 쏟게 하지 않는다).
         */
        [[nodiscard]] SW_API static bool areValuesWithinDeclaredRanges( const TypeInfo& typeInfo, ObjectAddress instance );
        /**
         * @brief 위와 같되 소프트 읽기가 읽기 전 값으로 되돌린 칸(@p listRestored — 어느 깊이든)은 보지 않습니다. 세 형식(JSON · XML · 바이너리)의 소프트 읽기 끝이 모두 이것을 지난다.
         * @details 되돌린 칸은 파일 값이 아니라 읽기 전 값이고(바탕 층 형식의 새 객체면 0 — 범위 밖), 그 칸의 잘못은 orphan 이 이미 알렸다.
         *          보면 모르는 키 · 읽지 못한 값 하나가 범위 밖 오류 묶음으로 덧나 알려진다. 목록은 `PropertyReadRollback::getRestoredValues` 가 준다.
         */
        [[nodiscard]] SW_API static bool isWithinDeclaredRanges( const TypeInfo& typeInfo, ObjectAddress instance, const vector<RestoredPropertyValue>& listRestored );

        /** @brief 프로퍼티 목록에서 키(철자 그대로)에 맞는 프로퍼티 메타데이터를 찾습니다. 지금 이름을 모두 본 뒤에 옛 이름(Alias)을 봅니다. */
        static const PropertyInfo* matchProperty( PropertyListView listProp, string_view keyRaw );

        /**
         * @brief 이름 해시(바이너리 태그 · diff 항목)에 맞는 프로퍼티의 인덱스입니다. 없으면 비어 있습니다.
         * @details 모든 프로퍼티의 지금 이름을 먼저 보고, 없을 때만 옛 이름(`PROPERTY( Alias = … )`)을 봅니다. 그래서 정상 경로는 옛 이름을 보지 않고,
         *          지금 이름이 늘 옛 이름보다 먼저입니다. 바이너리 태그 스트림과 오브젝트 diff 가 함께 씁니다.
         */
        [[nodiscard]] static optional<uint32> findPropertyIndexByNameHash( PropertyListView listProp, uint32 nameHash );
        /**
         * @brief 위와 같되 @p expectedIndex(직전에 맞은 자리 + 1)를 먼저 봅니다 — 태그는 쓴 순서(= 선언 순서)로 오므로 보통 한 번 비교로 끝나고, 놓칠 때만 훑는다.
         * @details 훑기 두 번(지금 이름 · 옛 이름)이 필드마다라 형식 하나 읽기가 최악 n² 비교였다(언리얼 태그 프로퍼티 로드 · protobuf 생성 파서의 다음 필드 예측과 같은 자리).
         *          기대 자리는 지금 이름만 견준다 — 옛 이름은 지금 이름을 모두 본 뒤에만 보는 규칙이 그대로다.
         */
        [[nodiscard]] static optional<uint32> findPropertyIndexByNameHash( PropertyListView listProp, uint32 nameHash, uint32 expectedIndex );

        /**
         * @brief 이 문맥의 저장 목적에서 이 타입의 프로퍼티를 쓰고 읽는지 판정합니다. 세 형식(XML · JSON · 바이너리)과 diff 가 모두 이 함수를 지납니다.
         * @details 표는 `SerializePurpose` 설명에 있다. `Transient` 는 어느 목적도 쓰지 않고, `RuntimeState` 는 저작(Authoring)만 쓰지 않으며,
         *          세이브(SaveGame)는 `SaveGame` 필드가 하나라도 있는 타입(옵트인)이면 그 필드만 쓴다. 읽을 때 이 함수가 false 인 필드는 지금 값 그대로 둔다.
         *          핸들은 대상의 고정 지역 id · 키 해시로 적혀(감사 A-2) 다른 실행의 세이브에서도 같은 대상을 찾는다 — 세이브에서 빼지 않는다.
         * @param owner 프로퍼티를 가진 타입(상속 목록을 순회하는 그 타입). 옵트인 판정은 상속분까지 봅니다(`TypeInfo::hasSaveGameProperty`).
         */
        static bool shouldSerializeProperty( const TypeInfo& owner, const PropertyInfo& prop, const SerializeContext& context )
        {
            if ( prop._metadata->_bTransient == SW_TRUE )
                return false;
            switch ( context.getPurpose() )
            {
                case SerializePurpose::Authoring:
                    return prop._metadata->isAuthored();
                case SerializePurpose::Snapshot:
                    return true;
                case SerializePurpose::SaveGame:
                    return prop._metadata->_bSaveGame == SW_TRUE || owner.hasSaveGameProperty() == false;
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
            return context.getPurpose() == SerializePurpose::Authoring && prop._metadata->_bTransient == SW_FALSE && prop._metadata->_bRuntimeState == SW_TRUE;
        }

        /** @brief 저작 파일에 있으면 안 되는 필드(`isForbiddenInFile`)를 타입과 필드 이름으로 오류 로그에 남깁니다. */
        SW_API static void reportForbiddenProperty( const TypeInfo& owner, const PropertyInfo& prop );
    };
} // namespace sw

namespace sw
{
    /**
     * @class PropertyReadRollback
     * @brief 읽지 못한 필드를 넘기고 이어 읽는 읽기가 그 필드를 읽기 전 값으로 남기게 합니다. 세 형식(바이너리 태그 스트림, JSON, XML)의 소프트 읽기가 함께 씁니다.
     * @details 컨테이너 · 구조체는 원소와 필드를 채운 뒤 끝에서 실패하므로, 담지 않고 넘기면 읽다 만 값이 남는다. 그래서 필드를 읽기 전에
     *          `keepValueBeforeRead` 로 값을 이 빌드의 바이트(`SerializerUtil::writePropertyBinary`)로 담고, 읽기가 실패하면 `restoreValueBeforeRead` 로 되읽는다.
     *          되돌리는 단위는 실패를 받은 필드 하나다. 구조체 칸 안의 실패는 안쪽 읽기가 같은 규칙으로 그 안쪽 필드만 되돌린다.
     *          소유 포인터 목록은 담지 않는다. 되돌리면 팩토리가 컴포넌트를 새로 만들어 다른 객체가 된다. 그래서 그 목록의 읽기가 실패하면 되돌리지 않고 실패다.
     *          두 함수가 false 이면 부른 쪽은 객체째 실패한다(오류는 여기서 남긴다). 읽다 만 값이나 기본값으로 넘어가지 않는다.
     *          실패하면 객체째 버리는 엄격 읽기는 담지 않는다(담는 비용은 이어 읽는 읽기만 낸다).
     */
    class PropertyReadRollback
    {
    public:
        PropertyReadRollback();

        /** @brief @p prop 의 지금 값을 담습니다. 앞서 담은 값은 버립니다. 값을 바이트로 쓰지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool keepValueBeforeRead( const TypeInfo& ownerType, const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context );
        /**
         * @brief 마지막으로 담은 필드를 담은 값으로 되돌립니다. 그 필드의 읽기가 실패한 뒤에 부릅니다. @p instance 는 담을 때와 같은 객체입니다.
         * @return 소유 포인터 목록이거나(되돌릴 수 없다) 담은 바이트를 되읽지 못하면(코덱 결함) 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool restoreValueBeforeRead( MutableObjectAddress instance, const SerializeContext& context );
        /** @brief 지금까지 되돌린 칸입니다 — 소프트 읽기 끝의 범위 검사(`SerializerUtil::isWithinDeclaredRanges`)가 뺀다. 되돌릴 때마다 여기 담기므로 형식이 따로 적지 않는다. */
        [[nodiscard]] const vector<RestoredPropertyValue>& getRestoredValues() const { return _listRestored; }

    private:
        vector<RestoredPropertyValue> _listRestored;
        vector<uint8>                 _bytes;
        const TypeInfo*               _pOwnerType;
        const PropertyInfo*           _pProperty;
        uint8                         _bOwnedPointerList : 1;
    };
} // namespace sw
