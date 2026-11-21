/**
 * @file CueTriggerComponent.h
 * @brief 태그를 가진 것이 들어오면 연출 신호와 소리를 내는 트리거입니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AudioPlayableAsset;

    class GameObjectManager;

    /**
     * @class CueTriggerComponent
     * @brief 태그(보통 `Player`)를 가진 것이 처음 들어오면 연출 신호(`GimmickCueEvent`, `_cue`)와 소리를 냅니다. `_bOnce` 가 꺼져 있으면 `_cooldown` 뒤에 다시 작동합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Scare Trigger", Tooltip = "Fires a scripted scare cue and sound when a tagged object enters" )
    class SW_GF_API CueTriggerComponent : public Component
    {
    public:
        REFLECT_BODY();

        CueTriggerComponent();
        virtual ~CueTriggerComponent() override = default;

        /** @brief 겹침 진입 사건을 구독합니다. */
        void  onRegister( GameObjectManager& manager ) override;
        void  onTick( TickContext& context ) override;
        int32 getFireCount() const { return _fireCount; }

    private:
        /** @brief 태그가 맞는 것이 들어오면(`GameObjectEvents::_overlapBegin`) 연출 신호와 소리를 냅니다. */
        void handleOverlapBegin( const OverlapInfo& overlap );

        Subscription _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)
        PROPERTY( Required, Category = "Scare", DisplayName = "Cue", Tooltip = "Cue name for the sequencer / camera / audio listeners" )
        hashed_string _cue;
        PROPERTY( Category = "Scare", DisplayName = "Sound" )
        AssetRef<AudioPlayableAsset> _sound;
        PROPERTY( Category = "Scare", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Scare", DisplayName = "Once" )
        bool _bOnce;
        PROPERTY( Category = "Scare", DisplayName = "Cooldown", Min = 0.0, Units = s )
        float32 _cooldown;
        PROPERTY( RuntimeState, Category = "Scare", DisplayName = "Cooldown Left", Tooltip = "Runtime", Units = s )
        float32 _cooldownLeft;
        PROPERTY( RuntimeState, Category = "Scare", DisplayName = "Fire Count", Tooltip = "Runtime" )
        int32 _fireCount;
    };
} // namespace sw
