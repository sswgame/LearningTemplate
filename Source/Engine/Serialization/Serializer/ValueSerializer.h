/**
 * @file ValueSerializer.h
 * @brief 값 하나(프로퍼티 오프셋을 더한 주소)를 바이너리 · 글로 쓰고 읽습니다. 직렬화기 셋(Binary · JSON · XML) · diff · 아카이브가 함께 쓰는 값 경로입니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    class ReflectValueBinding;
    enum class FieldReadResult : uint8;

    struct ValueSerializer
    {
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
        /**
         * @brief 값을 바이너리로 직렬화합니다(이 빌드의 버전, `kCurrentBinaryWireVersion`). @p value 는 값 하나 자체의 주소입니다(프로퍼티 오프셋을 더하지 않는다).
         * @details enum 은 열거자 정체성으로 적는다. 값이 아니라 열거자 이름의 해시(uint32)를 적고, 비트플래그는 켜진 이름의 수(uint32)와 그 해시들을
         *          (해시 오름차순이므로 같은 값이면 같은 바이트다) 적는다. 이름이 없는 값이나 이름이 덮지 못한 비트는 해시 위치에 0 을 두고 int64 값을 이어 붙인다.
         *          XML 과 JSON 이 이름을 적는 것과 같은 규칙이라, 열거자 순서를 바꾸거나 사이에 넣어도 저장된 뜻이 그대로다.
         * @return 바이너리 처리기 · 열거형 · 구조체 · 텍스트 처리기 어디에도 없는 타입이면(`hasBinaryValueCodec` 이 거짓) 아무 바이트도 쓰지 않고 false 입니다.
         *         구조체 안의 값이 실패해도 false 입니다.
         */
        [[nodiscard]] SW_API static bool serializeValueBinary( ValueAddress value, const hashed_string& typeName,
                                                               vector<uint8>& listBuffer, const SerializeContext& context );
        /** @brief 값 하나를 바이너리로 쓰고 읽을 수 있는 타입인가입니다. `serializeValueBinary` · `deserializeValueBinary` 가 고르는 순서와 같습니다. */
        [[nodiscard]] SW_API static bool hasBinaryValueCodec( const hashed_string& typeName, const SerializeContext& context );
        /**
         * @brief 바이너리에서 값을 역직렬화합니다.
         * @param value 값 하나 자체의 주소입니다(프로퍼티 오프셋을 더하지 않는다). 프로퍼티는 `PropertySerializer::applyPropertyBinary` 로 읽습니다.
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

        /** @brief 값 타입 하나를 세 형식이 모두 실어 나를 수 있으면 true 입니다(텍스트 핸들러, enum, 리플렉션 구조체). */
        SW_API static bool canCarryValueType( hashed_string typeName, const SerializeContext& context );

        /**
         * @brief 값 하나 자체를 텍스트로 씁니다. `formatPropertyText` 의 값 부분과 같은 형태이고, 짝은 `parseTextValue` 입니다.
         * @details 객체 주소와 값 주소를 한 함수로 받으면 값 주소에 오프셋이 한 번 더 더해진다(인스펙터 핸들 선택기가 고른 값을 `0:0` 으로 썼다).
         *          그래서 값 주소(`ValueAddress`)는 프로퍼티를 받지 않는 이 함수가 받고, 객체 주소와는 형식이 달라 서로 넘길 수 없다.
         *          오프셋 0 짜리 가짜 `PropertyInfo` 를 만들어 `formatPropertyText` 에 넘기지 않는다.
         */
        [[nodiscard]] SW_API static string formatValueText( ValueAddress value, const hashed_string& typeName, const SerializeContext& context );

        /**
         * @brief Alias 로 들어온 이름을 `SerializeContext` 핸들러가 아는 원본 이름(`_name`)으로 바꿉니다.
         * @details `ValueSerializer` 과 `JSONSerializer` 가 함께 쓰는 핸들러 조회 규칙이다.
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
         * @brief 원소 타입 이름이 소유 포인터인지 확인합니다(이름에 `*` 가 있으면 소유 포인터입니다).
         * @details `JSONSerializer` 와 `XMLSerializer` 가 함께 쓴다. 판정 기준이
         *          "이름에 별표가 있는가"라는 문자열 규칙이라, 한쪽만 고치면 두 포맷이 서로 다른
         *          컨테이너를 소유로 보게 된다.
         */
        SW_API static bool isOwnedPointerElementType( hashed_string elementTypeName );
    };
} // namespace sw
