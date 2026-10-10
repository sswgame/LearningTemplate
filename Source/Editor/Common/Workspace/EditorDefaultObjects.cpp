#include "pch.h"

#include "Editor/Common/Workspace/EditorDefaultObjects.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw::editor
{
    EditorDefaultObjects::EditorDefaultObjects()
        : _pManager{}
        , _listType{}
        , _listDefault{}
    {
    }

    EditorDefaultObjects::~EditorDefaultObjects()
    {
        clear();
    }

    const Component* EditorDefaultObjects::findDefault( const TypeInfo& type )
    {
        for ( size_t index = 0; index < _listType.size(); ++index )
        {
            if ( _listType[index] == &type )
                return _listDefault[index];
        }
        if ( type._addComponent == nullptr )
            return nullptr;
        if ( _pManager == nullptr )
            _pManager = make_unique<GameObjectManager>();
        GameObject* pOwner     = _pManager->createGameObject( hashed_string( "EditorDefaultObject" ) );
        Component*  pComponent = pOwner != nullptr ? type._addComponent( pOwner ) : nullptr;
        _listType.push_back( &type );
        _listDefault.push_back( pComponent );
        return pComponent;
    }

    void EditorDefaultObjects::clear()
    {
        _listType.clear();
        _listDefault.clear();
        _pManager.reset();
    }

    bool EditorDefaultObjects::isDefaultValue( const PropertyInfo& prop, const void* pInstance, const void* pDefaultInstance )
    {
        const SerializeContext& context = SerializeContext::getDefault();
        const string            current = SerializerUtil::formatPropertyText( prop, pInstance, context );
        if ( pDefaultInstance != nullptr )
            return current == SerializerUtil::formatPropertyText( prop, pDefaultInstance, context );
        if ( prop._metadata._defaultValue.empty() )
            return true;
        // 메타 글은 사람이 쓴 꼴이라("1.0" 과 "1") 숫자는 값으로 비교한다.
        float64 currentNumber{ 0.0 };
        float64 defaultNumber{ 0.0 };
        if ( StringUtil::parseDouble( string_view{ current }, currentNumber ) && StringUtil::parseDouble( string_view{ prop._metadata._defaultValue }, defaultNumber ) )
        {
            const float64 scale = MathUtil::max( MathUtil::abs( defaultNumber ), 1.0 );
            return MathUtil::abs( currentNumber - defaultNumber ) <= 1e-6 * scale;
        }
        return StringUtil::equals( string_view{ current }, string_view{ prop._metadata._defaultValue }, true );
    }
} // namespace sw::editor
