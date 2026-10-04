/**
 * @file ReflectionValidation.h
 * @brief `PROPERTY( Validate = fn )` · `REFLECT( Validate = fn )` 검증 함수를 돌려 결과를 모읍니다(로드 · 저장 · 인스펙터 편집, 나중에 맵 검사 패널).
 * @details 검증 함수의 모양은 `void fn( ValidationContext& context ) const`(const 가 아니어도 된다)이고, 파서가 같은 타입에서 찾아 모양을 봅니다.
 *          함수는 문제를 `context.addError` · `addWarning` 으로 적기만 합니다 — 값을 고치거나 로드를 멈추지 않습니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw
{
    /** @brief 검증 결과의 무게입니다. */
    enum class ValidationSeverity : uint8
    {
        Warning,
        Error,
    };

    /** @brief 검증 결과 하나입니다. */
    struct ValidationIssue
    {
        string             _message;
        string             _sourceLabel;  ///< 어느 오브젝트(이름 · 경로)에서 났는가 — 부르는 쪽이 정한다
        hashed_string      _typeName;     ///< 검증 함수가 속한 타입(FQN)
        hashed_string      _propertyName; ///< 프로퍼티 검증이면 그 이름, 타입 검증이면 비어 있다
        uint64             _sourceId{ 0 };
        ValidationSeverity _severity{ ValidationSeverity::Warning };
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

        /** @brief 적힌 결과입니다. */
        const vector<ValidationIssue>& getIssues() const noexcept { return _listIssue; }
        /** @brief 오류가 하나라도 있으면 true 입니다. */
        bool hasError() const noexcept;
        /** @brief 결과를 비웁니다(출처 · 범위는 그대로). */
        void clear() { _listIssue.clear(); }

        /** @brief 결과에 붙일 출처(오브젝트)입니다. */
        void setSource( uint64 sourceId, string_view sourceLabel );
        /** @brief 지금 돌리는 검증 함수의 타입 · 프로퍼티입니다(`ReflectionValidation` 이 부른다). */
        void setScope( const hashed_string& typeName, const hashed_string& propertyName );

    private:
        void add( ValidationSeverity severity, string_view message );

        vector<ValidationIssue> _listIssue;
        string                  _sourceLabel;
        hashed_string           _typeName;
        hashed_string           _propertyName;
        uint64                  _sourceId;
    };
} // namespace sw

namespace sw
{
    /** @brief 타입의 검증 함수를 돌립니다. */
    struct SW_API ReflectionValidation
    {
        /**
         * @brief 인스턴스를 검증합니다 — 프로퍼티 검증(상속분 포함), 값으로 든 반사 구조체 · 그 시퀀스의 원소까지 내려가고, 타입 검증은 기반부터.
         * @return 이번에 적힌 결과 수
         */
        static uint32 validateObject( const TypeInfo& type, const void* pInstance, ValidationContext& context );
        /**
         * @brief 이 타입(기반 · 값으로 든 구조체 포함)에 검증 함수가 하나라도 있으면 true 입니다. 없으면 `validateObject` 를 부를 까닭이 없다.
         * @details 답은 `TypeInfo` 에 캐시하고 등록 · 해제가 비웁니다(`clearInheritedProperties`).
         */
        static bool hasValidator( const TypeInfo& type );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 검증 결과를 출처(오브젝트)마다 모아 두는 표입니다. 같은 출처를 다시 검증하면 그 출처의 결과를 바꿉니다.
     * @details 맵 검사 패널 · 쿠커 · 시험이 읽습니다. 엔진 안에 하나라 모듈이 다시 올라와도 남습니다. 여러 스레드(로드 워커)에서 쓸 수 있습니다.
     */
    class SW_API ValidationIssueLog
    {
    public:
        static ValidationIssueLog& get();

        /** @brief @p sourceId 의 결과를 @p listIssue 로 바꿉니다(비었으면 그 출처를 지운다). */
        void replaceIssues( uint64 sourceId, const vector<ValidationIssue>& listIssue );
        /** @brief 출처 하나의 결과를 지웁니다(오브젝트가 사라졌다). */
        void removeSource( uint64 sourceId );
        /** @brief 모든 결과를 출처 순서 없이 모읍니다. */
        void collectIssues( vector<ValidationIssue>& outListIssue ) const;
        /** @brief 모든 결과의 수입니다. */
        uint32 getIssueCount() const;
        /** @brief 모두 지웁니다. */
        void clear();

    private:
        ValidationIssueLog() = default;

        mutable mutex           _mutex;
        vector<ValidationIssue> _listIssue;
    };
} // namespace sw
