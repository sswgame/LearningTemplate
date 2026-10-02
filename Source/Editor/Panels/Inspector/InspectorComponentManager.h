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

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class IInspectorComponent;

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
         * @details 예전에는 정확한 타입 이름으로만 찾아(`find`), 게임이 만든 SceneComponent 파생에는 트랜스폼 칸이 없었고 메시 · 카메라는 자기 확장이
         *          트랜스폼을 따로 복사해 그렸습니다. 언리얼 `IDetailCustomization` 처럼 하위 타입에도 걸립니다.
         */
        void collectForType( const TypeInfo& type, vector<IInspectorComponent*>& outListInspector ) const;
        void registerDefaults();

        template <typename TComponent, typename TInspector, typename... TArgs>
        void registerComponent( TArgs&&... args )
        {
            registerType( TComponent::StaticType()->_name.c_str(),
                          make_unique<TInspector>( std::forward<TArgs>( args )... ) );
        }

    private:
        map<string, unique_ptr<IInspectorComponent>> _mapInspector;
    };
} // namespace sw::editor
