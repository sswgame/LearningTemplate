#include "pch.h"

#include "Engine/Animation/Component/PoseAfterimageComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( afterimageVisible, "Afterimage.Visible", "Afterimages the first pose afterimage component shows now", PoseAfterimageComponent, &PoseAfterimageComponent::countVisibleGhosts );
    SW_AUTOMATION_GETTER_WATCH( afterimageCaptureCount, "Afterimage.CaptureCount", "Afterimages the first pose afterimage component has left so far", PoseAfterimageComponent,
                                &PoseAfterimageComponent::getCaptureCount );
} // namespace sw

namespace sw
{
    PoseAfterimageComponent::PoseAfterimageComponent()
        : _materialPath{}
        , _opacityOverLife{}
        , _color{ 1.0f, 1.0f, 1.0f, 0.5f }
        , _capturePeriod{ 0.1f }
        , _lifetime{ 0.5f }
        , _maxGhosts{ 6 }
        , _bCaptureOnBeginPlay{ SW_FALSE }
        , _bCapturing{ SW_FALSE }
        , _reserved{ 0 }
        , _listGhost{}
        , _sinceCapture{ 0.0f }
        , _captureCount{ 0 }
    {
        setCanEverTick( true );
    }

    void PoseAfterimageComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate ); // 이번 프레임의 포즈가 나온 뒤에 고정한다
        if ( _opacityOverLife._listKey.empty() )
            SW_LOG_ERROR_ID( "PoseAfterimage.NoOpacityCurve", "Pose afterimage on '%#' has no opacity keys - afterimages stay invisible", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        // 사본 풀은 시작할 때 꺼 둔 채 만든다 — 틱 안에서 오브젝트를 만들지 않는다.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const uint32       count    = MathUtil::min( MathUtil::max( _maxGhosts, 1u ), kMaxGhostLimit );
        _listGhost.assign( count, Ghost{} );
        for ( uint32 ghostIndex = 0; ghostIndex < count && pManager != nullptr; ++ghostIndex )
        {
            fixed_string<constant::buffer::kMax128> ghostName;
            formatstring( ghostName.data(), ghostName.capacity(), "%#_Afterimage%#", pOwner->getName().c_str(), ghostIndex );
            GameObject* pGhost = pManager->createGameObject( hashed_string( ghostName.c_str() ) );
            if ( pGhost == nullptr )
                continue;
            SkeletalMeshComponent* pMesh = pGhost->addComponent<SkeletalMeshComponent>();
            if ( pMesh != nullptr )
                pMesh->setMaterialPath( _materialPath );
            pGhost->setActive( false );
            _listGhost[ghostIndex]._object = pGhost->getHandle();
        }
        if ( _bCaptureOnBeginPlay == SW_TRUE )
            startCapturing();
    }

    void PoseAfterimageComponent::onEndPlay()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( const Ghost& ghost : _listGhost )
        {
            GameObject* pGhost = pManager != nullptr ? pManager->resolveGameObject( ghost._object ) : nullptr;
            if ( pGhost != nullptr )
                pManager->destroyGameObject( pGhost );
        }
        _listGhost.clear();
        _bCapturing = SW_FALSE;
        Component::onEndPlay();
    }

    void PoseAfterimageComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const float32 step = MathUtil::max( context.getDeltaTime(), 0.0f );
        for ( Ghost& ghost : _listGhost )
        {
            if ( ghost._bVisible == false )
                continue;
            ghost._age += step;
            GameObject* pGhost = pManager->resolveGameObject( ghost._object );
            if ( pGhost == nullptr )
                continue;
            if ( ghost._age >= _lifetime )
            {
                ghost._bVisible = false;
                pGhost->setActive( false );
                continue;
            }
            SkeletalMeshComponent* pMesh = pGhost->getComponent<SkeletalMeshComponent>();
            if ( pMesh != nullptr )
            {
                const float32 opacity = computeOpacity( _opacityOverLife, ghost._age / _lifetime, _color._w );
                pMesh->setCustomData( 0, float4{ _color._x, _color._y, _color._z, opacity } );
            }
        }
        if ( _bCapturing == SW_FALSE )
            return;
        _sinceCapture += step;
        if ( _sinceCapture >= _capturePeriod )
        {
            _sinceCapture = 0.0f;
            capture();
        }
    }

    void PoseAfterimageComponent::startCapturing()
    {
        if ( _bCapturing == SW_TRUE )
            return;
        _bCapturing   = SW_TRUE;
        _sinceCapture = 0.0f;
        capture();
    }

    void PoseAfterimageComponent::stopCapturing()
    {
        _bCapturing = SW_FALSE;
    }

    uint32 PoseAfterimageComponent::countVisibleGhosts() const
    {
        uint32 visibleCount = 0;
        for ( const Ghost& ghost : _listGhost )
        {
            visibleCount += ghost._bVisible ? 1u : 0u;
        }
        return visibleCount;
    }

    float32 PoseAfterimageComponent::computeOpacity( const FloatCurve& opacityOverLife, float32 ageRatio, float32 baseAlpha )
    {
        const optional<float32> curve = opacityOverLife.evaluate( MathUtil::saturate( ageRatio ) );
        return MathUtil::saturate( curve.value_or( 0.0f ) * baseAlpha );
    }

    SkeletalMeshComponent* PoseAfterimageComponent::findSourceMesh() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
    }

    void PoseAfterimageComponent::capture()
    {
        const SkeletalMeshComponent* pSource = findSourceMesh();
        if ( pSource == nullptr || _listGhost.empty() || pSource->getSkinSourceMesh() == nullptr )
            return; // 외형이 아직 메시를 입히지 않았다 — 다음 간격에 다시 본다
        // 빈 사본, 없으면 가장 오래된 사본을 다시 쓴다(고정 용량).
        Ghost* pPick = &_listGhost[0];
        for ( Ghost& ghost : _listGhost )
        {
            if ( ghost._bVisible == false )
            {
                pPick = &ghost;
                break;
            }
            if ( ghost._age > pPick->_age )
                pPick = &ghost;
        }
        SkeletalMeshComponent* pGhostMesh = findGhostMesh( *pPick );
        if ( pGhostMesh == nullptr )
            return;
        if ( pGhostMesh->getSkinSourceMesh() != pSource->getSkinSourceMesh() )
        {
            pGhostMesh->setSkeleton( pSource->getSharedSkeleton() );
            pGhostMesh->setSkinSourceMesh( pSource->getSkinSourceMesh() );
        }
        // 포즈는 고정이다 — 사본에는 애니메이터가 없어 다음 프레임에도 이 포즈다.
        if ( pGhostMesh->applyRewindPose( pSource->getLocalPose() ) == false )
            return;
        pGhostMesh->setWorldTransform( pSource->getWorldMatrix() );
        pGhostMesh->setCustomData( 0, float4{ _color._x, _color._y, _color._z, computeOpacity( _opacityOverLife, 0.0f, _color._w ) } );
        pGhostMesh->getOwner()->setActive( true );
        pPick->_age      = 0.0f;
        pPick->_bVisible = true;
        ++_captureCount;
    }

    SkeletalMeshComponent* PoseAfterimageComponent::findGhostMesh( Ghost& ghost )
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*              pGhost   = pManager != nullptr ? pManager->resolveGameObject( ghost._object ) : nullptr;
        return pGhost != nullptr ? pGhost->getComponent<SkeletalMeshComponent>() : nullptr;
    }
} // namespace sw
