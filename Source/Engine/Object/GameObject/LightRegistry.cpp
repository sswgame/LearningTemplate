/**
 * @file LightRegistry.cpp
 * @brief 빛 등록부 구현입니다(종류별 칸에 등록 · 해제).
 */
#include "pch.h"

#include "Engine/Object/GameObject/LightRegistry.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Object/Component/3D/LightComponent.h"

namespace sw
{
    namespace
    {
        struct LightRegistryInternal
        {
            /** @brief 종류 번호가 칸 범위 안이면 true 입니다. */
            static bool isValidType( uint32 lightType ) { return lightType < shaderslot::kLightTypeCount; }

            /** @brief 범위 밖 종류를 물으면 돌려주는 빈 목록입니다. */
            static const vector<LightComponent*>& getEmptyList()
            {
                static const vector<LightComponent*> s_listEmpty;
                return s_listEmpty;
            }
        };
    } // namespace

    void LightRegistry::add( LightComponent* pLight )
    {
        if ( pLight == nullptr || LightRegistryInternal::isValidType( pLight->getLightType() ) == false )
            return;

        std::scoped_lock<mutex>  lock{ _mutex };
        vector<LightComponent*>& listLight = _arrListLight[pLight->getLightType()];
        for ( const LightComponent* pExisting : listLight )
        {
            if ( pExisting == pLight )
                return;
        }
        listLight.push_back( pLight );
    }

    void LightRegistry::remove( LightComponent* pLight )
    {
        if ( pLight == nullptr || LightRegistryInternal::isValidType( pLight->getLightType() ) == false )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        // swap-and-pop. 부르는 쪽이 "활성인 첫 빛"을 고르고, 빛이 둘 이상일 때 어느 쪽이 뽑히는지는 예전(오브젝트 순회 순서)에도
        // 정해져 있지 않았다.
        (void)VectorUtil::removeSingleSwap( _arrListLight[pLight->getLightType()], pLight ); // 두 번 빼도 된다 — 없으면 할 일이 없다
    }

    const vector<LightComponent*>& LightRegistry::getAll( uint32 lightType ) const
    {
        if ( LightRegistryInternal::isValidType( lightType ) == false )
            return LightRegistryInternal::getEmptyList();
        return _arrListLight[lightType];
    }
} // namespace sw
