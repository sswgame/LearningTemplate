/**
 * @file NavMeshModifierComponent.h
 * @brief 베이크에서 이 오브젝트(와 자식)의 기하를 빼거나 영역을 바꿉니다 — 유니티 `NavMeshModifier` 의 자리입니다.
 * @details 가장 가까운 조상의 수정자가 이깁니다. 영역 이름은 내비게이션 표(`NavMeshSettings::_listArea`)의 이름이고, 모르는 이름은 베이크 때 오류를 남기고
 *          보통 땅으로 베이크합니다. 바꾸면(속성 · 켜기 · 끄기) 그 오브젝트 자리의 타일을 다시 베이크합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @class NavMeshModifierComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Navigation", DisplayName = "NavMesh Modifier", Tooltip = "Leaves this object out of the navmesh bake or gives its geometry an area" )
    class SW_API NavMeshModifierComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavMeshModifierComponent();
        ~NavMeshModifierComponent() override = default;

        void onPropertyChanged( hashed_string propertyName ) override;

        bool                 isIgnoredFromBuild() const { return _bIgnoreFromBuild; }
        void                 setIgnoreFromBuild( bool bIgnore ) { _bIgnoreFromBuild = bIgnore; }
        const hashed_string& getAreaName() const { return _areaName; }
        void                 setAreaName( const hashed_string& areaName ) { _areaName = areaName; }

    private:
        PROPERTY( Category = "Modifier", DisplayName = "Area", Tooltip = "Navigation area of this object's geometry (empty = keep)" )
        hashed_string _areaName;
        PROPERTY( Category = "Modifier", DisplayName = "Ignore From Build", Tooltip = "Leave this object and its children out of the bake" )
        bool _bIgnoreFromBuild;
    };
} // namespace sw
