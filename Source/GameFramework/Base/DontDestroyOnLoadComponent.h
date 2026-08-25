#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	struct SW_GF_API DontDestroyOnLoadData
	{
		REFLECT_BODY();
		PROPERTY()
		bool bPersistent{ true };
		PROPERTY()
		string persistentTag{ "Persistent" };
	};

	REFLECT()
	class SW_GF_API DontDestroyOnLoadComponent : public Component
	{
	public:
		REFLECT_BODY();
		DontDestroyOnLoadComponent()												   = default;
		virtual ~DontDestroyOnLoadComponent() override								   = default;
		DontDestroyOnLoadComponent( DontDestroyOnLoadComponent&& ) noexcept			   = default;
		DontDestroyOnLoadComponent& operator=( DontDestroyOnLoadComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		DontDestroyOnLoadData* getPersistData() const;
		DontDestroyOnLoadData* ensurePersistData();
	};
} // namespace sw
