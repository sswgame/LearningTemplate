/**
 * @file TagComponent.h
 * @brief GameObject에 태그 집합을 붙이는 컴포넌트
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    namespace generated
    {
        struct sw_TagComponent_Registrar;
    } // namespace generated
    /**
     * @brief GameObject의 태그를 담는 컴포넌트입니다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Tag Component", Tooltip = "GameObject Tag Container Component" )
    class SW_API TagComponent : public Component
    {
        friend struct ::sw::generated::sw_TagComponent_Registrar;

    public:
        REFLECT_BODY();
        TagComponent();
        virtual ~TagComponent() override = default;

        TagComponent( const TagComponent& )            = delete;
        TagComponent& operator=( const TagComponent& ) = delete;

        TagComponent( TagComponent&& other ) noexcept            = default;
        TagComponent& operator=( TagComponent&& other ) noexcept = default;

        void onBeginPlay() override;

        TagContainer&       getTags();
        const TagContainer& getTags() const;

        void addTag( TagID tag );
        void removeTag( TagID tag );
        void clearTags();
        bool hasTag( TagID tag, bool bExactMatch = false ) const;
        bool matchTags( const TagContainer& required, const TagContainer& forbidden ) const;
        bool matchesQuery( const TagQuery& query ) const;

    private:
        PROPERTY()
        TagContainer _tags;
    };
} // namespace sw
