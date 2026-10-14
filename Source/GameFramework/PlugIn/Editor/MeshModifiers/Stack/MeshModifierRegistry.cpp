/**
 * @file MeshModifierRegistry.cpp
 * @brief 메시 모디파이어 등록 목록을 구현합니다. 목록은 `RegistrationList`(id = 이름, (카테고리 순서 없음) id 사전순)입니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw::editor
{
    namespace
    {
        struct MeshModifierRegistryInternal
        {
            /** @brief 등록 목록입니다. 함수 안 정적 객체라 처음 등록할 때 만들어지고, 모든 등록 객체보다 늦게 소멸합니다. */
            static RegistrationList<MeshModifierRegistration>& getList()
            {
                static RegistrationList<MeshModifierRegistration> s_list{ RegistrationOrder::ByOrderThenName, true };
                return s_list;
            }

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
    MeshModifierRegistration::MeshModifierRegistration( const utf8* pID, const utf8* pDisplayName, const utf8* pCategory, MeshModifierCreateFn pCreateFn )
        : _pID{ pID }
        , _pDisplayName{ pDisplayName }
        , _pCategory{ pCategory }
        , _pCreateFn{ pCreateFn }
    {
        MeshModifierRegistry::add( this );
    }

    MeshModifierRegistration::~MeshModifierRegistration()
    {
        MeshModifierRegistry::remove( this );
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
        if ( pRegistration == nullptr || pRegistration->_pCreateFn == nullptr )
            return nullptr;
        unique_ptr<MeshModifier> modifier = pRegistration->_pCreateFn();
        if ( modifier == nullptr )
            return nullptr;
        modifier->setTypeID( pRegistration->_pID );
        modifier->setName( pRegistration->_pDisplayName );
        return modifier;
    }

    void MeshModifierRegistry::add( MeshModifierRegistration* pRegistration )
    {
        const RegistrationResult result = MeshModifierRegistryInternal::getList().add( pRegistration, pRegistration->_pID );
        if ( result == RegistrationResult::DuplicateName )
            SW_LOG_WARNING( "Mesh modifier id '%#' is registered twice - the first registration is kept", pRegistration->_pID );
    }

    void MeshModifierRegistry::remove( const MeshModifierRegistration* pRegistration )
    {
        (void)MeshModifierRegistryInternal::getList().remove( pRegistration ); // 거절된 중복 등록은 목록에 없다
    }
} // namespace sw::editor
