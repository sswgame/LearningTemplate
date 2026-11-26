/**
 * @file ReflectionValidation.h
 * @brief 검증 함수(`PROPERTY( Validate = fn )` · `REFLECT( Validate = fn )` · `REFLECT( ReadValidate = fn )`)를 돌려 결과를 모읍니다.
 * @details 검증 함수의 모양은 `void fn( ValidationContext& context ) const`(const 가 아니어도 된다)이고, 파서가 같은 타입에서 찾아 모양을 봅니다.
 *          함수는 문제를 `context.addError` · `addWarning` 으로 적기만 합니다 — 값을 고치거나 로드를 멈추지 않습니다.
 *          `Validate` 는 프로퍼티든 타입이든 에디터 데이터 검증이다(로드 · 저장 · 인스펙터 · 맵 검사). 배포에는 등록되지 않는다(생성 코드의 `SW_DEV_ONLY_FUNCTION`,
 *          언리얼 `UObject::IsDataValid` 가 `WITH_EDITOR` 인 것과 같다). `ReadValidate` 는 배포에서도 도는 읽기 검사다 — 스키마 결합형 읽기 끝과 세이브 읽기가
 *          오류면 거절한다(언리얼 `NetSerialize` 의 `bOutSuccess` 자리). 읽기 거절은 `validateRead` 로 읽기 검사만 돌려 두 구성이 같은 답을 낸다.
 *          결과 표(`Dev/ValidationIssueLog.h`)와 고침도 개발 전용이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct TypeInfo;
    struct TypeResolver;
} // namespace sw

namespace sw
{
    /** @brief 검증 결과의 무게입니다. */
    enum class ValidationSeverity : uint8
    {
        Warning,
        Error,
    };

    /**
     * @brief 검증 결과의 안전한 고침입니다 — 그 오브젝트의 한 프로퍼티를 한 값(프로퍼티 텍스트 형식)으로 바꾼다. 데이터일 뿐이고 적용은 맵 검사 패널이 한다(되돌리기 한 단계).
     * @details 값이 분명할 때만 둔다(기본값으로 되돌리기 · 런타임이 이미 쓰는 값을 적기). 모호하거나 데이터를 잃을 수 있는 결과는 고침 없이 적는다.
     *          언리얼 Map Check 의 수정 가능 메시지 · Godot 의 구성 경고 빠른 수정과 같은 자리다.
     */
    struct ValidationPropertyEdit
    {
        hashed_string _propertyName; ///< 바꿀 프로퍼티(비면 고침 없음). 적을 때 비우면 검증 범위의 프로퍼티다
        string        _valueText;    ///< 그 프로퍼티의 텍스트 값(`PropertySerializer::formatPropertyText` 와 같은 형식)
        string        _description;  ///< 패널 단추 툴팁 · 되돌리기 이름("Set Radius to 6 m")

        /** @brief 고칠 프로퍼티가 있으면 true 입니다. */
        bool isValid() const noexcept { return _propertyName.empty() == false; }
    };

    /** @brief 검증 결과 하나입니다. */
    struct ValidationIssue
    {
        ValidationPropertyEdit _fix; ///< 안전한 고침. `_fix.isValid()` 가 false 면 손으로 고친다
        string                 _message;
        string                 _sourceLabel;  ///< 어느 오브젝트(이름 · 경로)에서 났는가 — 부르는 쪽이 정한다
        hashed_string          _typeName;     ///< 검증 함수가 속한 타입(FQN)
        hashed_string          _propertyName; ///< 프로퍼티 검증이면 그 이름, 타입 검증이면 비어 있다
        uint64                 _sourceID{ 0 };
        ValidationSeverity     _severity{ ValidationSeverity::Warning };
    };
} // namespace sw

namespace sw
{
    /** @brief 검증 함수가 결과를 적는 곳입니다. 지금 어느 타입 · 프로퍼티를 보는지는 돌리는 쪽(`ReflectionValidation`)이 채웁니다. */
    class SW_API ValidationContext
    {
    public:
        ValidationContext();

        /** @brief 문제를 적습니다(로드 · 저장은 계속된다). */
        void addError( string_view message );
        /** @brief 경고를 적습니다. */
        void addWarning( string_view message );
        /** @brief 안전한 고침이 있는 문제를 적습니다. @p fix 의 프로퍼티가 비면 지금 범위의 프로퍼티입니다. 개발 전용(`Dev/ValidationIssueLog.cpp`)이라 배포 코드는 부르지 못한다. */
        void addErrorWithFix( string_view message, const ValidationPropertyEdit& fix );
        /** @brief 안전한 고침이 있는 경고를 적습니다. 개발 전용이다. */
        void addWarningWithFix( string_view message, const ValidationPropertyEdit& fix );

        /** @brief 적힌 결과입니다. */
        const vector<ValidationIssue>& getIssues() const noexcept { return _listIssue; }
        /** @brief 오류가 하나라도 있으면 true 입니다. */
        bool hasError() const noexcept;
        /** @brief 결과를 비웁니다(출처 · 범위는 그대로). */
        void clear() { _listIssue.clear(); }

        /** @brief 결과에 붙일 출처(오브젝트)입니다. */
        void setSource( uint64 sourceID, string_view sourceLabel );
        /** @brief 지금 돌리는 검증 함수의 타입 · 프로퍼티입니다(`ReflectionValidation` 이 부른다). */
        void setScope( const hashed_string& typeName, const hashed_string& propertyName );

    private:
        void add( ValidationSeverity severity, string_view message, const ValidationPropertyEdit& fix );

    private:
        vector<ValidationIssue> _listIssue;
        string                  _sourceLabel;
        hashed_string           _typeName;
        hashed_string           _propertyName;
        uint64                  _sourceID;
    };
} // namespace sw

namespace sw
{
    /** @brief 타입의 검증 함수를 돌립니다. */
    struct SW_API ReflectionValidation
    {
        /**
         * @brief 인스턴스를 검증합니다 — 프로퍼티 검증(상속분 포함), 값으로 든 반사 구조체 · 그 시퀀스의 원소까지 내려가고, 타입 검증은 기반부터.
         * @details 에디터 데이터 검증이다(로드 · 저장 · 인스펙터 · 맵 검사). 배포에는 `Validate` 가 등록되지 않아 읽기 검사만 남으므로, 읽은 값을 거절하는 자리는 `validateRead` 를 쓴다.
         * @return 이번에 적힌 결과 수
         */
        static uint32 validateObject( const TypeInfo& type, const void* pInstance, ValidationContext& context );
        /**
         * @brief 읽기 검사(`REFLECT( ReadValidate )`)만 돌립니다 — 값으로 든 구조체 · 그 시퀀스 원소까지 내려가고 기반부터. 배포와 개발 구성이 같은 답을 낸다.
         * @details 세이브 · 네트워크에서 읽은 값을 거절할지 정하는 자리가 쓴다(`FightingMatch::serializeState`, 스키마 결합형 읽기 끝은 스키마가 같은 함수를 부른다).
         * @return 이번에 적힌 결과 수
         */
        static uint32 validateRead( const TypeInfo& type, const void* pInstance, ValidationContext& context );
        /**
         * @brief 이 타입(기반 · 값으로 든 구조체 포함)에 검증 함수가 하나라도 있으면 true 입니다. 없으면 `validateObject` 를 부를 까닭이 없다.
         * @details 답은 `TypeInfo` 의 판정 비트다 — 등록 · 해제가 바뀐 타입과 자손만 다시 구한다(`TypeRegistry::relinkAffectedLocked` → `TypeInfo::computeDerivedFlags`).
         */
        static bool hasValidator( const TypeInfo& type );
        /** @brief 위 판정을 지금 구합니다(캐시하지 않는다). 칸 타입을 재귀로 따라가므로 칸 타입의 상속 목록이 지어져 있어야 한다. 칸 타입은 @p resolver 로 찾는다(레지스트리 잠금 안에서도 부른다). */
        static bool computeHasValidator( const TypeInfo& type, const TypeResolver& resolver );
        /** @brief 읽기 검사(값으로 든 구조체 포함)가 하나라도 있으면 true 입니다(`TypeInfo` 의 판정 비트). */
        static bool hasReadValidator( const TypeInfo& type );
        /** @brief 위 판정을 지금 구합니다(캐시하지 않는다, `computeHasValidator` 와 같은 조건). */
        static bool computeHasReadValidator( const TypeInfo& type, const TypeResolver& resolver );
    };
} // namespace sw
