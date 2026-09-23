#include "pch.h"

#include "Engine/Object/GameObject/LightRegistry.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"

namespace sw
{
    void LightRegistry::addDirectional( DirectionalLightComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        for ( const DirectionalLightComponent* pExisting : _listDirectional )
        {
            if ( pExisting == pComp )
                return;
        }
        _listDirectional.push_back( pComp );
    }

    void LightRegistry::removeDirectional( DirectionalLightComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        for ( size_t slot = 0; slot < _listDirectional.size(); ++slot )
        {
            if ( _listDirectional[slot] != pComp )
                continue;

            // swap-and-pop. 순서는 의미가 없다. 부르는 쪽이 "활성인 첫 빛"을 고르고, 빛이 둘 이상일
            // 때 어느 쪽이 뽑히는지는 예전(오브젝트 순회 순서)에도 정해져 있지 않았다.
            _listDirectional[slot] = _listDirectional.back();
            _listDirectional.pop_back();
            return;
        }
    }

    void LightRegistry::addPoint( PointLightComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        for ( const PointLightComponent* pExisting : _listPoint )
        {
            if ( pExisting == pComp )
                return;
        }
        _listPoint.push_back( pComp );
    }

    void LightRegistry::removePoint( PointLightComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        for ( size_t slot = 0; slot < _listPoint.size(); ++slot )
        {
            if ( _listPoint[slot] != pComp )
                continue;

            // swap-and-pop. 순서는 의미가 없다. 셰이더가 라이트 목록을 통째로 도는 구조라
            // 어느 자리에 들어가든 결과가 같다.
            _listPoint[slot] = _listPoint.back();
            _listPoint.pop_back();
            return;
        }
    }

    void LightRegistry::addSpot( SpotLightComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        for ( const SpotLightComponent* pExisting : _listSpot )
        {
            if ( pExisting == pComp )
                return;
        }
        _listSpot.push_back( pComp );
    }

    void LightRegistry::removeSpot( SpotLightComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        for ( size_t slot = 0; slot < _listSpot.size(); ++slot )
        {
            if ( _listSpot[slot] != pComp )
                continue;

            _listSpot[slot] = _listSpot.back();
            _listSpot.pop_back();
            return;
        }
    }
} // namespace sw
