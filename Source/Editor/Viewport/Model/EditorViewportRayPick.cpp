/**
 * @file EditorViewportRayPick.cpp
 * @brief 뷰포트 피킹 — 컴포넌트가 선언한 월드 경계(구로 거르고 상자 · 메시 삼각형으로 좁힌다) · 종류별 순서 테이블 · 경계 없는 컴포넌트의 기본 반지름
 */
#include "pch.h"

#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationCrowd.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshVertex.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Physics/Component/BoxCollider2DComponent.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Visual/2D/SpriteComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportRayPickInternal
        {
            /** @brief NDC 점을 월드 좌표로 옮깁니다. w 가 0 이면(퇴화 행렬) false 입니다. */
            static bool unproject( const float4x4& invViewProj, float32 ndcX, float32 ndcY, float32 ndcZ, float3& outWorld )
            {
                const float4 clip{ ndcX, ndcY, ndcZ, 1.0f };
                const float4 world = float4::transform( clip, invViewProj );
                if ( MathUtil::abs( world._w ) < MathUtil::kEpsilon )
                    return false;
                outWorld = float3{ world._x / world._w, world._y / world._w, world._z / world._w };
                return true;
            }

            /** @brief float3 의 축 성분입니다(0 · 1 · 2 = X · Y · Z). */
            static float32& getAxisOf( float3& v, uint32 axisIndex )
            {
                return axisIndex == 0 ? v._x : ( axisIndex == 1 ? v._y : v._z );
            }

            /** @brief 테이블 줄이 정하는 가장 좁은 판정 모양입니다. 구는 언제나 먼저 거르는 단계이고 답이 되지 않습니다. */
            enum class PickShape : uint8
            {
                Box,       ///< 선언한 월드 상자(`SceneComponent::getWorldBox`)까지 — 얇은 2D 경계는 여유를 더한 상자가 답이다
                Triangles, ///< 메시 공간 상자(OBB) 뒤에 CPU 삼각형까지 — 실루엣 밖은 빗나간다
            };

            /**
             * @brief 같은 거리일 때 어느 종류가 이기는지와 얼마나 좁혀 판정하는지 정하는 테이블의 한 줄입니다. **경계는 테이블이 아니라 컴포넌트가 선언합니다**(`SceneComponent::getWorldBounds` · `getWorldBox`).
             * @details 테이블은 순서 · 편집 여유 · 판정 모양만 정한다 — 경계를 테이블이 계산하면(로컬 스케일) 부모가 키운 물체는 클릭이 빗나가고, 게임이
             *          만든 컴포넌트는 경계가 있어도 기본 반지름으로만 집힌다.
             *          `_order3D`/`_order2D` 는 낮은 값이 먼저 보이고 같은 거리에서는 먼저 본 쪽이 남는다(2D 모드에서는 스프라이트가 메시보다 앞선다).
             *          `_pickPadding` 은 얇은 2D 경계를 집기 쉽게 더하는 여유다 — 편집 편의라 엔진 경계에는 넣지 않는다.
             *          테이블에 없는 종류는 선언한 상자로 테이블 다음 순서에 집힌다.
             */
            struct PickOrderRow
            {
                const utf8* _pName; ///< 진단용 이름
                uint8       _order3D;
                uint8       _order2D;
                float32     _pickPadding;
                PickShape   _shape;
                const TypeInfo* ( *_pStaticType )();
            };

            /** @brief 종류를 아는 순서 표입니다. 새 종류는 경계만 선언하면 집히고, 순서가 따로 필요할 때만 여기에 한 줄을 더합니다. */
            inline static const PickOrderRow _s_arrOrder[] = {
                {         "MeshComponent", 0, 2, 0.0f, PickShape::Triangles,          &MeshComponent::StaticType},
                {       "SpriteComponent", 1, 0, 0.1f,       PickShape::Box,        &SpriteComponent::StaticType},
                {"BoxCollider2DComponent", 2, 1, 0.1f,       PickShape::Box, &BoxCollider2DComponent::StaticType}
            };

            static constexpr uint32 kOrderRowCount = static_cast<uint32>( sizeof( _s_arrOrder ) / sizeof( _s_arrOrder[0] ) );

            /** @brief 이보다 짧은 로컬 걸음은 퇴화한 월드 행렬(스케일 0)로 보고 판정하지 않습니다. */
            static constexpr float32 kMinLocalStep = 1.0e-12f;
            /** @brief 메시가 없을 때의 메시 공간 반 크기입니다 — `MeshComponent::getWorldBox` 의 단위 상자와 같습니다. */
            static constexpr float32 kUnitBoxHalf = 0.5f;

            /** @brief 컴포넌트에 맞는 테이블 줄입니다. 타입 사슬을 파생에서 기반으로 오르며 가장 가까운 줄을 고릅니다(스프라이트는 메시이기도 하다). */
            static const PickOrderRow* findOrderRow( const Component* pComp )
            {
                for ( const TypeInfo* pType = pComp->getTypeInfo(); pType != nullptr; pType = pType->getParentType() )
                {
                    for ( const PickOrderRow& row : _s_arrOrder )
                    {
                        if ( row._pStaticType() == pType )
                            return &row;
                    }
                }
                return nullptr;
            }

            /** @brief 이 컴포넌트를 피킹 후보로 볼 수 있는지(활성 · 메시면 보이는 것)입니다. */
            static bool isPickable( const SceneComponent* pScene )
            {
                if ( pScene->isActive() == false )
                    return false;
                const MeshComponent* pMesh = castTo<MeshComponent>( pScene );
                return pMesh == nullptr || pMesh->isVisible();
            }

            /**
             * @brief 화면에 그려진 자세의 메시 공간 정점 위치를 채웁니다 — GPU 가 그리는 것과 같은 식이다.
             * @details 정점 애니메이션(VAT)이 걸린 메시는 군중 시계 + 인스턴스 시각 오프셋의 프레임(`MeshVertexAnimation::samplePosition`, 셰이더와 같은 보간),
             *          스킨 유닛은 모프 가중치([0, 1] 로 묶는다)를 레스트에 더한 뒤 스킨 팔레트로 스키닝한다(`Mesh::skinVertex` — 듀얼 쿼터니언 방식은 선형 근사).
             *          둘 다 아니면 레스트 정점 그대로다. 언리얼 스켈레탈 메시의 라인 트레이스(스키닝된 정점)와 같은 자리다.
             * @return VAT 표의 정점 수가 메시와 다르면(쿠킹 결함) 오류를 남기고 false 입니다.
             */
            static bool collectPosedPositions( const MeshComponent& component, const Mesh& mesh, float64 vertexAnimationClock, vector<float3>& outListPosition )
            {
                const vector<RHIVertex>& listRest = mesh.getVertices();
                outListPosition.resize( listRest.size() );
                const MeshVertexAnimation* pAnimation = mesh.findVertexAnimation();
                if ( pAnimation != nullptr && pAnimation->isEmpty() == false )
                {
                    if ( pAnimation->_vertexCount != static_cast<uint32>( listRest.size() ) )
                    {
                        SW_LOG_ERROR_ID( "EditorViewportRayPick.VertexAnimationMismatch", "Vertex animation has %# vertices but the mesh has %# - not picked", pAnimation->_vertexCount,
                                         listRest.size() );
                        return false;
                    }
                    const float32 time = static_cast<float32>( vertexAnimationClock ) + component.getVertexAnimationPhase(); // 스냅샷 `_vertexAnimationTime` 과 같은 시계
                    for ( uint32 vertex = 0; vertex < pAnimation->_vertexCount; ++vertex )
                    {
                        outListPosition[vertex] = pAnimation->samplePosition( time, vertex );
                    }
                    return true;
                }
                const SkeletalMeshComponent* pUnit = castTo<SkeletalMeshComponent>( &component );
                if ( pUnit == nullptr || mesh.hasSkin() == false )
                {
                    for ( size_t vertex = 0; vertex < listRest.size(); ++vertex )
                    {
                        const float32* pPosition = listRest[vertex]._arrPosition;
                        outListPosition[vertex]  = float3{ pPosition[0], pPosition[1], pPosition[2] };
                    }
                    return true;
                }
                vector<RHIVertex>              listPosed       = listRest;
                const vector<float32>&         listWeight      = pUnit->getMorphWeights();
                const vector<MeshMorphTarget>& listMorphTarget = mesh.getMorphTargets();
                const size_t                   morphCount      = MathUtil::min( listWeight.size(), listMorphTarget.size() );
                for ( size_t target = 0; target < morphCount; ++target )
                {
                    const float32 weight = MathUtil::saturate( listWeight[target] );
                    if ( weight <= 0.0f )
                        continue;
                    for ( const MeshMorphDelta& delta : listMorphTarget[target]._listDelta )
                    {
                        if ( delta._vertexIndex >= listPosed.size() )
                            continue;
                        float32* pPosition = listPosed[delta._vertexIndex]._arrPosition;
                        pPosition[0] += delta._position._x * weight;
                        pPosition[1] += delta._position._y * weight;
                        pPosition[2] += delta._position._z * weight;
                    }
                }
                const vector<float4x4>&       listPalette = pUnit->getSkinPalette();
                const vector<MeshSkinVertex>& listSkin    = mesh.getSkinVertices();
                for ( size_t vertex = 0; vertex < listPosed.size(); ++vertex )
                {
                    if ( listPalette.empty() == false )
                        MeshSkinBinding::skinVertex( listPalette, listSkin[vertex], listPosed[vertex] );
                    const float32* pPosition = listPosed[vertex]._arrPosition;
                    outListPosition[vertex]  = float3{ pPosition[0], pPosition[1], pPosition[2] };
                }
                return true;
            }

            /**
             * @brief 메시 하나를 메시 공간에서 판정합니다: 메시 상자(월드에서는 회전한 OBB) → 화면과 같은 자세의 삼각형. 맞으면 월드 거리를 씁니다.
             * @details 레이를 월드 역행렬로 옮겨 판정하고 로컬 거리를 로컬 걸음 길이로 나눠 월드 거리로 돌린다(비균등 스케일도 맞다).
             *          상자는 자세를 담는 것이다 — 정적 메시는 메시 경계, VAT 는 모든 프레임의 경계(`MeshVertexAnimation::_boundsCenter`)이고, 스킨 유닛은 레스트 상자가 자세를 담지 못해 상자 단계를 건너뛴다.
             */
            static bool hitTestMeshTriangles( const MeshComponent& mesh, const Ray& ray, float32 maxDistance, float64 vertexAnimationClock, float32& outDistance )
            {
                const float4x4 inverseWorld = mesh.getWorldMatrix().invert();
                const float3   localStep    = float3::transformVector( ray._direction, inverseWorld ); // 월드 1 m 가 메시 공간에서 이만큼이다
                const float32  stepLength   = localStep.getLength();
                if ( stepLength < kMinLocalStep )
                    return false;
                Ray localRay{};
                localRay._origin       = float3::transform( ray._origin, inverseWorld );
                localRay._direction    = localStep * ( 1.0f / stepLength );
                const float32 localMax = maxDistance * stepLength;

                const Mesh* pMesh    = mesh.getMesh().get();
                const bool  bHasMesh = pMesh != nullptr && pMesh->getVertexCount() > 0 && pMesh->getIndexCount() > 0;
                if ( bHasMesh == false )
                {
                    // 메시를 읽는 중이면 그리는 것이 없다 — 상자는 `MeshComponent::getWorldBox` 의 단위 상자다(선택 · 프레이밍이 같은 크기를 본다).
                    float32      boxLocalT = 0.0f;
                    const float3 unitMin{ -kUnitBoxHalf, -kUnitBoxHalf, -kUnitBoxHalf };
                    const float3 unitMax{ kUnitBoxHalf, kUnitBoxHalf, kUnitBoxHalf };
                    if ( Ray::intersectAABB( localRay, unitMin, unitMax, localMax, boxLocalT ) == false )
                        return false;
                    outDistance = boxLocalT / stepLength;
                    return true;
                }
                const MeshVertexAnimation*   pAnimation = pMesh->findVertexAnimation();
                const bool                   bAnimated  = pAnimation != nullptr && pAnimation->isEmpty() == false;
                const SkeletalMeshComponent* pUnit      = castTo<SkeletalMeshComponent>( &mesh );
                const bool                   bSkinned   = bAnimated == false && pUnit != nullptr && pMesh->hasSkin();
                if ( bSkinned == false )
                {
                    const float3 boxMin    = bAnimated ? pAnimation->_boundsCenter - pAnimation->_boundsHalfExtent : pMesh->getLocalBoundsMin();
                    const float3 boxMax    = bAnimated ? pAnimation->_boundsCenter + pAnimation->_boundsHalfExtent : pMesh->getLocalBoundsMax();
                    float32      boxLocalT = 0.0f;
                    if ( Ray::intersectAABB( localRay, boxMin, boxMax, localMax, boxLocalT ) == false )
                        return false;
                }

                vector<float3> listPosition;
                if ( collectPosedPositions( mesh, *pMesh, vertexAnimationClock, listPosition ) == false )
                    return false;
                const vector<uint32>& listIndex  = pMesh->getIndices();
                const uint32          indexCount = static_cast<uint32>( listIndex.size() );
                float32               bestLocal  = localMax;
                bool                  bHit       = false;
                for ( uint32 index = 0; index + 2 < indexCount; index += 3 )
                {
                    RayTriangleHit hit{};
                    if ( Ray::intersectTriangle( localRay, listPosition[listIndex[index]], listPosition[listIndex[index + 1]], listPosition[listIndex[index + 2]], bestLocal, hit ) )
                    {
                        bestLocal = hit._distance;
                        bHit      = true;
                    }
                }
                if ( bHit == false )
                    return false;
                outDistance = bestLocal / stepLength;
                return true;
            }

            /**
             * @brief 경계를 선언한 컴포넌트 하나: 구로 거르고, 줄이 정한 모양(상자 · 삼각형)으로 좁힙니다. @p maxDistance 와 같은 거리도 맞은 것으로 돌려줍니다(같은 거리의 순서는 부르는 쪽이 정한다).
             * @details 구의 진입 거리는 실제 표면보다 늘 가깝거나 같아서 그보다 먼 후보를 버리는 데만 쓴다 — 구가 답이면 길고 얇은 물체는 실루엣 밖에서도 집히고,
             *          큰 구가 안쪽의 작은 물체를 가린다(감사 E-10). Godot 4 의 에디터 피킹도 AABB 뒤에 메시 삼각형 레이다.
             */
            static bool hitTestDeclared( const SceneComponent& scene, const PickOrderRow* pRow, const float3& center, float32 radius, const Ray& ray, float32 maxDistance,
                                         float64 vertexAnimationClock, float32& outDistance )
            {
                const float32 padding = ( pRow != nullptr ) ? pRow->_pickPadding : 0.0f;
                float32       sphereT = 0.0f;
                if ( EditorViewportRayPick::rayHitsSphere( ray._origin, ray._direction, center, radius + padding, sphereT ) == false || sphereT > maxDistance )
                    return false;

                const PickShape shape = ( pRow != nullptr ) ? pRow->_shape : PickShape::Box;
                if ( shape == PickShape::Triangles )
                {
                    const MeshComponent* pMesh = castTo<MeshComponent>( &scene );
                    SW_ASSERT( pMesh != nullptr ); // 삼각형 줄은 메시 종류에만 둔다
                    return pMesh != nullptr && hitTestMeshTriangles( *pMesh, ray, maxDistance, vertexAnimationClock, outDistance );
                }

                AABB       box{};
                const bool bHasBox = scene.getWorldBox( box );
                SW_ASSERT( bHasBox ); // 경계 구를 선언한 컴포넌트는 상자도 선언한다(`SceneComponent::getWorldBox`)
                if ( bHasBox == false )
                    return false;
                const float3 pad{ padding, padding, padding };
                return Ray::intersectAABB( ray, box._min - pad, box._max + pad, maxDistance, outDistance );
            }

            /**
             * @brief 오브젝트 하나: 경계를 선언한 컴포넌트를 좁혀 판정하고, 하나도 맞지 않았으면 경계가 없는 씬 컴포넌트를 기본 반지름으로 봅니다.
             * @details 한 번 돌며 (거리, 순서) 가 가장 작은 컴포넌트를 고른다 — 같은 오브젝트 안에서 같은 거리면 순서가 낮은 쪽, 오브젝트끼리는 먼저 본 쪽이 남는다.
             *          기본 반지름은 경계가 없는 컴포넌트(빈 트랜스폼 · 빛 · 게임이 만든 컴포넌트)를 집게 하는 곳이다. 경계를 선언한 컴포넌트에는 쓰지 않는다 —
             *          쓰면 기둥 피벗 둘레의 빈 곳을 눌러도 기둥이 집힌다.
             */
            static void hitTestObject( GameObject* pObj, const Ray& ray, bool b2DMode, float64 vertexAnimationClock, EditorPickResult& ioBest )
            {
                if ( pObj == nullptr || pObj->isActive() == false )
                    return;

                Component* pBestComp     = nullptr;
                float32    bestDistance  = ioBest._distance;
                uint32     bestOrder     = kOrderRowCount;
                Component* pFallbackComp = nullptr;
                float32    fallbackT     = ioBest._distance;
                for ( Component* pComp : pObj->getComponents() )
                {
                    SceneComponent* pScene = castTo<SceneComponent>( pComp );
                    if ( pScene == nullptr || pScene->isActive() == false )
                        continue;
                    float3  center{};
                    float32 radius{ 0.0f };
                    float32 hitT{ 0.0f };
                    if ( pScene->getWorldBounds( center, radius ) == false )
                    {
                        const bool bCloser = EditorViewportRayPick::rayHitsSphere( ray._origin, ray._direction, pScene->getWorldPosition(), EditorViewportRayPick::kFallbackRadius,
                                                                                   hitT ) &&
                                             hitT < fallbackT;
                        if ( bCloser )
                        {
                            fallbackT     = hitT;
                            pFallbackComp = pScene;
                        }
                        continue;
                    }
                    if ( isPickable( pScene ) == false )
                        continue;
                    const PickOrderRow* pRow  = findOrderRow( pScene );
                    const uint32        order = ( pRow != nullptr ) ? ( b2DMode ? pRow->_order2D : pRow->_order3D ) : kOrderRowCount;
                    if ( hitTestDeclared( *pScene, pRow, center, radius, ray, bestDistance, vertexAnimationClock, hitT ) == false )
                        continue;
                    const bool bWins = hitT < bestDistance || ( pBestComp != nullptr && hitT == bestDistance && order < bestOrder );
                    if ( bWins )
                    {
                        bestDistance = hitT;
                        bestOrder    = order;
                        pBestComp    = pScene;
                    }
                }

                if ( pBestComp == nullptr )
                {
                    pBestComp    = pFallbackComp;
                    bestDistance = fallbackT;
                }
                if ( pBestComp == nullptr )
                    return;
                ioBest._distance   = bestDistance;
                ioBest._pObject    = pObj;
                ioBest._pComponent = pBestComp;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorViewportRayPick::pick( const GameObjectManager* pManager, const Ray& ray, bool b2DMode,
                                      EditorPickResult& outResult )
    {
        outResult = EditorPickResult{};
        if ( pManager == nullptr )
            return false;

        EditorPickResult best{};
        best._distance                     = MathUtil::kMaxFloat;
        const float64 vertexAnimationClock = pManager->getSubsystem<AnimationSystem>().getCrowd().getClock(); // VAT 가 그리는 프레임의 시계(`GPUSkinPaletteCollector::collect`)

        pManager->forEachGameObject( [&]( GameObject* pObj )
        {
            // 화면을 그리는 에디터 카메라는 레이가 출발하는 위치에 있어 기본 반지름 구가 늘 가장 가깝다 — 집지 않는다
            // (언리얼 · 유니티의 에디터 카메라는 씬 오브젝트가 아니다).
            const CameraComponent* pCamera = pObj != nullptr ? pObj->getComponent<CameraComponent>() : nullptr;
            if ( pCamera != nullptr && pCamera->getRole() == CameraRole::Viewport )
                return;
            EditorViewportRayPickInternal::hitTestObject( pObj, ray, b2DMode, vertexAnimationClock, best );
        } );

        if ( best._pObject == nullptr )
            return false;

        outResult = best;
        return true;
    }

    bool EditorViewportRayPick::rayHitsSphere( const float3& origin, const float3& dir, const float3& center,
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

    bool EditorViewportRayPick::makeRay( const float4x4& invViewProj, float32 u, float32 v, Ray& outRay )
    {
        const float32 ndcX = u * 2.0f - 1.0f;
        const float32 ndcY = 1.0f - v * 2.0f;

        float3 nearPt{};
        float3 farPt{};
        if ( EditorViewportRayPickInternal::unproject( invViewProj, ndcX, ndcY, 0.0f, nearPt ) == false )
            return false;
        if ( EditorViewportRayPickInternal::unproject( invViewProj, ndcX, ndcY, 1.0f, farPt ) == false )
            return false;

        const float3  dir    = farPt - nearPt;
        const float32 dirLen = dir.getLength();
        if ( dirLen < 1e-8f )
            return false;

        outRay._origin    = nearPt;
        outRay._direction = dir * ( 1.0f / dirLen );
        return true;
    }

    bool EditorViewportRayPick::rayHitsAxisPlane( const Ray& ray, uint32 axisIndex, float3& outPoint )
    {
        if ( axisIndex > 2 )
            return false;

        float3        direction = ray._direction;
        float3        origin    = ray._origin;
        const float32 dirAxis   = EditorViewportRayPickInternal::getAxisOf( direction, axisIndex );
        if ( MathUtil::abs( dirAxis ) <= 1e-4f )
            return false;

        const float32 t                                                 = -EditorViewportRayPickInternal::getAxisOf( origin, axisIndex ) / dirAxis;
        outPoint                                                        = origin + direction * t;
        EditorViewportRayPickInternal::getAxisOf( outPoint, axisIndex ) = 0.0f;
        return true;
    }

    uint32 EditorViewportRayPick::getTypedProviderCount()
    {
        return EditorViewportRayPickInternal::kOrderRowCount;
    }
} // namespace sw::editor
