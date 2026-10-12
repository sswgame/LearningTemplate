#include "pch.h"

#include "Engine/Object/Component/Navigation/NavGeometryContributor.h"

namespace sw
{
    namespace
    {
        struct NavGeometryContributorInternal
        {
            /** @brief 등록 줄 목록. 함수 정적이라 다른 .cpp 의 정적 초기화가 먼저 와도 비어 있는 채로 만들어진다. */
            static vector<const NavGeometryContribution*>& getContributions()
            {
                static vector<const NavGeometryContribution*> s_listContribution;
                return s_listContribution;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void NavGeometryContributorRegistry::add( const NavGeometryContribution* pContribution )
    {
        if ( pContribution == nullptr || pContribution->_pGetType == nullptr || pContribution->_pContribute == nullptr )
            return;
        vector<const NavGeometryContribution*>& listContribution = NavGeometryContributorInternal::getContributions();
        for ( const NavGeometryContribution* pExisting : listContribution )
        {
            if ( pExisting == pContribution )
                return;
        }
        listContribution.push_back( pContribution );
    }

    const vector<const NavGeometryContribution*>& NavGeometryContributorRegistry::getAll()
    {
        return NavGeometryContributorInternal::getContributions();
    }
} // namespace sw

namespace sw
{
    NavGeometryContributorRegistrar::NavGeometryContributorRegistrar( const NavGeometryContribution* pContribution )
    {
        NavGeometryContributorRegistry::add( pContribution );
    }
} // namespace sw
