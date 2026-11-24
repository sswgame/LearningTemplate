#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    namespace
    {
        struct StatefulComponentRegistrarInternal
        {
            /** @brief 정적 등록 목록입니다. 함수 안 정적 객체라 처음 등록할 때 만들어지고, 모든 등록 객체보다 늦게 소멸합니다. */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrationList, StatefulComponentRegistration, "StatefulComponentRegistry", RegistrationOrder::Insertion, true )
        };
    } // namespace

    StatefulComponentRegistration::StatefulComponentRegistration( const utf8* pTypeName, DespawnFn pDespawn, const ComponentStateCodec& codec, SourceLocation site )
        : _pTypeName{ pTypeName }
        , _pDespawn{ pDespawn }
        , _pCodec{ codec._pWrite != nullptr ? make_unique<ComponentStateCodec>( codec ) : nullptr }
        , _registrar{ this, &StatefulComponentRegistration::add, &StatefulComponentRegistration::remove, site }
    {
    }

    StatefulComponentRegistration::~StatefulComponentRegistration() = default;

    bool StatefulComponentRegistration::add( StatefulComponentRegistration* pRegistration )
    {
        if ( StatefulComponentRegistrarInternal::getRegistrationList().addLogged( pRegistration, "Stateful component type", string_view( pRegistration->_pTypeName ) ) != RegistrationResult::Added )
            return false;
        return pRegistration->_pCodec == nullptr || ComponentStateCodec::add( pRegistration->_pCodec.get() );
    }

    void StatefulComponentRegistration::remove( StatefulComponentRegistration* pRegistration )
    {
        if ( pRegistration->_pCodec != nullptr )
            ComponentStateCodec::remove( pRegistration->_pCodec.get() );
        (void)StatefulComponentRegistrarInternal::getRegistrationList().remove( pRegistration ); // 올린 등록만 불린다(거절된 것은 StaticRegistrar 가 내리지 않는다)
    }

    const vector<StatefulComponentRegistration*>& StatefulComponentRegistration::getRegistrations()
    {
        return StatefulComponentRegistrarInternal::getRegistrationList().getItems();
    }
} // namespace sw
