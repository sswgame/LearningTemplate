#pragma once
#include "Core/Container/string.h"

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
        virtual ~DontDestroyOnLoadComponent() override                                 = default;
        DontDestroyOnLoadComponent( DontDestroyOnLoadComponent&& ) noexcept            = default;
        DontDestroyOnLoadComponent& operator=( DontDestroyOnLoadComponent&& ) noexcept = default;

        void onBeginPlay() override;
        void onEndPlay() override;

    private:
        PROPERTY( Alias = "persistentTag" )
        string _persistentTag;
        PROPERTY( Alias = "bPersistent" )
        bool _bPersistent;
    };
} // namespace sw
