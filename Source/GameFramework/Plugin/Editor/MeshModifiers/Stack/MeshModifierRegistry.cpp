/**
 * @file MeshModifierRegistry.cpp
 * @brief 메시 모디파이어 등록 목록을 구현합니다. 목록은 `RegistrationList`(id = 이름, (카테고리 순서 없음) id 사전순)입니다.
 */
#include "pch.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 지금 올라 있는 모디파이어 등록의 목록을 듭니다. 등록자는 이 서브시스템이 선 뒤에 붙는다(`engine::registerModuleTypes`). */
        class MeshModifierRegistrations final : public ISubsystem
        {
        public:
            RegistrationList<MeshModifierRegistration> _listRegistration;

            static constexpr const utf8* kSubsystemName = "MeshModifierRegistrations";

            MeshModifierRegistrations()
                : _listRegistration{ "MeshModifierRegistry", RegistrationOrder::ByOrderThenName, true }
            {
            }

            SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
            void                shutdownSubsystem() override {}
            const utf8*         getSubsystemName() const override { return kSubsystemName; }
        };

        struct MeshModifierRegistryInternal
        {
            /** @brief 등록 목록입니다(플러그인의 모듈 서브시스템이 듭니다). 등록자는 넘겨받은 범위로 직접 찾는다 — 모듈을 올리는 때 게임 창구는 아직 묶이지 않았다. */
            static RegistrationList<MeshModifierRegistration>& getList() { return game::getSubsystem<MeshModifierRegistrations>()._listRegistration; }

            static bool isBefore( const MeshModifierRegistration* pLeft, const MeshModifierRegistration* pRight )
            {
                const int32 categoryOrder = string_view( pLeft->_pCategory ).compare( string_view( pRight->_pCategory ) );
                if ( categoryOrder != 0 )
                    return categoryOrder < 0;
                return string_view( pLeft->_pDisplayName ).compare( string_view( pRight->_pDisplayName ) ) < 0;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_MODULE_SUBSYSTEM( Engine, MeshModifierRegistrations, MeshModifierRegistrations );
} // namespace sw::editor

namespace sw::editor
{
    MeshModifierRegistration::MeshModifierRegistration( const utf8* pID, const utf8* pDisplayName, const utf8* pCategory, MeshModifierCreateFn pCreateFn )
        : _pID{ pID }
        , _pDisplayName{ pDisplayName }
        , _pCategory{ pCategory }
        , _pCreateFn{ pCreateFn }
    {
    }

    void MeshModifierRegistration::attach( const ISubsystemLocator& scope )
    {
        (void)requireSubsystem<MeshModifierRegistrations>( scope )._listRegistration.addLogged( this, "Mesh modifier id", _pID );
    }

    void MeshModifierRegistration::detach( const ISubsystemLocator& scope )
    {
        MeshModifierRegistrations* const pRegistrations = locateSubsystem<MeshModifierRegistrations>( &scope ); // 종료 때는 표가 먼저 내려갔을 수 있다
        if ( pRegistrations != nullptr )
            (void)pRegistrations->_listRegistration.remove( this ); // 거절된 중복 등록은 목록에 없다
    }
} // namespace sw::editor

namespace sw::editor
{
    void MeshModifierRegistry::getRegistrations( vector<const MeshModifierRegistration*>& outListRegistration )
    {
        outListRegistration.clear();
        for ( const MeshModifierRegistration* pRegistration : MeshModifierRegistryInternal::getList().getItems() )
        {
            // 카테고리 · 표시 이름 순으로 끼운다(등록 수가 수십 개라 삽입 정렬로 충분하다)
            size_t insertIndex = outListRegistration.size();
            while ( insertIndex > 0 && MeshModifierRegistryInternal::isBefore( pRegistration, outListRegistration[insertIndex - 1] ) )
            {
                --insertIndex;
            }
            outListRegistration.insert( outListRegistration.begin() + static_cast<ptrdiff_t>( insertIndex ), pRegistration );
        }
    }

    const MeshModifierRegistration* MeshModifierRegistry::findRegistration( string_view id )
    {
        return MeshModifierRegistryInternal::getList().findByName( id );
    }

    unique_ptr<MeshModifier> MeshModifierRegistry::create( string_view id )
    {
        const MeshModifierRegistration* pRegistration = findRegistration( id );
        unique_ptr<MeshModifier>        modifier;
        if ( pRegistration != nullptr && pRegistration->_pCreateFn != nullptr )
            modifier = pRegistration->_pCreateFn();
        if ( modifier != nullptr )
        {
            modifier->setTypeID( pRegistration->_pID );
            modifier->setName( pRegistration->_pDisplayName );
        }
        return modifier;
    }
} // namespace sw::editor
