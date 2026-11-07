/**
 * @file FunctionInfo.h
 * @brief 함수 · 인자 · 이벤트의 리플렉션 정보입니다.
 */
#pragma once
#include "Engine/Reflection/ReflectionMetadataBox.h"

namespace sw
{
    /** @brief 함수나 이벤트의 인자 하나입니다. 인자 순서는 선언 순서입니다. */
    struct SW_API FunctionParameterInfo
    {
        string _name;     ///< 선언에 적힌 이름. 이름 없이 적었으면 비어 있다
        string _typeName; ///< 정규 타입 이름이다(`int32`, `string`, `DamageEvent`). const 와 참조는 뗀다
        /** @brief 기본 인자의 C++ 식 그대로입니다(`1.0f`, `"idle"`, `Mode::Fast`). 없으면 비어 있습니다. 호출에서 인자를 생략하면 `ReflectionInvoke` 가 이 값을 읽어 넣습니다. */
        string _defaultValue;
        /** @brief 이 인자 타입의 이름과 값 변환(`ReflectTypeOpsOf<T>::kOps`)입니다. 이벤트는 `EventInfo::setOps` 가 채웁니다. */
        const ReflectTypeOps* _pType;
        /**
         * @brief 타입 핸들 인자(`TComponentHandle<T>`)의 대상 `T` 입니다. 타입 핸들이 아니면 비어 있습니다(프로퍼티의 `PropertyInfo::_handleTargetTypeName` 과 같다).
         * @details 이때 `_typeName` 은 `ComponentHandle` 이다 — 부르는 쪽은 타입 없는 핸들을 넣고, 생성한 호출 썽크가 그 컴포넌트의 씬으로 대상 타입을 확인한다
         *          (`ComponentHandleArgument::cast`, 틀리면 오류를 남기고 부르지 않는다). 인스펙터는 이 이름으로 선택기를 그린다.
         */
        hashed_string _handleTargetTypeName;

        FunctionParameterInfo() noexcept;
        FunctionParameterInfo( string name, string typeName, string defaultValue, const ReflectTypeOps* pType, hashed_string handleTargetTypeName = {} );

        /** @brief 기본 인자가 있으면 true 입니다. */
        bool hasDefaultValue() const noexcept { return _defaultValue.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /// @brief 리플렉션 메서드의 정보입니다. 이름, 시그니처, invoker 를 담습니다.
    struct SW_API FunctionInfo
    {
        string                               _name;
        hashed_string                        _hashName;
        string                               _returnTypeName; ///< 정규 타입 이름(예: void, int32)
        vector<FunctionParameterInfo>        _listParameter;  ///< 선언 순서대로 나열한 인자다. 이름, 타입, 기본 인자를 담는다
        ReflectMetadataBox<FunctionMetadata> _metadata;       ///< `FUNCTION()` 에 적은 메타데이터(`ReflectionMetadata.h`)
        /**
         * @brief 인자 묶음을 받아 메서드를 부르는 함수입니다.
         * @details 인자는 **그 C++ 타입 그대로**(`args.get<T>`) 넣어야 한다. 타입이 다르면 Debug 는 단언하고 Release 는 잘못 읽는다.
         *          타입을 모르는 쪽(콘솔, 비주얼 스크립팅)은 `ReflectionInvoke::call` 로 부른다. 이 함수는 인자를 바꿔 넣어 준다.
         */
        Delegate<TaskValue( void*, const TaskArgs& )> _invoker;
        /** @brief 반환 타입의 이름과 값 변환입니다. void 이거나 생성자면 nullptr 입니다. */
        const ReflectTypeOps* _pReturnType;

        /** @brief 빈 메서드 정보를 만듭니다(메타데이터 상자의 기본 생성이 `ReflectionMetadata.h` 를 봐야 해서 `Reflection.cpp` 에 둔다). */
        FunctionInfo() noexcept;

        /** @brief 인자 개수입니다. */
        uint32 getParameterCount() const noexcept { return static_cast<uint32>( _listParameter.size() ); }

        /** @brief 커스텀 메타데이터 태그를 조회합니다. */
        const string* findCustomMeta( const hashed_string& key ) const noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 리플렉션 이벤트입니다. `PROPERTY()` 를 붙인 멀티캐스트 델리게이트 필드(`MulticastDelegate<void( Args... )>`)가 이벤트가 됩니다.
     * @details 이벤트는 값이 아니라 구독 목록이라서 직렬화와 인스펙터의 값 편집 대상이 아니다. 이름으로 찾은 뒤(`TypeInfo::findEventInHierarchy`)
     *          타입을 모른 채로 바인딩, 해제, 호출한다(`ReflectionInvoke::bindEvent`, `broadcastEvent`). 에디터, 비주얼 스크립팅, 기믹 배선이 이 방식을 쓴다.
     */
    struct SW_API EventInfo
    {
        hashed_string                        _name;
        vector<FunctionParameterInfo>        _listParameter; ///< 이름은 선언에서 가져오고, 변환 테이블은 `setOps` 가 템플릿 인자에서 채운다
        ReflectMetadataBox<PropertyMetadata> _metadata;      ///< 표시용 메타(Category, DisplayName, Tooltip, Meta, HideInInspector)만 쓴다
        /** @brief 바인딩, 해제, 호출, 인자 변환을 담은 테이블입니다(`ReflectEventOpsOf<필드 타입>::kOps`). */
        const ReflectEventOps* _pOps;
        size_t                 _offset; ///< 인스턴스 안에서 델리게이트 필드가 있는 오프셋이다

        EventInfo() noexcept;

        /** @brief 테이블을 등록하고, 인자 변환 테이블을 템플릿 인자에서 채웁니다. 선언에서 이름을 읽지 못한 인자도 슬롯은 채웁니다. */
        void setOps( const ReflectEventOps* pOps );

        /** @brief 인스턴스 안 델리게이트 필드의 주소입니다. */
        void* getEventPtr( void* pInstance ) const noexcept { return reinterpret_cast<utf8*>( pInstance ) + _offset; }
        /** @brief 인스턴스 안 델리게이트 필드의 주소입니다. */
        const void* getEventPtr( const void* pInstance ) const noexcept { return reinterpret_cast<const utf8*>( pInstance ) + _offset; }
        /** @brief 인자 개수입니다. */
        uint32 getParameterCount() const noexcept { return static_cast<uint32>( _listParameter.size() ); }
    };
} // namespace sw
