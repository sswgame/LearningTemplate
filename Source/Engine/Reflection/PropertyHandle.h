/**
 * @file PropertyHandle.h
 * @brief 오래 드는 프로퍼티 참조 — (선언 타입, 색인, 세대)입니다. `const PropertyInfo*` 를 프레임을 넘겨 들지 않는다.
 * @details `PropertyInfo` 는 선언 타입의 `_listProperty` 원소다. 그 타입이 다시 등록되거나(핫 리로드) 모듈과 함께 내려가면 목록이 바뀌어 들고 있던 포인터가
 *          매달린다. 이 핸들은 선언 타입의 주소(레지스트리가 고정한다) · 목록 색인 · 등록 세대(`TypeInfo::_contentGeneration`)를 들고, 풀 때 세대가
 *          다르면 nullptr 를 준다 — 매달린 포인터 대신 "낡음" 이 보인다. 언리얼 `TFieldPath`(소유 구조체 + 이름, 소유자 직렬 번호로 해소 캐시를 버린다)와 같은 자리.
 *          한 프레임 안의 지역 변수 · 같은 호출 안의 목록은 그대로 `const PropertyInfo*` 를 쓴다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    /** @struct PropertyHandle @brief (선언 타입, 색인, 세대) 프로퍼티 참조입니다. 파일 머리말 참고. */
    struct SW_API PropertyHandle
    {
        const TypeInfo* _pType{ nullptr };                          ///< 선언 타입(`TypeInfo` 의 주소는 레지스트리 수명 동안 고정)
        uint32          _index{ constant::invalid_index::kUint32 }; ///< 선언 타입 `_listProperty` 의 색인
        uint32          _generation{ 0 };                           ///< 만들 때의 `TypeInfo::_contentGeneration`

        /**
         * @brief @p type 이나 그 기반이 선언한 @p property 의 핸들입니다(`getPropertiesWithBase` · `findPropertyInHierarchy` 가 준 원소).
         * @details 선언 타입은 @p type 의 사슬에서 원소 주소로 찾는다. 사슬에 없는 원소면 멈춘다(SW_FATAL) — 다른 타입의 프로퍼티를 넘긴 결함이다.
         */
        static PropertyHandle of( const TypeInfo& type, const PropertyInfo& property );

        /** @brief 비어 있지 않으면 true 입니다(낡았는지는 `find` 가 본다). */
        bool isSet() const { return _pType != nullptr; }
        /** @brief 지금의 프로퍼티입니다. 선언 타입이 내려갔거나 다시 등록됐으면(세대가 다르면) nullptr 입니다. */
        const PropertyInfo* find() const;
        /** @brief 지금의 프로퍼티입니다. 낡았으면 멈춥니다 — `find` 로 먼저 확인한 자리에서만 씁니다. */
        const PropertyInfo& get() const;
    };
} // namespace sw
