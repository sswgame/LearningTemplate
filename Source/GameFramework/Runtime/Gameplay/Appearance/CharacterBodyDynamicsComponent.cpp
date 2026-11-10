#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterBodyDynamicsComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Facial/WrinkleMap.h"
#include "Engine/Animation/Rig/RigJiggleNode.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/LightComponent.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterSurfaceBinding.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    namespace
    {
        struct CharacterBodyDynamicsInternal
        {
            static constexpr uint32  kAllLayers       = 0xFFFFFFFFu;
            static constexpr uint16  kSmearResolution = 64;    ///< 번짐 격자(굽기 텍스처보다 성기다 — 번짐은 부드럽다)
            static constexpr float32 kSmearDilution   = 0.5f;  ///< 흐름 한 번이 세기 대비 묽히는 비율
            static constexpr float32 kWipeRadius      = 0.08f; ///< 닦기가 끄는 UV 반지름

            static const CharacterBodyDynamicsComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                     pTarget = query.findTargetObject();
                const CharacterBodyDynamicsComponent* pFound  = pTarget != nullptr ? pTarget->getComponent<CharacterBodyDynamicsComponent>() : nullptr;
                if ( pFound == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "CharacterBodyDynamicsComponent" ) );
                return pFound;
            }

            template <float32 ( CharacterBodyDynamicsComponent::*Getter )() const>
            [[nodiscard]] static bool readValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterBodyDynamicsComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( ( pFound->*Getter )() ) );
                return true;
            }

            static CharacterBodyDynamicsComponent* findCommandTarget( const string& objectName, string& outReply )
            {
                GameObject*                     pObject = engine::findActiveObject( hashed_string( objectName ) );
                CharacterBodyDynamicsComponent* pFound  = pObject != nullptr ? pObject->getComponent<CharacterBodyDynamicsComponent>() : nullptr;
                if ( pFound == nullptr )
                    outReply = "no object named '" + objectName + "' with a CharacterBodyDynamicsComponent in the active scene";
                return pFound;
            }

            /** @brief `body.impulse <object> <x> <y> <z>` — 지글에 월드 속도 충격을 줍니다(맞음 시나리오). */
            static bool runImpulse( const vector<string>& listArgument, string& outReply )
            {
                float3 velocity{};
                if ( listArgument.size() != 4 || StringUtil::parseFloat( listArgument[1], velocity._x ) == false || StringUtil::parseFloat( listArgument[2], velocity._y ) == false ||
                     StringUtil::parseFloat( listArgument[3], velocity._z ) == false )
                    return false;
                CharacterBodyDynamicsComponent* pFound = findCommandTarget( listArgument[0], outReply );
                if ( pFound == nullptr )
                    return true;
                pFound->addImpulse( velocity );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "body.impulse %# (%# %# %#)", listArgument[0], velocity._x, velocity._y, velocity._z );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `body.fluid <object> <trigger> <seconds> [strength]` — 체액 방아쇠를 켭니다(울기 · 침 시나리오, 편집 창 미리보기). */
            static bool runFluid( const vector<string>& listArgument, string& outReply )
            {
                float32 seconds  = 0.0f;
                float32 strength = 1.0f;
                if ( listArgument.size() < 3 || listArgument.size() > 4 || StringUtil::parseFloat( listArgument[2], seconds ) == false )
                    return false;
                if ( listArgument.size() == 4 && StringUtil::parseFloat( listArgument[3], strength ) == false )
                    return false;
                CharacterBodyDynamicsComponent* pFound = findCommandTarget( listArgument[0], outReply );
                if ( pFound == nullptr )
                    return true;
                pFound->triggerFluid( hashed_string( listArgument[1] ), seconds, strength );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "body.fluid %# %# %#s x%#", listArgument[0], listArgument[1], seconds, strength );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( bodyFootLift, "Body.FootLift", "Heel foot lift (m) the target's body dynamics hands to foot IK",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::getFootLift>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyPupilSize, "Body.PupilSize", "Pupil size (0..1) after light adaptation and emotion",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::getPupilSize>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyEyeLuminance, "Body.EyeLuminance", "Light measured at the eye (sun when unoccluded, ambient, point lights)",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::getEyeLuminance>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyJiggle, "Body.JiggleDisplacement", "Largest distance (m) a jiggle tip is away from its animated pose",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::computeJiggleDisplacement>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyFluidWetness, "Body.FluidWetness", "Largest tear / saliva mask value on the target",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::computeFluidWetness>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodySmearDilution, "Body.SmearDilution", "Largest makeup smear dilution (0..1) on the target",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::computeMaxSmearDilution>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyFluidEffects, "Body.FluidEffects", "Tear drip / saliva strand effects emitting on the target",
                               &CharacterBodyDynamicsInternal::readValue<&CharacterBodyDynamicsComponent::computeActiveFluidEffects>, true, false );
    SW_DEV_COMMAND( BodyImpulse, "body.impulse", "body.impulse <object> <x> <y> <z>", "Push the object's jiggle bones with a world velocity (m/s)", &CharacterBodyDynamicsInternal::runImpulse );
    SW_DEV_COMMAND( BodyFluid, "body.fluid", "body.fluid <object> <trigger> <seconds> [strength]", "Turn on a body fluid trigger (Cry, Drool) for a while", &CharacterBodyDynamicsInternal::runFluid );
} // namespace sw

namespace sw
{
    CharacterBodyDynamicsComponent::CharacterBodyDynamicsComponent()
        : _eyeSocket{ "Eyes" }
        , _hitImpulseScale{ 0.02f }
        , _sunTraceDistance{ 50.0f }
        , _skyLightValue{}
        , _wipeEvent{}
        , _fluidSmearStrength{ 0.6f }
        , _listFluid{}
        , _listManualTrigger{}
        , _listSmear{}
        , _listSmearResistance{}
        , _pendingImpulse{}
        , _appliedHash{ 0 }
        , _pendingSeconds{ 0.0f }
        , _footLift{ 0.0f }
        , _pupilSize{ 0.5f }
        , _eyeLuminance{ 0.0f }
        , _motionScale{ 1.0f }
        , _pendingWipe{ 0.0f }
        , _bJiggleApplied{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void CharacterBodyDynamicsComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || deltaTime <= 0.0f )
            return;
        // 물리 질의 · 표면 쓰기 · 다른 컴포넌트 쓰기는 틱(워커) 밖에서 한다 — 쌓은 시간을 틱 뒤 게임 스레드가 한 번에 쓴다.
        _pendingSeconds += deltaTime;
        pManager->executeOrDeferPostTickOnComponent( *this, []( CharacterBodyDynamicsComponent& self, GameObjectManager& manager )
        { self.update( manager ); } );
    }

    void CharacterBodyDynamicsComponent::onHitReceived( const HitInfo& hit )
    {
        addImpulse( hit._direction * ( hit._impulse * _hitImpulseScale ) );
    }

    void CharacterBodyDynamicsComponent::onAnimationNotify( const AnimationNotifyInfo& notify )
    {
        if ( notify._event == _wipeEvent && notify._phase != AnimationNotifyPhase::End )
            _pendingWipe += notify._weight;
    }

    void CharacterBodyDynamicsComponent::addImpulse( const float3& worldVelocity )
    {
        _pendingImpulse += worldVelocity;
    }

    void CharacterBodyDynamicsComponent::triggerFluid( const hashed_string& trigger, float32 seconds, float32 strength )
    {
        for ( ManualTrigger& manual : _listManualTrigger )
        {
            if ( manual._trigger != trigger )
                continue;
            manual._remainingSeconds = seconds;
            manual._strength         = strength;
            return;
        }
        _listManualTrigger.push_back( ManualTrigger{ trigger, seconds, strength } );
    }

    void CharacterBodyDynamicsComponent::clearSmears()
    {
        for ( SkinLayerSmearState& smear : _listSmear )
            smear.clear();
    }

    float32 CharacterBodyDynamicsComponent::computeTriggerStrength( const hashed_string& trigger ) const
    {
        float32 strength = 0.0f;
        for ( const ManualTrigger& manual : _listManualTrigger )
        {
            if ( manual._trigger == trigger && manual._remainingSeconds > 0.0f )
                strength = MathUtil::max( strength, manual._strength );
        }
        const GameObject*                pOwner    = getOwner();
        const SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator != nullptr )
            strength = MathUtil::max( strength, pAnimator->getCurveValue( trigger ) );
        return strength;
    }

    void CharacterBodyDynamicsComponent::applyProfile( CharacterAppearanceComponent& appearance, const CharacterBodyProfileDef* pProfile, uint64 resolvedHash )
    {
        _appliedHash                      = resolvedHash;
        _bJiggleApplied                   = SW_FALSE;
        GameObject*               pOwner  = getOwner();
        FacialAnimationComponent* pFacial = pOwner->getComponent<FacialAnimationComponent>();
        if ( pFacial != nullptr )
            pFacial->setWrinkleMap( pProfile != nullptr && pProfile->_listWrinkle.empty() == false ? make_shared<const WrinkleMap>( pProfile->_listWrinkle, pProfile->_wrinkleIntensity ) : nullptr );
        else if ( pProfile != nullptr && pProfile->_listWrinkle.empty() == false )
            SW_LOG_ERROR_ONCE_ID( "BodyDynamics.WrinkleWithoutFace", "'%#': body profile '%#' maps wrinkles but the object has no FacialAnimationComponent - no wrinkles", pOwner->getName().c_str(),
                                  pProfile->_id.c_str() );

        // 번짐 상태는 해석의 피부 층마다 하나 — 층 목록이 바뀌면 새로 시작한다(번짐은 일시 상태다).
        const vector<ResolvedSkinLayer>& listLayer = appearance.getResolvedAppearance()._listSkinLayer;
        _listSmear.assign( listLayer.size(), SkinLayerSmearState{} );
        _listSmearResistance.clear();
        for ( size_t index = 0; index < listLayer.size(); ++index )
        {
            _listSmear[index].initialize( CharacterBodyDynamicsInternal::kSmearResolution );
            _listSmearResistance.push_back( listLayer[index]._smearResistance );
        }
        removeFluidEffects();
        _listFluid.clear();
        if ( pProfile == nullptr )
            return;
        for ( const BodyFluidDef& fluid : pProfile->_listFluid )
            _listFluid.push_back( FluidStream{ {}, fluid._name, {}, 0.0f } );
    }

    void CharacterBodyDynamicsComponent::update( GameObjectManager& manager )
    {
        const float32 seconds                     = _pendingSeconds;
        _pendingSeconds                           = 0.0f;
        GameObject*                   pOwner      = getOwner();
        CharacterAppearanceComponent* pAppearance = pOwner != nullptr ? pOwner->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "BodyDynamics.NoAppearance", "'%#' has a CharacterBodyDynamicsComponent without a CharacterAppearanceComponent - the body profile does nothing",
                                  pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        const AppearanceDatabase*      pDatabase = pAppearance->findDatabase();
        const ResolvedAppearance&      resolved  = pAppearance->getResolvedAppearance();
        const CharacterBodyProfileDef* pProfile  = pDatabase != nullptr && resolved._bodyProfile.empty() == false ? pDatabase->getBodyProfiles().findProfile( resolved._bodyProfile ) : nullptr;
        if ( resolved._hash != _appliedHash )
            applyProfile( *pAppearance, pProfile, resolved._hash );

        // 리그 몸 입력: 굽 높이 · 지글 물성 · 동작 배율 · 충격. 리그가 아직 묶이지 않았으면 묶인 뒤 첫 프레임에 건다.
        _footLift                        = pAppearance->getFootLift();
        PoseModifierComponent* pModifier = pOwner->getComponent<PoseModifierComponent>();
        _motionScale                     = pProfile != nullptr ? pProfile->computeActionScale( pOwner->getTags() ) : 1.0f;
        if ( pModifier != nullptr && pModifier->isRigReady() )
        {
            RigBodyInput& input = pModifier->getRigInstance().getBodyInput();
            input.setFootLift( _footLift );
            input.setMotionScale( _motionScale );
            input.addImpulse( _pendingImpulse );
            if ( _bJiggleApplied == SW_FALSE )
            {
                input.setJiggleGroups( pProfile != nullptr ? pProfile->_listJiggle : vector<RigJiggleGroupParams>{} );
                (void)RigJiggleNode::checkGroups( pModifier->getRigInstance(), pModifier->getRigPath() ); // 없는 묶음은 오류로 남는다 — 그 노드는 쉰다
                _bJiggleApplied = SW_TRUE;
            }
        }
        _pendingImpulse = float3{};
        if ( pProfile == nullptr || seconds <= 0.0f )
            return;
        if ( pProfile->_bHasPupil == SW_TRUE )
            updatePupil( manager, *pAppearance, *pProfile, seconds );
        updateFluids( *pAppearance, *pProfile, seconds );
        for ( ManualTrigger& manual : _listManualTrigger )
            manual._remainingSeconds = MathUtil::max( 0.0f, manual._remainingSeconds - seconds );
    }

    float32 CharacterBodyDynamicsComponent::measureEyeLuminance( GameObjectManager& manager, const float3& eye ) const
    {
        float32      luminance = 0.0f;
        const uint64 ownerID   = getOwner()->getObjectID();
        // 날씨: 판 상태의 날씨 값(`_skyLightValue` — 맑음 1, 흐림 · 비 · 안개는 낮다)이 직사광을 줄인다. 날씨가 없는 씬은 맑은 하늘이다.
        const SessionStateComponent* pSession = _skyLightValue.empty() ? nullptr : SessionStateComponent::findFirstOpen( &manager );
        const float32                skyLight = pSession != nullptr ? MathUtil::saturate( pSession->getWeather().computeValue( _skyLightValue ) ) : 1.0f;
        for ( LightComponent* pLight : manager.getComponentRegistry().getAll<LightComponent>( LightComponent::getRegistryIndex( LightKind::Directional ) ) )
        {
            const DirectionalLightComponent* pSun = static_cast<const DirectionalLightComponent*>( pLight );
            if ( pSun == nullptr || pSun->isActive() == false )
                continue;
            // 해 쪽으로 쏜 광선이 막히면 그늘 · 실내다 — 주변광만 받는다. 시간대 · 날씨는 해 세기(`DaylightComponent`)에 이미 들어 있다.
            const float3    towardSun = -pSun->getLightDirection();
            CharacterRayHit blocker;
            const bool      bShaded = _sunTraceDistance > 0.0f && CharacterRayHit::raycast3D( manager, eye, towardSun, _sunTraceDistance, CharacterBodyDynamicsInternal::kAllLayers, ownerID, blocker );
            const float32   ambient = pSun->getAmbient() * pSun->getIntensity();
            luminance += ( bShaded ? 0.0f : pSun->getIntensity() * skyLight ) + ambient;
        }
        for ( LightComponent* pLight : manager.getComponentRegistry().getAll<LightComponent>( LightComponent::getRegistryIndex( LightKind::Point ) ) )
        {
            if ( pLight == nullptr || pLight->isActive() == false )
                continue;
            const float32 distanceSquared = ( pLight->getLightPosition() - eye ).getLengthSquared();
            luminance += pLight->getIntensity() / ( 1.0f + distanceSquared ); // 거리 제곱 감쇠(가까운 횃불 · 실내등)
        }
        return luminance;
    }

    void CharacterBodyDynamicsComponent::updatePupil( GameObjectManager& manager, CharacterAppearanceComponent& appearance, const CharacterBodyProfileDef& profile, float32 seconds )
    {
        float4x4 eyeWorld{};
        if ( appearance.findSocketWorldTransform( _eyeSocket, eyeWorld ) == false )
        {
            SW_LOG_ERROR_ONCE_ID( "BodyDynamics.EyeSocketMissing", "'%#': eye socket '%#' is not in the appearance - pupil adaptation stops", getOwner()->getName().c_str(), _eyeSocket.c_str() );
            return;
        }
        const BodyPupilDef& pupil        = profile._pupil;
        _eyeLuminance                    = measureEyeLuminance( manager, eyeWorld.getTranslation() );
        const float32 emotion            = pupil._emotionCurve.empty() ? 0.0f : computeTriggerStrength( pupil._emotionCurve );
        const float32 target             = MathUtil::saturate( pupil.computeTargetSize( _eyeLuminance ) + emotion * pupil._emotionScale );
        _pupilSize                       = pupil.advance( _pupilSize, target, seconds );
        SkeletalMeshComponent* pBody     = getOwner()->getComponent<SkeletalMeshComponent>();
        MaterialInstance*      pInstance = pBody != nullptr ? CharacterSurfaceBinding::acquireInstance( *pBody ) : nullptr;
        if ( pInstance != nullptr )
            pInstance->setScalarParameter( pupil._parameter, _pupilSize ); // 눈 셰이딩 기능(머티리얼 작업)이 같은 이름을 읽는다
    }

    void CharacterBodyDynamicsComponent::updateFluids( CharacterAppearanceComponent& appearance, const CharacterBodyProfileDef& profile, float32 seconds )
    {
        CharacterSurfaceState&    surface  = appearance.getSurfaceState();
        const ResolvedAppearance& resolved = appearance.getResolvedAppearance();
        for ( size_t fluidIndex = 0; fluidIndex < profile._listFluid.size() && fluidIndex < _listFluid.size(); ++fluidIndex )
        {
            const BodyFluidDef& def    = profile._listFluid[fluidIndex];
            FluidStream&        stream = _listFluid[fluidIndex];
            const float32       amount = computeTriggerStrength( def._trigger );
            stream._emitAccumulator += amount * def._ratePerSecond * seconds;
            while ( stream._emitAccumulator >= 1.0f )
            {
                stream._listDistance.push_back( 0.0f );
                stream._emitAccumulator -= 1.0f;
            }
            if ( stream._listDistance.empty() || appearance.hasSurfaceState() == false )
            {
                updateFluidEffect( appearance, def, stream, amount, 0, seconds );
                continue;
            }
            // 자국을 찍을 부품 — 이름을 적었으면 그 몸 부품, 아니면 몸 첫 부품(해석 순서의 0 번이 몸이다).
            uint32 partIndex = 0;
            for ( size_t index = 0; def._part.empty() == false && index < resolved._listPart.size(); ++index )
            {
                if ( resolved._listPart[index]._owner.empty() && resolved._listPart[index]._partName == def._part )
                    partIndex = static_cast<uint32>( index );
            }
            const float32 length = def.computePathLength();
            for ( float32& distance : stream._listDistance )
            {
                const float2 from = def.samplePath( distance );
                distance += def._speed * seconds;
                const float2 to = def.samplePath( distance );
                if ( surface.stampHit( partIndex, CharacterGeometryConstant::kNoGroup, def._channel, to, def._radius, def._wetValue ) == false )
                    SW_LOG_ERROR_ONCE_ID( "BodyDynamics.FluidChannelUnknown", "'%#': fluid '%#' channel '%#' is not in the surface channels - no tears are drawn", getOwner()->getName().c_str(), def._name.c_str(),
                                          def._channel.c_str() );
                if ( def._residueChannel.empty() == false )
                    (void)surface.stampHit( partIndex, CharacterGeometryConstant::kNoGroup, def._residueChannel, to, def._radius, def._residueValue );
                // 화장 번짐 — 눈물 길 아래의 층이 흐름 방향으로 끌리고 묽어진다(층의 번짐 저항만큼 덜).
                const float2  flow    = to - from;
                const float32 length2 = flow.getLength();
                if ( length2 <= 0.0f )
                    continue;
                for ( size_t layerIndex = 0; layerIndex < _listSmear.size(); ++layerIndex )
                    _listSmear[layerIndex].advect( to, flow * ( 1.0f / length2 ), def._radius * 2.0f, _fluidSmearStrength * seconds, _listSmearResistance[layerIndex], CharacterBodyDynamicsInternal::kSmearDilution );
            }
            uint32 arrivedCount = 0; // 이번에 경로 끝(턱)에 닿은 줄기 — 방울이 된다
            for ( const float32 distance : stream._listDistance )
                arrivedCount += distance > length ? 1u : 0u;
            VectorUtil::removeIf( stream._listDistance, [length]( float32 distance )
            { return distance > length; } );
            updateFluidEffect( appearance, def, stream, amount, arrivedCount, seconds );
        }
        if ( _pendingWipe > 0.0f )
        {
            // 닦기: 눈가에서 옆으로 끈다(첫 체액 출처 둘레 — 출처가 없으면 얼굴 가운데로 본다).
            const float2 center = profile._listFluid.empty() ? float2{ 0.5f, 0.5f } : profile._listFluid.front().samplePath( 0.0f );
            for ( size_t layerIndex = 0; layerIndex < _listSmear.size(); ++layerIndex )
                _listSmear[layerIndex].drag( center, float2{ 1.0f, 0.0f }, CharacterBodyDynamicsInternal::kWipeRadius, _pendingWipe, _listSmearResistance[layerIndex] );
            _pendingWipe = 0.0f;
        }
    }

    void CharacterBodyDynamicsComponent::updateFluidEffect( CharacterAppearanceComponent& appearance, const BodyFluidDef& def, FluidStream& stream, float32 amount, uint32 arrivedCount,
                                                            float32 seconds )
    {
        if ( def._effect.empty() )
            return;
        // 방울은 줄기가 턱에 닿은 뒤 잠시, 가닥은 방아쇠가 켜진 동안이다.
        if ( arrivedCount > 0 )
            stream._dripRemaining = def._dripSeconds;
        const bool bWanted    = def._bStrand == SW_TRUE ? amount > 0.0f : stream._dripRemaining > 0.0f;
        stream._dripRemaining = MathUtil::max( 0.0f, stream._dripRemaining - seconds );

        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner->getManager();
        VFXComponent*      pEffect  = pManager != nullptr ? pManager->resolveComponent( stream._effect ) : nullptr;
        if ( pEffect == nullptr )
        {
            if ( bWanted == false )
                return; // 아직 한 번도 켜지 않았다 — 만들지 않는다
            pEffect = pOwner->addComponent<VFXComponent>();
            if ( pEffect == nullptr )
                return;
            pEffect->setEffectPath( def._effect.viewOrEmpty() );
            pEffect->play(); // 방출기는 되풀이다 — 켜고 끄기는 방출기 단위로(꺼도 떨어지던 방울은 끝까지 떨어진다)
            stream._effect    = TComponentHandle<VFXComponent>{ pEffect };
            stream._bEffectOn = SW_TRUE;
        }
        float4x4 socketWorld{};
        if ( appearance.findSocketWorldTransform( def._socket, socketWorld ) == false )
        {
            SW_LOG_ERROR_ONCE_ID( "BodyDynamics.FluidSocketMissing", "'%#': fluid '%#' socket '%#' is not in the appearance - no drips or strands", pOwner->getName().c_str(), def._name.c_str(),
                                  def._socket.c_str() );
            return;
        }
        pEffect->setWorldPosition( socketWorld.getTranslation() );
        if ( bWanted == ( stream._bEffectOn == SW_TRUE ) )
            return;
        for ( uint32 emitterIndex = 0; emitterIndex < pEffect->getEmitterCount(); ++emitterIndex )
            (void)pEffect->setEmitterEnabled( emitterIndex, bWanted );
        stream._bEffectOn = bWanted ? SW_TRUE : SW_FALSE;
    }

    void CharacterBodyDynamicsComponent::removeFluidEffects()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( FluidStream& stream : _listFluid )
        {
            VFXComponent* pEffect = pManager != nullptr ? pManager->resolveComponent( stream._effect ) : nullptr;
            if ( pEffect != nullptr )
                (void)pOwner->removeComponent( pEffect );
            stream._effect = TComponentHandle<VFXComponent>{};
        }
    }

    float32 CharacterBodyDynamicsComponent::computeActiveFluidEffects() const
    {
        float32 count = 0.0f;
        for ( const FluidStream& stream : _listFluid )
            count += stream._bEffectOn == SW_TRUE ? 1.0f : 0.0f;
        return count;
    }

    float32 CharacterBodyDynamicsComponent::computeJiggleDisplacement() const
    {
        const GameObject*            pOwner    = getOwner();
        const PoseModifierComponent* pModifier = pOwner != nullptr ? pOwner->getComponent<PoseModifierComponent>() : nullptr;
        if ( pModifier == nullptr || pModifier->isRigReady() == false )
            return 0.0f;
        float32            largest  = 0.0f;
        const RigInstance& instance = pModifier->getRigInstance();
        for ( uint32 nodeIndex = 0; nodeIndex < instance.getNodeCount(); ++nodeIndex )
        {
            const RigNode& node = instance.getNode( nodeIndex );
            if ( string_view( node.getTypeName() ) != RigJiggleNode::kTypeName )
                continue;
            const RigSpringChain& chain        = static_cast<const RigJiggleNode&>( node ).getChain();
            const vector<float3>& listParticle = chain.getParticles();
            const vector<float3>& listAnimated = chain.getAnimatedPositions();
            if ( listParticle.empty() || listParticle.size() != listAnimated.size() )
                continue;
            largest = MathUtil::max( largest, ( listParticle.back() - listAnimated.back() ).getLength() );
        }
        return largest;
    }

    float32 CharacterBodyDynamicsComponent::computeFluidWetness() const
    {
        const GameObject*                   pOwner      = getOwner();
        const CharacterAppearanceComponent* pAppearance = pOwner != nullptr ? pOwner->getComponent<CharacterAppearanceComponent>() : nullptr;
        const AppearanceDatabase*           pDatabase   = pAppearance != nullptr ? pAppearance->findDatabase() : nullptr;
        if ( pDatabase == nullptr || pAppearance->hasSurfaceState() == false )
            return 0.0f;
        const CharacterBodyProfileDef* pProfile = pDatabase->getBodyProfiles().findProfile( pAppearance->getResolvedAppearance()._bodyProfile );
        if ( pProfile == nullptr )
            return 0.0f;
        const CharacterSurfaceState& surface = pAppearance->getSurfaceState();
        float32                      largest = 0.0f;
        for ( const BodyFluidDef& fluid : pProfile->_listFluid )
        {
            for ( uint32 part = 0; part < surface.getPartCount(); ++part )
            {
                const SurfaceMask* pMask = surface.findMask( part, fluid._channel );
                if ( pMask == nullptr )
                    continue;
                for ( const float32 texel : pMask->getTexels() )
                    largest = MathUtil::max( largest, texel );
            }
        }
        return largest;
    }

    float32 CharacterBodyDynamicsComponent::computeMaxSmearDilution() const
    {
        float32 largest = 0.0f;
        for ( const SkinLayerSmearState& smear : _listSmear )
        {
            const uint32  resolution = smear.getResolution();
            const float32 last       = resolution > 1u ? static_cast<float32>( resolution - 1u ) : 1.0f;
            for ( uint32 y = 0; y < resolution; ++y )
            {
                for ( uint32 x = 0; x < resolution; ++x )
                    largest = MathUtil::max( largest, smear.sampleDilution( float2{ static_cast<float32>( x ) / last, static_cast<float32>( y ) / last } ) );
            }
        }
        return largest;
    }
} // namespace sw
