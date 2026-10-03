#include "pch.h"

#include "Engine/Object/Component/ComponentStableKey.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    string_view ComponentStableKey::getBaseName( const Component* pComp )
    {
        if ( pComp == nullptr )
            return "Component";

        // 이름표가 먼저다 — 상태와 함께 저장된다(언리얼의 컴포넌트 이름 자리). 이름표가 없는 상태는 타입 이름으로 읽혀 키가 그대로다.
        // 이름표의 글은 intern 된 저장소에 살아 뷰를 돌려줘도 된다.
        const hashed_string componentName = pComp->getComponentName();
        if ( componentName.empty() == false )
            return componentName.view();
        const TypeInfo* pTypeInfo = pComp->getTypeInfo();
        if ( pTypeInfo != nullptr )
        {
            if ( pTypeInfo->_name.empty() == false )
                return pTypeInfo->_name.c_str();
            if ( pTypeInfo->_fullyQualifiedName.empty() == false )
                return pTypeInfo->_fullyQualifiedName.c_str();
        }
        return "Component";
    }

    string ComponentStableKey::makeKey( const Component* pComp )
    {
        if ( pComp == nullptr || pComp->getOwner() == nullptr )
            return {};

        const string_view targetBase = getBaseName( pComp );
        int32             occurrence = 0;
        bool              bFound     = false;
        pComp->getOwner()->forEachComponent( [&]( const Component* pOther )
        {
            if ( bFound || pOther == nullptr || getBaseName( pOther ) != targetBase )
                return;
            if ( pOther == pComp )
                bFound = true;
            else
                ++occurrence;
        } );
        if ( bFound == false )
            return {};

        string key;
        key.reserve( targetBase.size() + 12 );
        key.append( targetBase.data(), targetBase.size() );
        key += '#';
        key += to_string( occurrence );
        return key;
    }

    Component* ComponentStableKey::findComponent( GameObject* pOwner, string_view key )
    {
        if ( pOwner == nullptr || key.empty() )
            return nullptr;

        const size_t hashPos = key.rfind( '#' );
        if ( hashPos == string_view::npos || hashPos + 1 == key.size() )
            return nullptr;

        const string_view requestedBase       = key.substr( 0, hashPos );
        int32             requestedOccurrence = 0;
        for ( size_t index = hashPos + 1; index < key.size(); ++index )
        {
            if ( key[index] < '0' || key[index] > '9' )
                return nullptr;
            requestedOccurrence = requestedOccurrence * 10 + ( key[index] - '0' );
        }

        int32      occurrence = 0;
        Component* pFound     = nullptr;
        pOwner->forEachComponent( [&]( Component* pComp )
        {
            if ( pFound != nullptr || pComp == nullptr || getBaseName( pComp ) != requestedBase )
                return;
            if ( occurrence == requestedOccurrence )
                pFound = pComp;
            else
                ++occurrence;
        } );
        return pFound;
    }
} // namespace sw
