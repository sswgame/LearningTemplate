/**
 * @file TagComponent.h
 * @brief GameObject에 태그 정보를 부여하는 컴포넌트 및 순수 ECS TagData
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	/**
	 * @brief Pure ECS Data Struct for Tags
	 */
	REFLECT()
	struct SW_API TagData
	{
		REFLECT_BODY();
		TagContainer tags;
	};

	/**
	 * @brief GameObject가 가지는 태그들을 담는 Facade 컴포넌트입니다.
	 * @details ECS 체제에서 TagData(Registry 풀)를 참조/조작합니다.
	 */
	REFLECT()
	class SW_API TagComponent : public Component
	{
	public:
		REFLECT_BODY();
		TagComponent()					 = default;
		virtual ~TagComponent() override = default;

		TagComponent( const TagComponent& )			   = delete;
		TagComponent& operator=( const TagComponent& ) = delete;

		TagComponent( TagComponent&& other ) noexcept			 = default;
		TagComponent& operator=( TagComponent&& other ) noexcept = default;

		void onBeginPlay() override;

		TagData* getTagData() const;

		TagContainer&		getTags();
		const TagContainer& getTags() const;

		void addTag( TagID tag );
		void removeTag( TagID tag );
		void clearTags();
		bool hasTag( TagID tag, bool bExactMatch = false ) const;
		bool matchTags( const TagContainer& required, const TagContainer& forbidden ) const;
		bool matchesQuery( const TagQuery& query ) const;
	};
} // namespace sw
