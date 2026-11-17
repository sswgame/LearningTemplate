#include "pch.h"

#include "Engine/Object/Component/ComponentStableKey.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/UUID/UUID.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    namespace
    {
        struct ComponentStableKeyInternal
        {
            static constexpr const utf8* kHexDigits             = "0123456789abcdef";
            static constexpr size_t      kHexLength             = 16;
            static constexpr int32       kHexBase               = 16;
            static constexpr uint32      kNibbleBits            = 4;
            static constexpr uint64      kNibbleMask            = 0xF;
            static constexpr size_t      kLegacyKeyDigitReserve = 12; ///< `#` 와 순번 숫자 자리

            /** @brief 옛 키 글(`이름#n`)입니다. */
            static string makeLegacyText( string_view baseName, int32 occurrence )
            {
                string key;
                key.reserve( baseName.size() + kLegacyKeyDigitReserve );
                key.append( baseName.data(), baseName.size() );
                key += '#';
                key += to_string( occurrence );
                return key;
            }

            /** @brief 오브젝트의 컴포넌트를 목록 칸으로 옮깁니다(오브젝트 순서). @p outIndex 는 @p pTarget 의 자리입니다(없으면 그대로). */
            static vector<ComponentLocalIDSlot> collectSlots( const GameObject& owner, const Component* pTarget, size_t& outIndex )
            {
                // 삭제 예정 컴포넌트도 센다 — 표시만 하고 목록에서 빼기 전(틱 뒤 `removeComponent` 가 굳힌다)에 형제의 id 가 흔들리지 않게.
                vector<ComponentLocalIDSlot> listSlot;
                for ( const Component* pComp : owner.getComponents() )
                {
                    if ( pComp == nullptr )
                        continue;
                    if ( pComp == pTarget )
                        outIndex = listSlot.size();
                    listSlot.push_back( ComponentLocalIDSlot{ string( ComponentStableKey::getBaseName( pComp ) ), pComp->getStoredLocalID(), 0 } );
                }
                ComponentStableKey::resolveLocalIDs( listSlot );
                return listSlot;
            }

            /** @brief 운영체제 난수 64 비트(0 이 아닌 값)입니다. */
            static uint64 makeRandomID()
            {
                const UUID uuid = UUID::generate();
                uint64     id   = 0;
                Memory::copy( &id, uuid._arrBytes, sizeof( id ) );
                return id != 0 ? id : 1;
            }
        };
    } // namespace

    string_view ComponentStableKey::getBaseName( const Component* pComp )
    {
        if ( pComp == nullptr )
            return "Component";

        // 이름이 먼저다 — 상태와 함께 저장된다(언리얼의 컴포넌트 이름 자리). 이름이 없는 상태는 타입 이름으로 읽혀 키가 그대로다.
        // 이름의 글은 intern 된 저장소에 살아 뷰를 돌려줘도 된다.
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

    void ComponentStableKey::resolveLocalIDs( vector<ComponentLocalIDSlot>& inoutListSlot )
    {
        unordered_set<uint64> setTaken;
        for ( const ComponentLocalIDSlot& slot : inoutListSlot )
        {
            if ( slot._storedID != 0 )
                setTaken.insert( slot._storedID );
        }
        unordered_map<string, int32> mapOccurrence;
        for ( ComponentLocalIDSlot& slot : inoutListSlot )
        {
            int32& occurrence = mapOccurrence[slot._baseName]; // 옛 키처럼 같은 이름을 모두 센다(적힌 id 가 있어도)
            if ( slot._storedID != 0 )
            {
                slot._resolved = slot._storedID;
                ++occurrence;
                continue;
            }
            const string legacy = ComponentStableKeyInternal::makeLegacyText( slot._baseName, occurrence );
            ++occurrence;
            uint64 id = HashUtil::computeStringHash64( legacy.data(), legacy.size(), false );
            while ( id == 0 || setTaken.contains( id ) )
            {
                id = HashUtil::computeStringHash64( legacy.data(), legacy.size(), false, id ); // 결정적으로 다시 섞는다 — 같은 목록은 같은 답
            }
            setTaken.insert( id );
            slot._resolved = id;
        }
    }

    uint64 ComponentStableKey::getLocalID( const Component* pComp )
    {
        if ( pComp == nullptr || pComp->getOwner() == nullptr )
            return 0;
        if ( pComp->getStoredLocalID() != 0 )
            return pComp->getStoredLocalID();
        size_t                             index    = 0;
        const vector<ComponentLocalIDSlot> listSlot = ComponentStableKeyInternal::collectSlots( *pComp->getOwner(), pComp, index );
        return index < listSlot.size() ? listSlot[index]._resolved : 0;
    }

    void ComponentStableKey::freezeLocalIDs( GameObject& owner )
    {
        size_t                             unused   = 0;
        const vector<ComponentLocalIDSlot> listSlot = ComponentStableKeyInternal::collectSlots( owner, nullptr, unused );
        size_t                             index    = 0;
        for ( Component* pComp : owner.getComponents() )
        {
            if ( pComp == nullptr )
                continue;
            if ( pComp->_localID == 0 )
                pComp->_localID = listSlot[index]._resolved;
            ++index;
        }
    }

    void ComponentStableKey::assignFreshLocalID( Component* pComp )
    {
        if ( pComp == nullptr || pComp->getOwner() == nullptr )
            return;
        // 형제를 굳히지 않는다 — 로드 중(모르는 타입 보관 `MissingComponent`)에도 불리고, 끝에 붙은 컴포넌트는 앞 형제의 옛 키 순번을 바꾸지 않는다.
        const GameObject&     owner = *pComp->getOwner();
        unordered_set<uint64> setTaken;
        for ( const Component* pOther : owner.getComponents() )
        {
            if ( pOther != nullptr && pOther != pComp )
                setTaken.insert( pOther->getStoredLocalID() );
        }
        uint64 id = ComponentStableKeyInternal::makeRandomID();
        while ( setTaken.contains( id ) )
        {
            id = ComponentStableKeyInternal::makeRandomID();
        }
        pComp->_localID = id;
    }

    void ComponentStableKey::restoreLocalID( Component* pComp, uint64 localID )
    {
        if ( pComp != nullptr )
            pComp->_localID = localID;
    }

    string ComponentStableKey::formatKey( uint64 localID )
    {
        string key( ComponentStableKeyInternal::kHexLength + 1, '0' );
        key[0] = kLocalIDPrefix;
        for ( size_t digit = 0; digit < ComponentStableKeyInternal::kHexLength; ++digit )
        {
            key[ComponentStableKeyInternal::kHexLength - digit] = ComponentStableKeyInternal::kHexDigits[( localID >> ( digit * ComponentStableKeyInternal::kNibbleBits ) ) & ComponentStableKeyInternal::kNibbleMask];
        }
        return key;
    }

    optional<uint64> ComponentStableKey::parseKey( string_view key )
    {
        if ( key.size() != ComponentStableKeyInternal::kHexLength + 1 || key[0] != kLocalIDPrefix )
            return nullopt;
        uint64 localID = 0;
        if ( StringUtil::parseUint64( key.substr( 1 ), localID, ComponentStableKeyInternal::kHexBase ) == false || localID == 0 )
            return nullopt;
        return localID;
    }

    string ComponentStableKey::makeKey( const Component* pComp )
    {
        const uint64 localID = getLocalID( pComp );
        return localID != 0 ? formatKey( localID ) : string{};
    }

    string ComponentStableKey::makeLegacyKey( const Component* pComp )
    {
        if ( pComp == nullptr || pComp->getOwner() == nullptr )
            return {};

        const string_view targetBase = getBaseName( pComp );
        int32             occurrence = 0;
        bool              bFound     = false;
        pComp->getOwner()->forEachComponent( [&]( const Component* pOther )
        {
            const bool bSkipOtherComponent = bFound || pOther == nullptr || getBaseName( pOther ) != targetBase;
            if ( bSkipOtherComponent )
                return;
            if ( pOther == pComp )
                bFound = true;
            else
                ++occurrence;
        } );
        if ( bFound == false )
            return {};
        return ComponentStableKeyInternal::makeLegacyText( targetBase, occurrence );
    }

    bool ComponentStableKey::matchesKey( const Component* pComp, string_view key )
    {
        if ( pComp == nullptr || key.empty() )
            return false;
        const optional<uint64> localID = parseKey( key );
        if ( localID.has_value() )
            return getLocalID( pComp ) == *localID;
        return makeLegacyKey( pComp ) == key; // 옮기는 동안의 옛 키(`이름#n`)
    }

    Component* ComponentStableKey::findComponent( GameObject* pOwner, string_view key )
    {
        if ( pOwner == nullptr || key.empty() )
            return nullptr;
        const optional<uint64> localID = parseKey( key );
        if ( localID.has_value() )
        {
            size_t                             unused   = 0;
            const vector<ComponentLocalIDSlot> listSlot = ComponentStableKeyInternal::collectSlots( *pOwner, nullptr, unused );
            size_t                             index    = 0;
            for ( Component* pComp : pOwner->getComponents() )
            {
                if ( pComp == nullptr )
                    continue;
                if ( listSlot[index]._resolved == *localID && pComp->isPendingDestroy() == false )
                    return pComp;
                ++index;
            }
            return nullptr;
        }

        // 옮기는 동안의 옛 키(`이름#n`) — 이름과 같은 이름 가운데 순번.
        const size_t hashPos = key.rfind( '#' );
        if ( hashPos == string_view::npos || hashPos == 0 || hashPos + 1 == key.size() )
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
            const bool bSkipComponent = pFound != nullptr || pComp == nullptr || getBaseName( pComp ) != requestedBase;
            if ( bSkipComponent )
                return;
            if ( occurrence == requestedOccurrence )
                pFound = pComp;
            else
                ++occurrence;
        } );
        return pFound;
    }
} // namespace sw
