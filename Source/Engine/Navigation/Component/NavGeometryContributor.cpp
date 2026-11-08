#include "pch.h"

#include "Engine/Navigation/Component/NavGeometryContributor.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. 정적 등록부는 모두 `RegistrationList` 하나로 쓴다. */
        SW_DEFINE_REGISTRATION_LIST( getContributions, const NavGeometryContribution, "NavGeometryContributorRegistry" )
    } // namespace
} // namespace sw

namespace sw
{
    bool NavGeometryContributorRegistry::add( const NavGeometryContribution* pContribution )
    {
        SW_ASSERT( pContribution == nullptr || ( pContribution->_pGetType != nullptr && pContribution->_pContribute != nullptr ) ); // 등록 매크로가 늘 채운다
        const RegistrationResult result = getContributions().addLogged( pContribution, "Nav geometry contributor" );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent; // 같은 줄을 다시 올리면 `AlreadyPresent` 라 할 일이 없다
    }

    void NavGeometryContributorRegistry::remove( const NavGeometryContribution* pContribution )
    {
        (void)getContributions().remove( pContribution ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    const vector<const NavGeometryContribution*>& NavGeometryContributorRegistry::getAll()
    {
        return getContributions().getItems();
    }
} // namespace sw
