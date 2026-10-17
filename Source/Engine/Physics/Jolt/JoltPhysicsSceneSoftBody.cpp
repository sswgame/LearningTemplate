/**
 * @file JoltPhysicsSceneSoftBody.cpp
 * @brief `JoltPhysicsScene` 의 소프트 바디(천 · 헤어 카드)입니다. `SoftBodySharedSettings` 에 정점 · 면을 넣고 늘어남 · 비틀림 · 굽힘 구속을 만듭니다.
 * @details 정점은 씬 스텝(`PhysicsSystem::Update`)이 강체와 함께 푼다. 고정 정점은 역질량 0 이라 구속이 움직이지 않고, `setSoftBodyPinnedTransform`
 *          이 위치를 직접 쓴다(Jolt 의 키네마틱 정점 방식). 몸 자리는 만든 때 그대로 둔다(`mUpdatePosition` 끔) — 정점이 질량 중심 기준이라
 *          몸이 정점을 따라 옮겨 다니면 고정 정점의 로컬 위치를 매번 다시 맞춰야 한다. 천이 멀리 가지 않는 쓰임(옷 · 깃발 · 커튼)이라 넓은 단계 상자가
 *          커지는 값은 작다.
 *          굽힘 구속은 거리형(`EBendType::Distance`, 공유 변 맞은편 두 정점 사이 거리)이다. 이면각형(`Dihedral`)은 쓰지 않는다 — 두 삼각형이 180° 로
 *          접혀 법선이 반대가 되면 각도 부호(`(n2 × n1) · e`)가 반올림 잡음으로 정해져 보정이 반복마다 뒤집히고, 바닥에 떨어져 접히는 천이
 *          수만 m 로 발산했다(Windows 에서 떨어뜨리는 자세 24 가지 중 4 가지, 리눅스 CI 는 기본 자세에서). 거리형은 접혀도 특이점이 없다.
 */
#include "pch.h"

#include "Engine/Physics/Jolt/JoltPhysicsScene.h"
#include "Engine/Physics/Jolt/JoltUtil.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

namespace sw
{
    SW_LOG_CALLER( "JoltPhysicsScene" );
} // namespace sw

namespace sw
{
    PhysicsSoftBodyHandle JoltPhysicsScene::createSoftBody( const PhysicsSoftBodyDesc& desc )
    {
        const size_t vertexCount      = desc._listVertex.size();
        const bool   bWholeTriangles  = desc._listTriangleIndex.empty() == false && ( desc._listTriangleIndex.size() % 3 ) == 0;
        const bool   bInvalidSoftBody = vertexCount == 0 || bWholeTriangles == false || desc._vertexMass <= 0.0f;
        if ( bInvalidSoftBody )
        {
            SW_LOG_ERROR( "Soft body needs vertices, whole triangles and a positive vertex mass (%# vertices, %# indices, mass %#)", vertexCount,
                          desc._listTriangleIndex.size(), desc._vertexMass );
            return PhysicsSoftBodyHandle{};
        }
        for ( const vector<uint32>* pListIndex : { &desc._listTriangleIndex, &desc._listPinnedVertex } )
        {
            for ( const uint32 vertexIndex : *pListIndex )
            {
                if ( vertexCount <= vertexIndex )
                {
                    SW_LOG_ERROR( "Soft body names vertex %# of %#", vertexIndex, vertexCount );
                    return PhysicsSoftBodyHandle{};
                }
            }
        }

        JPH::Ref<JPH::SoftBodySharedSettings> pShared = JoltUtil::createObject<JPH::SoftBodySharedSettings>(); // Jolt 참조 카운트 객체다. 바디가 들고 Ref 가 해제한다.
        pShared->mVertices.reserve( vertexCount );
        const float32 inverseMass = 1.0f / desc._vertexMass;
        for ( const float3& position : desc._listVertex )
        {
            pShared->mVertices.push_back( JPH::SoftBodySharedSettings::Vertex{
                JPH::Float3{position._x, position._y, position._z},
                JPH::Float3{       0.0f,        0.0f,        0.0f},
                inverseMass
            } );
        }
        for ( const uint32 vertexIndex : desc._listPinnedVertex )
        {
            pShared->mVertices[vertexIndex].mInvMass = 0.0f;
        }
        for ( size_t index = 0; index < desc._listTriangleIndex.size(); index += 3 )
        {
            const JPH::SoftBodySharedSettings::Face face{ desc._listTriangleIndex[index], desc._listTriangleIndex[index + 1], desc._listTriangleIndex[index + 2] };
            if ( face.IsDegenerate() )
            {
                SW_LOG_ERROR( "Soft body triangle %# repeats a vertex", index / 3 );
                return PhysicsSoftBodyHandle{};
            }
            pShared->AddFace( face );
        }
        // 정점 속성 하나를 모든 정점이 나눠 쓴다(속성 수가 정점보다 적으면 Jolt 가 마지막 것을 되풀이한다).
        const bool                                          bBend = desc._bendCompliance >= 0.0f;
        const JPH::SoftBodySharedSettings::VertexAttributes attributes{ desc._stretchCompliance, desc._shearCompliance, bBend ? desc._bendCompliance : FLT_MAX };
        pShared->CreateConstraints( &attributes, 1, bBend ? JPH::SoftBodySharedSettings::EBendType::Distance : JPH::SoftBodySharedSettings::EBendType::None );
        pShared->Optimize();

        JPH::SoftBodyCreationSettings settings{ pShared, JoltUtil::toJolt( desc._position ), JoltUtil::toJolt( desc._rotation ), JoltLayerUtil::makeObjectLayer( desc._layer, true ) };
        settings.mNumIterations  = desc._iterationCount;
        settings.mLinearDamping  = desc._linearDamping;
        settings.mFriction       = desc._friction;
        settings.mGravityFactor  = desc._gravityFactor;
        settings.mVertexRadius   = desc._vertexRadius;
        settings.mUpdatePosition = false;
        const JPH::BodyID bodyID = _system.GetBodyInterface().CreateAndAddSoftBody( settings, JPH::EActivation::Activate );
        if ( bodyID.IsInvalid() )
        {
            SW_LOG_ERROR( "Soft body could not be created (body limit)" );
            return PhysicsSoftBodyHandle{};
        }

        SoftBodyRecord record;
        record._bodyID           = bodyID;
        record._listPinnedVertex = desc._listPinnedVertex;
        record._listPinnedRestLocal.reserve( desc._listPinnedVertex.size() );
        for ( const uint32 vertexIndex : desc._listPinnedVertex )
        {
            record._listPinnedRestLocal.push_back( desc._listVertex[vertexIndex] );
        }
        return PhysicsSoftBodyHandle::fromSlot( _softBodies.insert( std::move( record ) ) );
    }

    void JoltPhysicsScene::removeSoftBody( const SoftBodyRecord& record )
    {
        JPH::BodyInterface& bodyInterface = _system.GetBodyInterface();
        bodyInterface.RemoveBody( record._bodyID );
        bodyInterface.DestroyBody( record._bodyID );
    }

    void JoltPhysicsScene::destroySoftBody( PhysicsSoftBodyHandle softBody )
    {
        SoftBodyRecord record;
        if ( _softBodies.take( softBody.getSlot(), record ) == false )
            return;
        removeSoftBody( record );
    }

    bool JoltPhysicsScene::isSoftBodyValid( PhysicsSoftBodyHandle softBody ) const
    {
        return _softBodies.get( softBody.getSlot() ) != nullptr;
    }

    bool JoltPhysicsScene::getSoftBodyVertices( PhysicsSoftBodyHandle softBody, vector<float3>& outListPosition ) const
    {
        outListPosition.clear();
        const SoftBodyRecord* pRecord = _softBodies.get( softBody.getSlot() );
        if ( pRecord == nullptr )
            return false;
        JPH::BodyLockRead lock( _system.GetBodyLockInterface(), pRecord->_bodyID );
        if ( lock.Succeeded() == false )
            return false;
        const JPH::Body&                     body      = lock.GetBody();
        const JPH::SoftBodyMotionProperties* pMotion   = static_cast<const JPH::SoftBodyMotionProperties*>( body.GetMotionProperties() );
        const JPH::RMat44                    transform = body.GetCenterOfMassTransform();
        outListPosition.reserve( pMotion->GetVertices().size() );
        for ( const JPH::SoftBodyVertex& vertex : pMotion->GetVertices() )
        {
            outListPosition.push_back( JoltUtil::toEngine( JPH::Vec3( transform * vertex.mPosition ) ) );
        }
        return true;
    }

    void JoltPhysicsScene::setSoftBodyPinnedTransform( PhysicsSoftBodyHandle softBody, const float3& position, const quaternion& rotation )
    {
        const SoftBodyRecord* pRecord = _softBodies.get( softBody.getSlot() );
        if ( pRecord == nullptr || pRecord->_listPinnedVertex.empty() )
            return;
        {
            JPH::BodyLockWrite lock( _system.GetBodyLockInterface(), pRecord->_bodyID );
            if ( lock.Succeeded() == false )
                return;
            JPH::Body&                     body    = lock.GetBody();
            JPH::SoftBodyMotionProperties* pMotion = static_cast<JPH::SoftBodyMotionProperties*>( body.GetMotionProperties() );
            // 서술자 로컬 → 월드(고정 자세) → 질량 중심 기준(Jolt 정점의 좌표)
            const JPH::RMat44 pinnedToBody = body.GetCenterOfMassTransform().InversedRotationTranslation() *
                                             JPH::RMat44::sRotationTranslation( JoltUtil::toJolt( rotation ), JoltUtil::toJolt( position ) );
            for ( size_t pinnedIndex = 0; pinnedIndex < pRecord->_listPinnedVertex.size(); ++pinnedIndex )
            {
                JPH::SoftBodyVertex& vertex = pMotion->GetVertex( pRecord->_listPinnedVertex[pinnedIndex] );
                vertex.mPosition            = JPH::Vec3( pinnedToBody * JoltUtil::toJolt( pRecord->_listPinnedRestLocal[pinnedIndex] ) );
                vertex.mVelocity            = JPH::Vec3::sZero();
            }
        }
        // 잠금을 놓은 뒤 깨운다(바디 인터페이스가 다시 잠근다). 자는 천은 고정 정점이 옮겨져도 풀리지 않는다.
        _system.GetBodyInterface().ActivateBody( pRecord->_bodyID );
    }

    void JoltPhysicsScene::drawSoftBodies( IPhysicsDebugRenderer& renderer ) const
    {
        const float4 color{ 0.3f, 0.9f, 0.9f, 1.0f };
        _softBodies.forEachHandle( [&]( SlotHandle, const SoftBodyRecord& record )
        {
            JPH::BodyLockRead lock( _system.GetBodyLockInterface(), record._bodyID );
            if ( lock.Succeeded() == false )
                return;
            const JPH::Body&                     body      = lock.GetBody();
            const JPH::SoftBodyMotionProperties* pMotion   = static_cast<const JPH::SoftBodyMotionProperties*>( body.GetMotionProperties() );
            const JPH::RMat44                    transform = body.GetCenterOfMassTransform();
            for ( const JPH::SoftBodySharedSettings::Edge& edge : pMotion->GetSettings()->mEdgeConstraints )
            {
                const JPH::Vec3 from = JPH::Vec3( transform * pMotion->GetVertex( edge.mVertex[0] ).mPosition );
                const JPH::Vec3 to   = JPH::Vec3( transform * pMotion->GetVertex( edge.mVertex[1] ).mPosition );
                renderer.drawLine( JoltUtil::toEngine( from ), JoltUtil::toEngine( to ), color );
            }
        } );
    }
} // namespace sw
