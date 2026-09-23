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
        if ( pComp->getComponentName().empty() == false )
            return pComp->getComponentName().c_str();

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
