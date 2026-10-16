#include "pch.h"

#include "Engine/Object/Component/Navigation/NavGeometryContributor.h"

namespace sw
{
    namespace
    {
        /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. */
        vector<const NavGeometryContribution*>& getContributions()
        {
            static vector<const NavGeometryContribution*> s_listContribution;
            return s_listContribution;
        }
    } // namespace
} // namespace sw

namespace sw
{
    void NavGeometryContributorRegistry::add( const NavGeometryContribution* pContribution )
    {
        const bool bInvalidContribution = pContribution == nullptr || pContribution->_pGetType == nullptr || pContribution->_pContribute == nullptr;
        if ( bInvalidContribution )
            return;
        vector<const NavGeometryContribution*>& listContribution = getContributions();
        for ( const NavGeometryContribution* pExisting : listContribution )
        {
            if ( pExisting == pContribution )
                return;
        }
        listContribution.push_back( pContribution );
    }

    const vector<const NavGeometryContribution*>& NavGeometryContributorRegistry::getAll()
    {
        return getContributions();
    }
} // namespace sw

namespace sw
{
    NavGeometryContributorRegistrar::NavGeometryContributorRegistrar( const NavGeometryContribution* pContribution )
    {
        NavGeometryContributorRegistry::add( pContribution );
    }
} // namespace sw
