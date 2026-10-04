/**
 * @file LightRegistry.cpp
 * @brief 빛 등록부 구현입니다(종류별 칸에 등록 · 해제).
 */
#include "pch.h"

#include "Engine/Object/GameObject/LightRegistry.h"

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

        std::scoped_lock<mutex> lock{ _mutex };
        // 이미 등록된 빛은 목록이 거절한다(멱등).
        (void)_arrRegisteredLight[pLight->getLightType()].add( pLight );
    }

    void LightRegistry::remove( LightComponent* pLight )
    {
        if ( pLight == nullptr || LightRegistryInternal::isValidType( pLight->getLightType() ) == false )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        // 순서를 지키며 뺀다. 부르는 쪽은 "활성인 첫 빛"(그림자를 드리우는 방향광)을 고르므로, 빛 하나를 빼도 남은 빛 사이의 앞뒤가
        // 바뀌지 않아야 그 선택이 등록 순서로 정해진다.
        (void)_arrRegisteredLight[pLight->getLightType()].remove( pLight ); // 두 번 빼도 된다 — 없으면 할 일이 없다
    }

    void LightRegistry::addShadowCaster( ShadowCaster2DComponent* pCaster )
    {
        if ( pCaster == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_registeredShadowCaster.add( pCaster ); // 이미 있으면 목록이 거절한다
    }

    void LightRegistry::removeShadowCaster( ShadowCaster2DComponent* pCaster )
    {
        if ( pCaster == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_registeredShadowCaster.remove( pCaster ); // 없으면 할 일이 없다
    }

    const vector<LightComponent*>& LightRegistry::getAll( uint32 lightType ) const
    {
        if ( LightRegistryInternal::isValidType( lightType ) == false )
            return LightRegistryInternal::getEmptyList();
        return _arrRegisteredLight[lightType].getItems();
    }
} // namespace sw
