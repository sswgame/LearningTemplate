#include "pch.h"

#include "Engine/Object/GameObject/LightRegistry.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"

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

            // swap-and-pop. 순서는 의미가 없다 — 부르는 쪽이 "활성인 첫 빛"을 고르고, 빛이 둘 이상일
            // 때 어느 쪽이 뽑히는지는 예전(오브젝트 순회 순서)에도 정해져 있지 않았다.
            _listDirectional[slot] = _listDirectional.back();
            _listDirectional.pop_back();
            return;
        }
    }
} // namespace sw
