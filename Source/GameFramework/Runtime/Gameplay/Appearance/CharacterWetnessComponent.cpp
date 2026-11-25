#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessSystem.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WeatherShelter.h"

namespace sw
{
    namespace
    {
        struct CharacterWetnessComponentInternal
        {
            /** @brief 감시 값 대상의 노출 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const CharacterWetnessComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<CharacterWetnessComponent>( "CharacterWetnessComponent", outValue );
            }

            /** @brief 감시 값 `Surface.Submersion` 입니다. 대상의 마지막 잠긴 비율(0..1)입니다. */
            [[nodiscard]] static bool readSubmersion( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterWetnessComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFound->getSubmersion() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.RainExposure` 입니다. 대상이 맞는 비(날씨 값, 지붕 아래면 0)입니다. */
            [[nodiscard]] static bool readRainExposure( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterWetnessComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFound->getRainExposure() ) );
                return true;
            }

            /** @brief `surface.set <object> <channel> <value>` — 오브젝트 외형의 몸 표면 채널을 모든 영역에서 값으로 정합니다(씻김 · 마름 시나리오의 시작값). */
            static bool runSet( const vector<string>& listArgument, string& outReply )
            {
                float32 value = 0.0f;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], value ) == false )
                    return false;
                GameObject*                   pObject     = engine::findActiveObject( hashed_string( listArgument[0] ) );
                CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
                if ( pAppearance == nullptr || pAppearance->hasSurfaceState() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "surface.set: no object named '%#' with a CharacterAppearanceComponent surface state in the active scene", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                CharacterSurfaceState& state   = pAppearance->getSurfaceState();
                const hashed_string    channel = hashed_string( listArgument[1] );
                bool                   bSet    = true;
                for ( uint32 region = 0; region < state.getRegionCount(); ++region )
                {
                    bSet = state.setRegionValue( region, channel, value ) && bSet;
                }
                if ( bSet == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "surface.set: unknown surface channel '%#'", listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "surface.set %# %# = %#", listArgument[0], listArgument[1], value );
                outReply = string( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceSubmersion, "Surface.Submersion", "Submerged fraction (0..1) the target's CharacterWetnessComponent measured last", &CharacterWetnessComponentInternal::readSubmersion, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( surfaceRainExposure, "Surface.RainExposure", "Rain strength on the target (weather value; 0 under a roof)", &CharacterWetnessComponentInternal::readRainExposure, true, false );
    SW_DEV_COMMAND( SurfaceSet, "surface.set", "surface.set <object> <channel> <value>", "Set a body surface channel (Wet, Dirt, Blood...) of an object's appearance in every region",
                    &CharacterWetnessComponentInternal::runSet );
} // namespace sw

namespace sw
{
    CharacterWetnessComponent::CharacterWetnessComponent()
        : _wetChannel{}
        , _precipitationValue{}
        , _rainWetPerSecond{ 0.1f }
        , _shelterDistance{ 20.0f }
        , _immersionWetPerSecond{ 0.5f }
        , _bodyHeight{ 1.8f }
        , _pendingSeconds{ 0.0f }
        , _submersion{ 0.0f }
        , _rainExposure{ 0.0f }
        , _waterDepth{ 0.0f }
    {
    }

    void CharacterWetnessComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        (void)CharacterWetnessSystem::getOrCreate( manager );
    }

    void CharacterWetnessComponent::onUnregister( GameObjectManager& manager )
    {
        CharacterWetnessSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void CharacterWetnessComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        // 물리 질의와 외형의 표면 쓰기는 틱(워커) 밖에서 한다 — 쌓은 시간을 시스템이 틱 뒤 게임 스레드에서 한 번에 쓴다.
        // 틱이 도는 동안만(플레이) 시간이 쌓이므로 편집 모드에서는 시스템이 돌아도 아무것도 바꾸지 않는다.
        if ( context.getDeltaTime().count() > 0.0f )
            _pendingSeconds += context.getDeltaTime().count();
    }

    void CharacterWetnessComponent::applyExposure( GameObjectManager& manager )
    {
        const float32 seconds = _pendingSeconds;
        _pendingSeconds       = 0.0f;
        if ( seconds <= 0.0f )
            return;
        GameObject*                   pOwner      = getOwner();
        CharacterAppearanceComponent* pAppearance = pOwner != nullptr ? pOwner->getComponent<CharacterAppearanceComponent>() : nullptr;
        const SceneComponent*         pScene      = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pAppearance == nullptr || pScene == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "CharacterWetness.NoAppearance", "'%#' has a CharacterWetnessComponent without a CharacterAppearanceComponent and a scene component - rain and water change nothing",
                                  pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        if ( pAppearance->hasSurfaceState() == false )
            return; // 외형이 아직 조립 전이거나 표면 채널을 끈 캐릭터다(외형이 까닭을 남긴다)
        CharacterSurfaceState& state = pAppearance->getSurfaceState();
        const float3           feet  = pScene->getWorldPosition();

        // 물: 발 자리의 깊이를 키로 나눈 비율이 잠긴 정도다.
        const WaterBodyComponent* pWater = WaterBodyComponent::findWaterAt( manager, feet._x, feet._z );
        float32                   depth  = 0.0f;
        _waterDepth                      = pWater != nullptr && pWater->isUnderwater( feet, depth ) ? depth : 0.0f;
        _submersion                      = MathUtil::saturate( _waterDepth / _bodyHeight );

        // 비: 날씨 값 × 하늘이 열렸는지. 날씨가 없는 씬은 비가 없다(판 상태가 날씨를 열지 않았다).
        const float32 precipitation = MathUtil::max( 0.0f, GameStateComponent::readWeather( manager, _precipitationValue ) );
        bool          bSheltered    = false;
        if ( precipitation > 0.0f && _shelterDistance > 0.0f )
            bSheltered = WeatherShelter::isSheltered( manager, feet + float3{ 0.0f, _bodyHeight, 0.0f }, _shelterDistance, pOwner->getObjectID() );
        _rainExposure = bSheltered ? 0.0f : precipitation;

        const float32 wetGain = ( _rainExposure * _rainWetPerSecond + _submersion * _immersionWetPerSecond ) * seconds;
        if ( wetGain > 0.0f )
        {
            bool bKnownChannel = true;
            for ( uint32 region = 0; region < state.getRegionCount(); ++region )
            {
                bKnownChannel = state.addRegionValue( region, _wetChannel, wetGain ) && bKnownChannel;
            }
            if ( bKnownChannel == false )
                SW_LOG_ERROR_ONCE_ID( "CharacterWetness.WetChannelUnknown", "'%#': wet channel '%#' is not in the appearance surface channels - nothing gets wet", pOwner->getName().c_str(),
                                      _wetChannel.c_str() );
        }
        state.applyWater( seconds, _submersion );
    }
} // namespace sw
