/**
 * @file PropertySerializer.h
 * @brief 프로퍼티 값 하나(비트필드 · 컨테이너 · 값)를 옮기고 · 견주고 · 글과 바이트로 쓰고 읽습니다. 저장 목적마다 쓰는 칸을 고르는 판정도 여기 있습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

namespace sw
{
    struct PropertySerializer
    {
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
         * @brief 이름이 같은 칸끼리 값을 옮깁니다(@p srcType → @p dstType, 글로 옮기므로 타입은 글 형식이 같으면 된다). 짝이 없는 칸은 건너뜁니다.
         * @details 한 데이터를 종류마다 다른 스키마로 읽고 쓰는 곳(행동 트리 노드 — 종류마다 받는 칸이 다르다)이 칸마다 손으로 옮기지 않게 한다. 옮기지 못한 칸이 있으면 false 입니다.
         */
        [[nodiscard]] SW_API static bool copySameNamedProperties( const void* pSrc, const TypeInfo& srcType, void* pDst, const TypeInfo& dstType );

        /** @brief 키 문자열이 철자 그대로 같은지 비교합니다(대소문자가 다르면 다른 키 — 별칭 없음). */
        static bool isKeyEqual( string_view left, string_view right );

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
