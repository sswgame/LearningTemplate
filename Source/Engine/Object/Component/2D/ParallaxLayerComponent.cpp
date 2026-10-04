#include "pch.h"

#include "Engine/Object/Component/2D/ParallaxLayerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct ParallaxLayerComponentInternal
        {
            /**
             * @brief 한 축의 레이어 자리입니다. 카메라에 대한 상대 자리 `(O − R) − (C − R)·s` 에서 흐르는 몫 `(C − R)·s` 를 되풀이 길이로 감습니다.
             * @details 감은 값은 [−M/2, M/2) 라 레이어가 카메라에서 반 칸 넘게 벗어나지 않습니다. 내용이 M 마다 같으므로 M 만큼 튀는 것은 보이지 않습니다.
             */
            static float32 computeAxis( float32 origin, float32 camera, float32 reference, float32 scroll, float32 repeatSize )
            {
                float32 flow = ( camera - reference ) * scroll;
                if ( repeatSize > 0.0f )
                    flow -= repeatSize * MathUtil::floor( flow / repeatSize + 0.5f );
                return camera + ( origin - reference ) - flow;
            }
        };
    } // namespace

    ParallaxLayerComponent::ParallaxLayerComponent()
        : _scrollFactor{ 0.5f, 0.5f }
        , _repeatSize{ 0.0f, 0.0f }
        , _referencePoint{ 0.0f, 0.0f }
        , _origin{ 0.0f, 0.0f, 0.0f }
        , _bHasOrigin{ SW_FALSE }
    {
    }

    void ParallaxLayerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 카메라를 옮기는 컴포넌트(따라가기 · 픽셀 퍼펙트)가 앞 그룹에서 돈 뒤에 그 프레임의 카메라를 본다.
        setTickGroup( TickGroup::PostUpdate );
        (void)captureOrigin(); // 자리를 잡을 장면 컴포넌트가 없으면 틱이 다시 묻는다
    }

    void ParallaxLayerComponent::onEndPlay()
    {
        GameObject*     pOwner = getOwner();
        SceneComponent* pRoot  = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pRoot != nullptr && _bHasOrigin == SW_TRUE )
            pRoot->setLocalPosition( _origin );
        _bHasOrigin = SW_FALSE;
        Component::onEndPlay();
    }

    void ParallaxLayerComponent::onTick( float32 /*deltaTime*/ )
    {
        GameObject*              pOwner   = getOwner();
        const GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 씬의 게임 카메라를 고르는 규칙과 같다(`Scene::ensureDefaultCameras`).
        const CameraComponent* pCamera = pManager->getCameraRegistry().selectCamera( CameraRole::Game );
        if ( pCamera == nullptr )
            return;
        const float3 cameraPosition = pCamera->getCameraPosition();
        applyCamera( float2{ cameraPosition._x, cameraPosition._y } );
    }

    float2 ParallaxLayerComponent::computeLayerPosition( const float2& origin, const float2& camera, const float2& reference, const float2& scroll,
                                                         const float2& repeatSize )
    {
        using Internal = ParallaxLayerComponentInternal;
        return float2{ Internal::computeAxis( origin._x, camera._x, reference._x, scroll._x, repeatSize._x ),
                       Internal::computeAxis( origin._y, camera._y, reference._y, scroll._y, repeatSize._y ) };
    }

    void ParallaxLayerComponent::applyCamera( const float2& camera )
    {
        if ( captureOrigin() == false )
            return;
        SceneComponent* pRoot    = getOwner()->getPrimarySceneComponent();
        const float2    position = computeLayerPosition( float2{ _origin._x, _origin._y }, camera, _referencePoint, _scrollFactor, _repeatSize );
        pRoot->setLocalPosition( float3{ position._x, position._y, _origin._z } );
    }

    bool ParallaxLayerComponent::captureOrigin()
    {
        if ( _bHasOrigin == SW_TRUE )
            return true;
        GameObject*           pOwner = getOwner();
        const SceneComponent* pRoot  = ( pOwner != nullptr ) ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr )
            return false;
        _origin     = pRoot->getLocalPosition();
        _bHasOrigin = SW_TRUE;
        return true;
    }
} // namespace sw
