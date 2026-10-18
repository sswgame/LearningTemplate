#include "pch.h"

#include "Editor/Panels/Inspector/InspectorComponentPicker.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    const TypeInfo* InspectorComponentPickerUtil::findTargetType( const TypeRegistry& registry, const PropertyInfo& prop )
    {
        if ( prop._handleTargetTypeName.empty() )
            return nullptr;
        return registry.findType( prop._handleTargetTypeName );
    }

    string InspectorComponentPickerUtil::makeLabel( const Component& component )
    {
        const GameObject* pOwner = component.getOwner();
        string            label  = pOwner != nullptr ? string( pOwner->getName().c_str() ) : string();
        label += " / ";
        label += ComponentStableKey::makeKey( &component );
        return label;
    }

    void InspectorComponentPickerUtil::collectChoices( const GameObjectManager& manager, const TypeInfo& targetType, vector<InspectorComponentChoice>& outListChoice )
    {
        outListChoice.clear();
        // 콜백은 공유 잠금 안이라 구조를 바꾸지 않는다. 핸들과 라벨만 모은다.
        manager.forEachComponentOfType( &targetType, [&outListChoice]( Component* pComp )
        { outListChoice.push_back( InspectorComponentChoice{ pComp->getHandle(), makeLabel( *pComp ) } ); } );
        // 방문 순서는 약속되지 않는다.
        std::sort( outListChoice.begin(), outListChoice.end(), []( const InspectorComponentChoice& lhs, const InspectorComponentChoice& rhs )
        { return lhs._label < rhs._label; } );
    }

    InspectorHandleState InspectorComponentPickerUtil::computeHandleState( const GameObjectManager& manager, const TypeInfo& targetType, ComponentHandle handle )
    {
        if ( handle.isValid() == false )
            return InspectorHandleState::Empty;
        // 타입 없는 판은 삭제 예정이면 nullptr 이다. 타입 핸들 판은 타입이 틀리면 단언하므로 쓰지 않는다.
        const Component* pComp = manager.resolveComponent( handle );
        if ( pComp == nullptr )
            return InspectorHandleState::Missing;
        const TypeInfo* pType = pComp->findCachedTypeInfo();
        if ( pType == nullptr || pType->isDerivedFrom( &targetType ) == false )
            return InspectorHandleState::WrongType;
        return InspectorHandleState::Valid;
    }
} // namespace sw::editor
