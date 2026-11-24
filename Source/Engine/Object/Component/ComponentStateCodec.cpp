#include "pch.h"

#include "Engine/Object/Component/ComponentStateCodec.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    namespace
    {
        struct ComponentStateCodecInternal
        {
            /** @brief 코덱 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. */
            static SW_DEFINE_REGISTRATION_LIST( getCodecs, const ComponentStateCodec, "ComponentStateCodec" )

                /** @brief @p pComponent 가 @p codec 의 형식(또는 파생)이면 true 입니다. */
                static bool matchesCodec( const Component* pComponent, const ComponentStateCodec& codec )
            {
                const TypeInfo* const pType = pComponent != nullptr && pComponent->isPendingDestroy() == false ? pComponent->getTypeInfo() : nullptr;
                return pType != nullptr && pType->isDerivedFrom( codec._pGetType() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ComponentStateCodec::add( const ComponentStateCodec* pCodec )
    {
        SW_ASSERT( pCodec == nullptr || ( pCodec->_pTypeName != nullptr && pCodec->_pGetType != nullptr && pCodec->_pWrite != nullptr && pCodec->_pRestore != nullptr ) );
        const RegistrationResult result = ComponentStateCodecInternal::getCodecs().addLogged( pCodec, "Component state codec" );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }

    void ComponentStateCodec::remove( const ComponentStateCodec* pCodec )
    {
        (void)ComponentStateCodecInternal::getCodecs().remove( pCodec );
    }

    void ComponentStateCodec::captureObject( const GameObject& object, vector<uint8>& outBytes )
    {
        outBytes.clear();
        Archive       arch;
        uint32        count = 0;
        vector<uint8> stateBytes;
        for ( Component* pComponent : object.getComponents() )
        {
            if ( pComponent != nullptr && pComponent->isTransient() )
                continue; // 파생 컴포넌트는 상태가 아니다
            vector<string_view> listTakenType;
            for ( const ComponentStateCodec* pCodec : ComponentStateCodecInternal::getCodecs().getItems() )
            {
                const string_view typeName{ pCodec->_pTypeName };
                if ( ComponentStateCodecInternal::matchesCodec( pComponent, *pCodec ) == false || VectorUtil::contains( listTakenType, typeName ) )
                    continue; // 같은 형식의 코덱이 둘이면 먼저 것
                listTakenType.push_back( typeName );
                stateBytes.clear();
                pCodec->_pWrite( *pComponent, stateBytes );
                arch << typeName << string_view( ComponentStableKey::formatKey( ComponentStableKey::getLocalID( pComponent ) ) ) << stateBytes;
                ++count;
            }
        }
        if ( count > 0 )
            outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    void ComponentStateCodec::restoreObject( GameObject& object, const vector<uint8>& bytes )
    {
        Archive arch( bytes.data(), bytes.size() );
        while ( arch.getRemainingBytes() > 0 )
        {
            string        typeName;
            string        stableKey;
            vector<uint8> stateBytes;
            arch >> typeName >> stableKey >> stateBytes;
            if ( arch.isError() )
            {
                SW_LOG_ERROR_ID( "ComponentStateCodec.StateCorrupted", "The saved component state of '%#' is corrupted - the rest of it is not restored", object.getName().c_str() );
                return;
            }
            const ComponentStateCodec* pFound = nullptr;
            for ( const ComponentStateCodec* pCodec : ComponentStateCodecInternal::getCodecs().getItems() )
            {
                if ( pFound == nullptr && typeName == pCodec->_pTypeName )
                    pFound = pCodec;
            }
            Component* pTarget = nullptr;
            for ( Component* pComponent : object.getComponents() )
            {
                if ( pFound != nullptr && pTarget == nullptr && ComponentStateCodecInternal::matchesCodec( pComponent, *pFound ) && ComponentStableKey::matchesKey( pComponent, stableKey ) )
                    pTarget = pComponent;
            }
            if ( pTarget == nullptr )
            {
                SW_LOG_WARNING_ID( "ComponentStateCodec.TargetMissing", "The saved state of component '%#' (%#) on '%#' has no component or registered type to go to - it starts fresh", typeName, stableKey,
                                   object.getName().c_str() );
                continue;
            }
            pFound->_pRestore( *pTarget, std::move( stateBytes ) );
        }
    }
} // namespace sw
