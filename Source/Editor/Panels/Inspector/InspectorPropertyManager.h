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

namespace sw::editor
{
    struct EditorPropertyDrawerRegistration;
    struct InspectorMethodArgSlot;

    class IInspectorProperty;

    /** @brief 타입별 인스펙터 프로퍼티 UI 관리자 (EditorContext 소유) */
    class InspectorPropertyManager
    {
    public:
        InspectorPropertyManager();
        ~InspectorPropertyManager();
        InspectorPropertyManager( const InspectorPropertyManager& )            = delete;
        InspectorPropertyManager& operator=( const InspectorPropertyManager& ) = delete;

        /** @brief 타입 이름에 위젯을 겁니다. 같은 이름이 있으면 바꿉니다. */
        void                registerType( string_view typeName, unique_ptr<IInspectorProperty> pProperty );
        IInspectorProperty* find( string_view typeName ) const;
        /** @brief `ReflectBuiltins.xxx` 의 내장 타입마다 위젯을 등록합니다(`InspectorWidgetFor<T>` 가 갈래를 정합니다). */
        void registerDefaults();
        /** @brief 그리기 확장 등록 줄(`SW_EDITOR_PROPERTY_DRAWER`)과 맞춥니다. 세대가 같으면 바로 돌아갑니다. */
        void syncWithRegistry();
        /** @brief 등록 줄이 [@p pBegin, @p pEnd)(언로드되는 모듈 이미지) 안인 그리기를 지웁니다(가렸던 내장 위젯은 되살린다). 지운 수입니다. */
        uint32 releaseDrawersWithin( const void* pBegin, const void* pEnd );

        /** @brief CallInEditor 인자 칸 하나를 그 C++ 타입의 위젯으로 그립니다. 값이 바뀌었으면 true 입니다. */
        static bool drawMethodArg( const utf8* pLabel, InspectorMethodArgSlot& slot );

    private:
        /** @brief 내장 표(`ReflectBuiltins.xxx`)의 위젯을 모두 다시 겁니다. */
        void registerBuiltins();

        NameRegistry<unique_ptr<IInspectorProperty>>    _registry;         ///< 프로퍼티 타입 이름 → 위젯
        vector<const EditorPropertyDrawerRegistration*> _listRegistration; ///< 지금 걸려 있는 그리기 확장 등록 줄
        uint32                                          _syncedGeneration; ///< 마지막으로 맞춘 등록 세대
    };
} // namespace sw::editor
