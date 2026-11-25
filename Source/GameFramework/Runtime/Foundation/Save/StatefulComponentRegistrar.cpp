#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"

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

    namespace
    {
        /** @brief 식생의 벤 자리를 모든 게임의 상태 스냅샷(세이브 · 핫 리로드, 스트리밍 셀에 든 식생은 셀의 떠 둔 상태)에 싣습니다 — 식생은 엔진 형식이라 GF 에 주인 파일이 없어 등록을 여기 둔다. */
        const StatefulComponentRegistrar<FoliageComponent> s_foliageStateRegistration{ "FoliageComponent" };
    } // namespace
} // namespace sw
