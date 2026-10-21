/**
 * @file InspectorPropertyManager.h
 * @brief 프로퍼티 타입명 → IInspectorProperty (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    struct FunctionParameterInfo;

    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    struct EditorPropertyDrawerRegistration;
    struct InspectorMethodArgSlot;

    class IInspectorProperty;

    /** @brief 타입별 인스펙터 프로퍼티 UI 관리자 (EditorContext 소유) */
    class InspectorPropertyManager : public EditorSubsystem
    {
    public:
        InspectorPropertyManager();
        ~InspectorPropertyManager() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        SubsystemInitResult initializeSubsystem() override
        {
            registerDefaults();
            return SubsystemInitResult::Succeeded;
        }
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "InspectorPropertyManager";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        InspectorPropertyManager( const InspectorPropertyManager& )            = delete;
        InspectorPropertyManager& operator=( const InspectorPropertyManager& ) = delete;

        /** @brief 타입 이름에 위젯을 겁니다. 같은 이름이 있으면 바꿉니다. */
        void                registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty );
        IInspectorProperty* find( string_view typeName ) const;
        /** @brief `ReflectBuiltins.xxx` 의 내장 타입마다 위젯을 등록합니다(`InspectorWidgetFor<T>` 가 분기를 정합니다). */
        void registerDefaults();
        /** @brief 그리기 확장 등록 줄(`SW_EDITOR_PROPERTY_DRAWER`)과 맞춥니다. 세대가 같으면 바로 돌아갑니다. */
        void syncWithRegistry();
        /** @brief 등록 줄이 [@p pBegin, @p pEnd)(언로드되는 모듈 이미지) 안인 그리기를 지웁니다(가렸던 내장 위젯은 복원한다). 지운 수입니다. */
        uint32 releaseDrawersWithin( const void* pBegin, const void* pEnd );

        /**
         * @brief CallInEditor 인자 필드 하나를 그 C++ 타입의 위젯으로 그립니다. 값이 바뀌었으면 true 입니다.
         * @details 타입 핸들 인자(`FunctionParameterInfo::_handleTargetTypeName`)는 프로퍼티와 같은 선택기를 그린다 — @p pManager(편집 대상의 씬)에서 대상 타입의
         *          컴포넌트만 고를 수 있다. 씬이나 대상 타입이 없으면 선택기 대신 오류 글이다(정수 입력으로 물러서지 않는다).
         */
        static bool drawMethodArg( const utf8* pLabel, InspectorMethodArgSlot& slot, const FunctionParameterInfo& parameter, const GameObjectManager* pManager );

    private:
        /** @brief 내장 표(`ReflectBuiltins.xxx`)의 위젯을 모두 다시 겁니다. */
        void registerBuiltins();

        NameRegistry<unique_ptr<IInspectorProperty>>    _registry;         ///< 프로퍼티 타입 이름 → 위젯
        vector<const EditorPropertyDrawerRegistration*> _listRegistration; ///< 지금 걸려 있는 그리기 확장 등록 줄
        uint32                                          _syncedGeneration; ///< 마지막으로 맞춘 등록 세대
    };
} // namespace sw::editor
