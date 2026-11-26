#include "pch.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    REFLECT()
    class PersistAcrossScenesComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics;
        PersistAcrossScenesComponent();
        virtual ~PersistAcrossScenesComponent() override = default;

        void onBeginPlay() override;

    private:
        PROPERTY()
        bool _bPersistent;
    };

    PersistAcrossScenesComponent::PersistAcrossScenesComponent()
        : _bPersistent{ true }
    {
    }

    void PersistAcrossScenesComponent::onBeginPlay()
    {
        Component::onBeginPlay();

        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        // 씬 매니저가 루트를 씬 전환 너머로 옮겨 심는다(유니티 `Object.PersistAcrossScenes` — 태그를 붙이지 않는다).
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( _bPersistent && pSceneManager != nullptr )
            pSceneManager->markPersistent( pOwner );
    }
} // namespace sw

#include "PersistAcrossScenesComponent.cpp.gen.inl"
