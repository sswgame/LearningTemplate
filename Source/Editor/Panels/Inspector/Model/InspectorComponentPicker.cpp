#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorComponentPicker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorSceneWalk.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    const TypeInfo* InspectorComponentChoice::findTargetType( const TypeRegistry& registry, const PropertyInfo& prop )
    {
        return findTargetType( registry, prop._handleTargetTypeName );
    }

    const TypeInfo* InspectorComponentChoice::findTargetType( const TypeRegistry& registry, const hashed_string& targetTypeName )
    {
        if ( targetTypeName.empty() )
            return nullptr;
        return registry.findType( targetTypeName );
    }

    string InspectorComponentChoice::makeLabel( const Component& component )
    {
        const GameObject* pOwner = component.getOwner();
        string            label  = pOwner != nullptr ? string( pOwner->getName().c_str() ) : string();
        label += " / ";
        label += ComponentStableKey::makeKey( &component );
        return label;
    }

    void InspectorComponentChoice::collectChoices( const GameObjectManager& manager, const TypeInfo& targetType, vector<InspectorComponentChoice>& outListChoice )
    {
        outListChoice.clear();
        // 콜백은 공유 잠금 안이라 구조를 바꾸지 않는다. 핸들과 라벨만 모은다.
        manager.forEachComponentOfType( editor::EditorSceneWalk::getKey(), &targetType, [&outListChoice]( Component* pComp )
        { outListChoice.push_back( InspectorComponentChoice{ pComp->getHandle(), makeLabel( *pComp ) } ); } );
        // 방문 순서는 약속되지 않는다.
        VectorUtil::sort( outListChoice, []( const InspectorComponentChoice& lhs, const InspectorComponentChoice& rhs )
        { return lhs._label < rhs._label; } );
    }

    InspectorHandleState InspectorComponentChoice::computeHandleState( const GameObjectManager& manager, const TypeInfo& targetType, ComponentHandle handle )
    {
        if ( handle.isValid() == false )
            return InspectorHandleState::Empty;
        // 타입 핸들 버전은 타입이 틀리면 단언하므로 쓰지 않는다. 타입 없는 버전은 삭제 대기 컴포넌트도 돌려주므로(`GameObjectStore::resolveComponent` 가
        // 삭제 대기를 포함해 찾는다) 표시를 따로 본다. 순회(`collectChoices`)가 건너뛰는 대상과 같은 기준이다.
        const Component* pComp = manager.resolveComponent( handle );
        if ( pComp == nullptr || pComp->isPendingDestroy() )
            return InspectorHandleState::Missing;
        const TypeInfo* pType = pComp->findCachedTypeInfo();
        if ( pType == nullptr || pType->isDerivedFrom( &targetType ) == false )
            return InspectorHandleState::WrongType;
        return InspectorHandleState::Valid;
    }

    string InspectorComponentChoice::makePreview( const GameObjectManager& manager, ComponentHandle value, InspectorHandleState state )
    {
        fixed_string<constant::buffer::kMax512> preview;
        switch ( state )
        {
            case InspectorHandleState::Empty:
            {
                return string( "(none)" );
            }
            case InspectorHandleState::Missing:
            {
                formatstring( preview.data(), preview.capacity(), "(missing) object %# / component %#", value.objectID(), value.componentID() );
                return string( preview.c_str() );
            }
            case InspectorHandleState::Valid:
            case InspectorHandleState::WrongType:
            {
                break;
            }
        }
        const Component* pComp = manager.resolveComponent( value );
        const string     label = pComp != nullptr ? makeLabel( *pComp ) : string();
        formatstring( preview.data(), preview.capacity(), "%#%#", state == InspectorHandleState::WrongType ? "(wrong type) " : "", label );
        return string( preview.c_str() );
    }
} // namespace sw::editor
