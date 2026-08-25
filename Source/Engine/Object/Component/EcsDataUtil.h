/**
 * @file EcsDataUtil.h
 * @brief ECS Data 구조체를 엔티티에 붙이고 Defaults를 주입합니다.
 */
#pragma once
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
	/**
	 * @brief 엔티티에 TData가 없으면 추가하고 gamedata.xml Defaults를 주입합니다.
	 * @param pAliasTypeInfo 파사드 Component TypeInfo. XML 키 별칭 (HPBarBaseData ← HPBar).
	 */
	template <typename TData>
	TData* ensureEcsData( GameObject* pGameObject, const TypeInfo* pAliasTypeInfo = nullptr )
	{
		if ( pGameObject == nullptr )
			return nullptr;
		TData* pExisting = pGameObject->template getComponent<TData>().get();
		if ( pExisting != nullptr )
			return pExisting;
		TData* pData = pGameObject->template addComponent<TData>();
		if ( pData != nullptr )
		{
			if constexpr ( HasStaticType_v<TData> )
			{
				const TypeInfo* pTypeInfo = TData::StaticType();
				if ( pTypeInfo != nullptr )
					ComponentDefaults::applyDefaults( pData, *pTypeInfo, pAliasTypeInfo );
			}
		}
		return pData;
	}
} // namespace sw
