/**
 * @file TagComponent.h
 * @brief GameObject 에 태그 집합을 붙이는 컴포넌트입니다.
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class TagIndex;

    /**
     * @brief GameObject 의 태그를 담는 컴포넌트입니다.
     */
    REFLECT( Category = "Gameplay", DisplayName = "Tag Component", Tooltip = "GameObject Tag Container Component", Meta = "Icon=Tag" )
    class SW_API TagComponent : public Component
    {
    public:
        REFLECT_BODY();
        TagComponent();
        virtual ~TagComponent() override = default;

        TagComponent( const TagComponent& )            = delete;
        TagComponent& operator=( const TagComponent& ) = delete;

        /** @brief 태그 컨테이너입니다. 읽기 전용 — 쓰기는 아래 셋을 지난다(틱 중이면 미룬다). */
        const TagContainer& getTags() const;

        /**
         * @brief 태그를 더합니다. 컴포넌트 틱 중이면 오브젝트의 미룸 길(`GameObject::addTag`)로 틱 뒤에 적용합니다.
         * @details 주의: 틱 안에서 살아 있는 컨테이너에 바로 쓰면 다른 워커의 `hasTag` · 태그 질의와 같은 컨테이너를 동시에 만진다.
         */
        void addTag( TagID tag );
        /** @brief 태그를 뺍니다. 틱 중이면 `addTag` 처럼 미룹니다. */
        void removeTag( TagID tag );
        /** @brief 태그를 모두 지웁니다. 틱 중이면 `addTag` 처럼 미룹니다. */
        void clearTags();
        bool hasTag( TagID tag, bool bExactMatch = false ) const;
        bool matchesTags( const TagContainer& required, const TagContainer& forbidden ) const;
        bool matchesQuery( const TagQuery& query ) const;

        /** @brief 매니저의 태그 색인(`TagIndex`)을 받아 두고 지금 태그로 칸을 맞춥니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 태그 색인에서 칸을 뺍니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 인스펙터 · 직렬화기가 `_tags` 를 직접 썼으면 색인을 맞춥니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 상태 읽기가 `_tags` 를 채웠으면 색인을 맞춥니다. */
        void onPostLoad() override;

    private:
        /** @brief 소유 매니저가 구조 변경을 얼려 두었으면(컴포넌트 틱 중) true 입니다. */
        bool isStructureFrozen() const;
        /** @brief 색인의 칸을 지금 태그에 맞춥니다. 등록 전(색인이 없음)이면 할 일이 없습니다. `_tags` 를 바꾸는 곳은 모두 이 함수를 부릅니다. */
        void syncTagIndex();

        PROPERTY()
        TagContainer _tags;
        TagIndex*    _pTagIndex; ///< 등록한 매니저의 태그 색인이다. 등록 전과 해제 뒤에는 nullptr 이다
    };
} // namespace sw
