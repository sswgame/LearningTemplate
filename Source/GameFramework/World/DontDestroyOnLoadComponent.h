#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    REFLECT()
    class SW_GF_API DontDestroyOnLoadComponent : public Component
    {
    public:
        REFLECT_BODY();
        DontDestroyOnLoadComponent();
        virtual ~DontDestroyOnLoadComponent() override = default;

        void onBeginPlay() override;

    private:
        PROPERTY()
        bool _bPersistent;
    };
} // namespace sw
