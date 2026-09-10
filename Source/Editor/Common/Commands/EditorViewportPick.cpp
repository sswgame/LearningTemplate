/**
 * @file EditorViewportPick.cpp
 * @brief 컴포넌트 종류별 피킹 경계 표와 일반 SceneComponent 폴백
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorViewportPick.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportPickInternal
        {
            /** @brief 구 하나를 후보로 넣습니다. 더 가까우면 ioBest를 갱신합니다. */
            static void considerSphere( GameObject* pObj, Component* pComp, const float3& center, float32 radius,
                                        const EditorPickRay& ray, EditorPickResult& ioBest )
            {
                float32 hitT{ 0.0f };
                if ( EditorViewportPick::rayHitsSphere( ray._origin, ray._direction, center, radius, hitT ) == false )
                    return;
                if ( hitT >= ioBest._distance )
                    return;

                ioBest._distance   = hitT;
                ioBest._pObject    = pObj;
                ioBest._pComponent = pComp;
            }

            // ------------------------------------------------------------------------------
            // 종류를 아는 제공자 — 각자 고유한 경계 계산을 안다
            // ------------------------------------------------------------------------------
            static void considerMesh( GameObject* pObj, const EditorPickRay& ray, EditorPickResult& ioBest )
            {
                MeshComponent* pMesh = pObj->getComponent<MeshComponent>();
                if ( pMesh == nullptr || pMesh->isActive() == false || pMesh->isVisible() == false )
                    return;

                const float3  scale    = pMesh->getLocalScale();
                const float32 absX     = MathUtil::abs( scale._x );
                const float32 absY     = MathUtil::abs( scale._y );
                const float32 absZ     = MathUtil::abs( scale._z );
                const float32 maxScale = MathUtil::max( absX, MathUtil::max( absY, absZ ) );
                const float32 radius   = pMesh->getBoundsRadius() * MathUtil::max( maxScale, 0.001f );

                considerSphere( pObj, pMesh, pMesh->getWorldPosition(), radius, ray, ioBest );
            }

            static void considerSprite( GameObject* pObj, const EditorPickRay& ray, EditorPickResult& ioBest )
            {
                SpriteComponent* pSprite = pObj->getComponent<SpriteComponent>();
                if ( pSprite == nullptr || pSprite->isActive() == false )
                    return;

                const float3  scale  = pSprite->getLocalScale();
                const float32 absX   = MathUtil::abs( scale._x );
                const float32 absY   = MathUtil::abs( scale._y );
                const float32 radius = MathUtil::max( absX, absY ) * 0.7f + 0.1f;

                considerSphere( pObj, pSprite, pSprite->getWorldPosition(), radius, ray, ioBest );
            }

            static void considerBoxCollider2D( GameObject* pObj, const EditorPickRay& ray, EditorPickResult& ioBest )
            {
                BoxCollider2DComponent* pBox = pObj->getComponent<BoxCollider2DComponent>();
                if ( pBox == nullptr || pBox->isActive() == false )
                    return;

                const float2  offsetPos = pBox->getOffsetPosition();
                const float2  offsetScl = pBox->getOffsetScale();
                const float3  center    = pBox->getWorldPosition() + float3{ offsetPos._x, offsetPos._y, 0.0f };
                const float32 radius    = offsetScl.getLength() * 0.5f + 0.1f;

                considerSphere( pObj, pBox, center, radius, ray, ioBest );
            }

            /**
             * @brief 오브젝트의 **모든** SceneComponent를 기본 반지름으로 후보에 넣습니다.
             * @details 표가 종류를 모르는 컴포넌트 — 즉 게임이 만든 컴포넌트 — 를 집을 수 있게 하는
             *          유일한 경로입니다. 예전 `considerScenePick` 은 주 컴포넌트 하나만 봤습니다.
             *          RTTI가 꺼져 있어 `dynamic_cast`를 쓸 수 없으므로 리플렉션 `castTo`로 판별합니다.
             */
            static void considerSceneComponents( GameObject* pObj, const EditorPickRay& ray, EditorPickResult& ioBest )
            {
                for ( Component* pComp : pObj->getComponents() )
                {
                    SceneComponent* pScene = castTo<SceneComponent>( pComp );
                    if ( pScene == nullptr || pScene->isActive() == false )
                        continue;

                    considerSphere( pObj, pScene, pScene->getWorldPosition(), EditorViewportPick::kFallbackRadius,
                                    ray, ioBest );
                }
            }

            /** @brief 이 종류의 후보를 넣는 함수 */
            using PickConsiderFunc = void ( * )( GameObject* pObj, const EditorPickRay& ray, EditorPickResult& ioBest );

            /**
             * @brief 피킹 제공자 한 줄.
             * @details `_order3D`/`_order2D` 는 **같은 거리일 때 어느 종류가 이기는지**를 정합니다
             *          (낮은 값이 먼저 보이고, 동거리에서는 먼저 본 쪽이 남습니다). 2D 모드에서
             *          스프라이트가 메시보다 앞서는 것이 규약이라 두 순서를 따로 둡니다.
             */
            struct PickProviderRow
            {
                const utf8*      _pName; ///< 진단용 이름
                uint8            _order3D;
                uint8            _order2D;
                PickConsiderFunc _pConsider;
            };

            /** @brief 종류를 아는 제공자 표 — 새 종류는 여기 한 줄이다. */
            inline static const PickProviderRow _s_arrProvider[] = {
                {         "MeshComponent", 0, 2,          &considerMesh},
                {       "SpriteComponent", 1, 0,        &considerSprite},
                {"BoxCollider2DComponent", 2, 1, &considerBoxCollider2D}
            };

            static constexpr uint32 kProviderCount = static_cast<uint32>( sizeof( _s_arrProvider ) / sizeof( _s_arrProvider[0] ) );

            /** @brief 활성 모드에서 이 줄의 순서입니다. */
            static uint8 orderOf( const PickProviderRow& row, bool b2DMode )
            {
                return b2DMode ? row._order2D : row._order3D;
            }

            /** @brief 오브젝트 하나에 대해 전용 제공자를 순서대로 보고, 못 잡았으면 일반 경로로 내려갑니다. */
            static void considerObject( GameObject* pObj, const EditorPickRay& ray, bool b2DMode,
                                        EditorPickResult& ioBest )
            {
                if ( pObj == nullptr || pObj->isActive() == false )
                    return;

                const float32 beforeDistance = ioBest._distance;

                for ( uint8 pass = 0; pass < static_cast<uint8>( kProviderCount ); ++pass )
                {
                    for ( const PickProviderRow& row : _s_arrProvider )
                    {
                        if ( orderOf( row, b2DMode ) == pass )
                            row._pConsider( pObj, ray, ioBest );
                    }
                }

                // 전용 제공자가 이 오브젝트에서 아무것도 못 잡았을 때만 일반 경로를 쓴다. 그러지 않으면
                // 기본 반지름 구가 전용 경계보다 커서 더 가까운 t 를 내는 경우에 선택 컴포넌트가 뒤바뀐다.
                if ( beforeDistance <= ioBest._distance )
                    considerSceneComponents( pObj, ray, ioBest );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorViewportPick::pick( const GameObjectManager* pManager, const EditorPickRay& ray, bool b2DMode,
                                   EditorPickResult& outResult )
    {
        outResult = EditorPickResult{};
        if ( pManager == nullptr )
            return false;

        EditorPickResult best{};
        best._distance = MathUtil::MaxFloat;

        pManager->forEachGameObject( [&]( GameObject* pObj )
        {
            EditorViewportPickInternal::considerObject( pObj, ray, b2DMode, best );
        } );

        if ( best._pObject == nullptr )
            return false;

        outResult = best;
        return true;
    }

    bool EditorViewportPick::rayHitsSphere( const float3& origin, const float3& dir, const float3& center,
                                            float32 radius, float32& outHitT )
    {
        const float3  toCenter = origin - center;
        const float32 b        = toCenter.dot( dir );
        const float32 c        = toCenter.dot( toCenter ) - radius * radius;
        const bool    bAway    = ( c > 0.0f && b > 0.0f );
        if ( bAway )
            return false;

        const float32 discr = b * b - c;
        if ( discr < 0.0f )
            return false;

        const float32 sqrtDiscr = MathUtil::sqrt( discr );
        float32       hitT      = -b - sqrtDiscr;
        if ( hitT < 0.0f )
            hitT = -b + sqrtDiscr;
        if ( hitT < 0.0f )
            return false;

        outHitT = hitT;
        return true;
    }

    uint32 EditorViewportPick::getTypedProviderCount()
    {
        return EditorViewportPickInternal::kProviderCount;
    }
} // namespace sw::editor
