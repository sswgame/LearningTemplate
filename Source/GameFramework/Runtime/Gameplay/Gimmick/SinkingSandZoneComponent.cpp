#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/SinkingSandZoneComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        struct SinkingSandZoneComponentInternal
        {
            /** @brief 감시 값 대상의 점유 상태입니다. 대상이 없거나 어느 구역의 점유자도 아니면 까닭을 적고 nullptr 입니다. */
            static const SinkingSandZoneComponent::OccupantRecord* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject();
                if ( pTarget == nullptr || query._pManager == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return nullptr;
                }
                return SinkingSandZoneComponent::findInScene( *query._pManager, pTarget->getHandle() );
            }

            /** @brief 감시 값 `SinkingSand.Depth` 입니다. 대상이 빠진 깊이(m)이고, 구역 밖이면 0 입니다. */
            [[nodiscard]] static bool readDepth( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query.findTargetObject() == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                const SinkingSandZoneComponent::OccupantRecord* pRecord = findTarget( query, outValue );
                outValue.setNumber( pRecord != nullptr ? static_cast<float64>( pRecord->_depth ) : 0.0 );
                return true;
            }

            /** @brief 감시 값 `SinkingSand.Phase` 입니다. 대상의 단계 이름(`Sinking` · `Engulfed`)이고, 구역 밖이면 `None` 입니다. */
            [[nodiscard]] static bool readPhase( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query.findTargetObject() == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                const SinkingSandZoneComponent::OccupantRecord* pRecord = findTarget( query, outValue );
                outValue.setText( pRecord != nullptr ? getEnumName( pRecord->_phase ) : "None" );
                return true;
            }

            /** @brief 감시 값 `SinkingSand.Escapes` 입니다. 대상이 뛰어서 깊이를 줄인 수이고, 구역 밖이면 0 입니다. */
            [[nodiscard]] static bool readEscapes( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query.findTargetObject() == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                const SinkingSandZoneComponent::OccupantRecord* pRecord = findTarget( query, outValue );
                outValue.setNumber( pRecord != nullptr ? static_cast<float64>( pRecord->_escapeCount ) : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( sinkingSandDepth, "SinkingSand.Depth", "Depth (m) the target sank in a sinking sand zone; 0 outside every zone", &SinkingSandZoneComponentInternal::readDepth, true, false );
    SW_AUTOMATION_QUERY_WATCH( sinkingSandPhase, "SinkingSand.Phase", "Sinking sand phase of the target: Sinking, Engulfed, or None outside every zone", &SinkingSandZoneComponentInternal::readPhase, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( sinkingSandEscapes, "SinkingSand.Escapes", "Jumps that pulled the target up in its sinking sand zone; 0 outside every zone", &SinkingSandZoneComponentInternal::readEscapes,
                               true, false );
} // namespace sw

namespace sw
{
    SinkingSandZoneComponent::OccupantRecord::OccupantRecord()
        : _object{}
        , _visual{}
        , _visualPosition{}
        , _depth{ 0.0f }
        , _secondsAtBottom{ 0.0f }
        , _escapeCount{ 0 }
        , _phase{ SinkingSandPhase::Sinking }
        , _bWasGrounded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SinkingSandZoneComponent::SinkingSandZoneComponent()
        : _halfExtents{ 2.0f, 1.0f, 2.0f }
        , _sinkSpeed{ 0.15f }
        , _maxDepth{ 1.0f }
        , _moveScaleAtMaxDepth{ 0.1f }
        , _escapePerJump{ 0.25f }
        , _engulfSeconds{ 2.0f }
        , _listOccupant{}
        , _pendingSeconds{ 0.0f }
    {
    }

    void SinkingSandZoneComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || deltaTime <= 0.0f )
            return;
        _pendingSeconds += deltaTime;
        pManager->executeOrDeferPostTickOnComponent( *this, []( SinkingSandZoneComponent& self, GameObjectManager& manager )
        { self.updateOccupants( manager ); } );
    }

    void SinkingSandZoneComponent::onEndPlay()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( const OccupantRecord& record : _listOccupant )
        {
            if ( pManager != nullptr )
                restoreVisual( *pManager, record );
        }
        _listOccupant.clear();
        Component::onEndPlay();
    }

    void SinkingSandZoneComponent::updateOccupants( GameObjectManager& manager )
    {
        const float32 seconds        = _pendingSeconds;
        _pendingSeconds              = 0.0f;
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pZone  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pZone == nullptr || seconds <= 0.0f )
            return;
        const float3 center = pZone->getWorldPosition();

        // 점유자 고르기: 발(컨트롤러 자리)이 상자 안인 캐릭터. 나간 점유자는 표현을 되돌리고 뺀다.
        vector<CharacterControllerComponent*> listInside;
        manager.forEachComponentOfType<CharacterControllerComponent>( [&]( CharacterControllerComponent* pController )
        {
            const float3 offset  = pController->getWorldPosition() - center;
            const bool   bInside = MathUtil::abs( offset._x ) <= _halfExtents._x && MathUtil::abs( offset._y ) <= _halfExtents._y && MathUtil::abs( offset._z ) <= _halfExtents._z;
            if ( bInside && pController->getOwner() != nullptr )
                listInside.push_back( pController );
        } );
        for ( size_t recordIndex = _listOccupant.size(); recordIndex > 0; --recordIndex )
        {
            const OccupantRecord& record = _listOccupant[recordIndex - 1];
            bool                  bStill = false;
            for ( const CharacterControllerComponent* pController : listInside )
            {
                bStill = bStill || pController->getOwner()->getHandle() == record._object;
            }
            if ( bStill )
                continue;
            restoreVisual( manager, record );
            _listOccupant.erase( _listOccupant.begin() + static_cast<ptrdiff_t>( recordIndex - 1 ) );
        }

        for ( CharacterControllerComponent* pController : listInside )
        {
            GameObject&     object  = *pController->getOwner();
            OccupantRecord* pRecord = nullptr;
            for ( OccupantRecord& record : _listOccupant )
            {
                if ( record._object == object.getHandle() )
                    pRecord = &record;
            }
            if ( pRecord == nullptr )
            {
                OccupantRecord created;
                created._object         = object.getHandle();
                MeshComponent* pMesh    = object.getComponent<MeshComponent>(); // 첫 메시가 몸이다 — 부품은 그 아래에 붙어 함께 내려간다
                created._visual         = TComponentHandle<MeshComponent>{ pMesh };
                created._visualPosition = pMesh != nullptr ? pMesh->getLocalPosition() : float3{};
                created._bWasGrounded   = pController->isGrounded() ? SW_TRUE : SW_FALSE;
                _listOccupant.push_back( created );
                pRecord = &_listOccupant.back();
            }
            // 탈출: 디딘 상태에서 떠오르는 순간(점프)마다 깊이를 돌려준다. 삼켜진 뒤에는 돌아오지 않는다.
            const bool bGrounded   = pController->isGrounded();
            const bool bJumped     = pRecord->_bWasGrounded == SW_TRUE && bGrounded == false && pController->getVelocity()._y > 0.0f;
            pRecord->_bWasGrounded = bGrounded ? SW_TRUE : SW_FALSE;
            if ( pRecord->_phase == SinkingSandPhase::Sinking )
            {
                if ( bJumped )
                {
                    pRecord->_depth = MathUtil::max( 0.0f, pRecord->_depth - _escapePerJump );
                    ++pRecord->_escapeCount;
                }
                if ( bGrounded )
                    pRecord->_depth = MathUtil::min( _maxDepth, pRecord->_depth + _sinkSpeed * seconds );
                pRecord->_secondsAtBottom = pRecord->_depth >= _maxDepth ? pRecord->_secondsAtBottom + seconds : 0.0f;
                if ( pRecord->_secondsAtBottom >= _engulfSeconds )
                    pRecord->_phase = SinkingSandPhase::Engulfed;
            }
            // 같은 깊이가 이동과 표현을 함께 정한다.
            pController->applyMoveScale( pRecord->_phase == SinkingSandPhase::Engulfed ? 0.0f : computeMoveScale( pRecord->_depth ) );
            MeshComponent* pMesh = manager.resolveComponent( pRecord->_visual );
            if ( pMesh != nullptr )
                pMesh->setLocalPosition( pRecord->_visualPosition - float3{ 0.0f, pRecord->_depth, 0.0f } );
        }
    }

    void SinkingSandZoneComponent::restoreVisual( GameObjectManager& manager, const OccupantRecord& record )
    {
        MeshComponent* pMesh = manager.resolveComponent( record._visual );
        if ( pMesh != nullptr )
            pMesh->setLocalPosition( record._visualPosition );
    }

    float32 SinkingSandZoneComponent::computeMoveScale( float32 depth ) const
    {
        const float32 ratio = MathUtil::saturate( depth / _maxDepth );
        return MathUtil::lerp( 1.0f, _moveScaleAtMaxDepth, ratio );
    }

    const SinkingSandZoneComponent::OccupantRecord* SinkingSandZoneComponent::findOccupant( GameObjectHandle object ) const
    {
        for ( const OccupantRecord& record : _listOccupant )
        {
            if ( record._object == object )
                return &record;
        }
        return nullptr;
    }

    const SinkingSandZoneComponent::OccupantRecord* SinkingSandZoneComponent::findInScene( const GameObjectManager& manager, GameObjectHandle object )
    {
        const OccupantRecord* pFound = nullptr;
        // 구역이 겹치면 캐릭터는 둘 다의 점유자일 수 있다 — 상태를 낸 마지막 구역의 것이다.
        manager.forEachComponentOfType<SinkingSandZoneComponent>( [&pFound, object]( const SinkingSandZoneComponent* pZone )
        {
            const OccupantRecord* pRecord = pZone->findOccupant( object );
            pFound                        = pRecord != nullptr ? pRecord : pFound;
        } );
        return pFound;
    }
} // namespace sw
