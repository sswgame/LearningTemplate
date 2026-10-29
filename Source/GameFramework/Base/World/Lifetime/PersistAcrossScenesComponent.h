#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    REFLECT()
    class SW_GF_API PersistAcrossScenesComponent : public Component
    {
    public:
        REFLECT_BODY();
        PersistAcrossScenesComponent();
        virtual ~PersistAcrossScenesComponent() override = default;

        void onBeginPlay() override;

    private:
        PROPERTY()
        bool _bPersistent;
    };
} // namespace sw
