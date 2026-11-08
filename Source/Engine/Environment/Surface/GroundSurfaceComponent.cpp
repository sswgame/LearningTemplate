#include "pch.h"

#include "Engine/Environment/Surface/GroundSurfaceComponent.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct GroundSurfaceComponentInternal
        {
            /** @brief 발 위에서 쏘기 시작하는 높이(m)입니다 — 발이 바닥에 조금 묻혀도 바닥을 맞힌다. */
            static constexpr float32 kProbeLift = 0.3f;

            /** @brief @p object 의 발(주 씬 컴포넌트 자리) 아래 표면을 읽습니다. 씬 · 표 · 맞은 것이 없으면 false 입니다. */
            static bool sampleUnder( GameObject& object, float32 probeLength, SurfaceSample& outSample )
            {
                GameObjectManager*                       pManager = object.getManager();
                const SceneComponent*                    pScene   = object.getPrimarySceneComponent();
                const shared_ptr<const SurfaceKindTable> pTable   = pManager != nullptr && pScene != nullptr ? SurfaceKindTable::findDefault() : nullptr;
                if ( pTable == nullptr )
                    return false;
                const float3 feet = pScene->getWorldPosition();
                return SurfaceQuery::sampleBelow( *pManager, pTable, feet + float3{ 0.0f, kProbeLift, 0.0f }, feet - float3{ 0.0f, probeLength, 0.0f }, object.getObjectID(), false, outSample );
            }

            /** @brief 감시 값 `Surface.GroundKind` 입니다. 대상의 `GroundSurfaceComponent` 가 마지막으로 읽은 표면 종류입니다(디딘 것이 없으면 값이 없다). */
            [[nodiscard]] static bool readGroundKind( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*             pTarget = query.findTargetObject();
                const GroundSurfaceComponent* pGround = pTarget != nullptr ? pTarget->getComponent<GroundSurfaceComponent>() : nullptr;
                if ( pGround == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "GroundSurfaceComponent" ) );
                    return false;
                }
                if ( pGround->getGroundKind().empty() )
                {
                    outValue.setText( "no ground under the target" );
                    return false;
                }
                outValue.setText( pGround->getGroundKind().c_str() );
                return true;
            }

            /** @brief 감시 값 `Surface.MoveScale` 입니다. 대상 캐릭터 컨트롤러가 지난 물리 프레임에 걷는 속도에 곱한 계수입니다. */
            [[nodiscard]] static bool readMoveScale( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*                   pTarget     = query.findTargetObject();
                const CharacterControllerComponent* pController = pTarget != nullptr ? pTarget->getComponent<CharacterControllerComponent>() : nullptr;
                if ( pController == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "CharacterControllerComponent" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pController->getMoveScale() ) );
                return true;
            }

            /** @brief `surface.probe <object> [length]` — 오브젝트 발 아래의 표면(종류 · 형식 · 재질 · 지형 레이어 · 법선 · 표면 속도 · 물 깊이 · 이동 계수)을 적습니다. */
            static bool runProbe( const vector<string>& listArgument, string& outReply )
            {
                float32    length  = 1.0f;
                const bool bParsed = ( listArgument.size() == 1 || listArgument.size() == 2 ) && ( listArgument.size() == 1 || StringUtil::parseFloat( listArgument[1], length ) );
                if ( bParsed == false || length <= 0.0f )
                    return false;
                GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                if ( pObject == nullptr )
                {
                    outReply = "surface.probe: no object named '" + listArgument[0] + "' in the active scene";
                    return true;
                }
                SurfaceSample sample;
                if ( sampleUnder( *pObject, length, sample ) == false )
                {
                    outReply = "surface.probe: nothing with a surface kind under '" + listArgument[0] + "' (see Surface.KindMissing in the log)";
                    return true;
                }
                outReply = formatToString( "surface %# kind=%# source=%# material=%# layer=%# point=%#,%#,%# normal=%#,%#,%# velocity=%#,%#,%# water=%# moveScale=%#", listArgument[0],
                                           sample._pKind->_name.c_str(), SurfaceQuery::getSourceName( sample._source ), sample._material.c_str(),
                                           sample._terrainLayer == SurfaceSample::kNoTerrainLayer ? -1 : static_cast<int32>( sample._terrainLayer ), sample._point._x, sample._point._y,
                                           sample._point._z, sample._normal._x, sample._normal._y, sample._normal._z, sample._surfaceVelocity._x, sample._surfaceVelocity._y,
                                           sample._surfaceVelocity._z, sample._waterDepth, sample._pKind->_moveScale );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceGroundKind, "Surface.GroundKind", "Surface kind the target's GroundSurfaceComponent stood on last", &GroundSurfaceComponentInternal::readGroundKind, true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceMoveScale, "Surface.MoveScale", "Move scale the target's character controller applied to its walk speed last physics frame",
                               &GroundSurfaceComponentInternal::readMoveScale, true, false );
    SW_DEV_COMMAND( SurfaceProbe, "surface.probe", "surface.probe <object> [length]",
                    "Print the surface under an object's feet: kind, source (collider, terrain layer, water), material, normal, surface velocity, water depth, move scale",
                    &GroundSurfaceComponentInternal::runProbe );
} // namespace sw

namespace sw
{
    GroundSurfaceComponent::GroundSurfaceComponent()
        : _probeLength{ 0.6f }
        , _lastSample{}
        , _groundKind{}
        , _bSampledThisFrame{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void GroundSurfaceComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<GroundSurfaceComponent>( this );
    }

    void GroundSurfaceComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<GroundSurfaceComponent>( this );
        Component::onUnregister( manager );
    }

    void GroundSurfaceComponent::sampleGroundOnce( GameObjectManager& manager )
    {
        if ( _bSampledThisFrame == SW_TRUE )
            return;
        sampleGround( manager );
        _bSampledThisFrame = SW_TRUE;
    }

    void GroundSurfaceComponent::finishFrameSample( GameObjectManager& manager )
    {
        if ( _bSampledThisFrame == SW_FALSE )
            sampleGround( manager );
        _bSampledThisFrame = SW_FALSE;
    }

    void GroundSurfaceComponent::sampleGround( GameObjectManager& manager )
    {
        (void)manager;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        SurfaceSample sample;
        const bool    bSampled                    = GroundSurfaceComponentInternal::sampleUnder( *pOwner, _probeLength, sample );
        _lastSample                               = bSampled ? sample : SurfaceSample{};
        _groundKind                               = bSampled ? sample._pKind->_name : hashed_string{};
        CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>();
        if ( bSampled && pController != nullptr )
            pController->applyMoveScale( sample._pKind->_moveScale );
    }
} // namespace sw
