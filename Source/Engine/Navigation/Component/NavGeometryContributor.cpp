#include "pch.h"

#include "Engine/Navigation/Component/NavGeometryContributor.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. 정적 등록부는 모두 `RegistrationList` 하나로 쓴다. */
        RegistrationList<const NavGeometryContribution>& getContributions()
        {
            static RegistrationList<const NavGeometryContribution> s_listContribution{ "NavGeometryContributorRegistry" };
            return s_listContribution;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void NavGeometryContributorRegistry::add( const NavGeometryContribution* pContribution )
    {
        SW_ASSERT( pContribution == nullptr || ( pContribution->_pGetType != nullptr && pContribution->_pContribute != nullptr ) ); // 등록 매크로가 늘 채운다
        (void)getContributions().addLogged( pContribution, "Nav geometry contributor" );                                            // 같은 줄을 다시 올리면 `AlreadyPresent` 라 할 일이 없다
    }

    const vector<const NavGeometryContribution*>& NavGeometryContributorRegistry::getAll()
    {
        return getContributions().getItems();
    }
} // namespace sw

namespace sw
{
    NavGeometryContributorRegistrar::NavGeometryContributorRegistrar( const NavGeometryContribution* pContribution )
    {
        NavGeometryContributorRegistry::add( pContribution );
    }
} // namespace sw
