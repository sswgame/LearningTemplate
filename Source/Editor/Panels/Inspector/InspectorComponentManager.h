/**
 * @file InspectorComponentManager.h
 * @brief 컴포넌트 타입명 → IInspectorComponent (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class IInspectorComponent;

    /**
     * @struct EditorInspectorRegistration
     * @brief 컴포넌트 인스펙터 확장 하나의 등록 줄입니다. 확장의 .cpp 가 `SW_EDITOR_INSPECTOR` 로 둡니다.
     * @details id 는 컴포넌트 클래스 이름입니다. 컴포넌트 하나에 확장은 하나이고, 둘째 등록은 거절됩니다. 그리는 순서는 등록 순서가
     *          아니라 타입 계층(기반 → 파생)이 정하므로 `_order` 는 쓰지 않습니다.
     */
    struct EditorInspectorRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "inspector";

        const TypeInfo* ( *_pGetComponentType )();
        unique_ptr<IInspectorComponent> ( *_pCreate )();
    };

    /** @brief 등록 줄이 가리키는 확장 생성 함수입니다. */
    template <typename TInspector>
    unique_ptr<IInspectorComponent> createInspectorComponent()
    {
        return make_unique<TInspector>();
    }

    /** @brief 컴포넌트 타입별 인스펙터 UI 관리자 (EditorContext 소유) */
    class InspectorComponentManager
    {
    public:
        InspectorComponentManager()  = default;
        ~InspectorComponentManager() = default;

        void                 registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector );
        IInspectorComponent* find( string_view typeName ) const;
        /**
         * @brief 이 타입과 그 기반들에 등록된 확장을 기반 → 파생 순서로 모읍니다.
         * @details 언리얼 `IDetailCustomization` 처럼 하위 타입에도 걸립니다. 게임이 만든 SceneComponent 파생에도 트랜스폼 칸이 그려집니다.
         */
        void collectForType( const TypeInfo& type, vector<IInspectorComponent*>& outListInspector ) const;
        /** @brief `SW_EDITOR_INSPECTOR` 로 등록된 확장을 모두 만들어 둡니다. */
        void registerDefaults();

    private:
        map<string, unique_ptr<IInspectorComponent>> _mapInspector;
    };
} // namespace sw::editor

/**
 * @brief 컴포넌트 인스펙터 확장을 그 확장의 .cpp 에서 등록합니다. 예: `SW_EDITOR_INSPECTOR( CameraComponent, CameraComponentInspector );`
 * @param TComponent 리플렉션 타입(`StaticType()`)이 있는 컴포넌트
 * @param TInspector 기본 생성자가 있는 `IInspectorComponent` 구현
 */
#define SW_EDITOR_INSPECTOR( TComponent, TInspector )                                                          \
    SW_EDITOR_REGISTER( ::sw::editor::EditorInspectorRegistration, Inspector_##TInspector, { #TComponent, 0 }, \
                        &TComponent::StaticType, &::sw::editor::createInspectorComponent<TInspector> )
