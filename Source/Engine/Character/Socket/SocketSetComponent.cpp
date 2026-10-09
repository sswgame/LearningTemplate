#include "pch.h"

#include "Engine/Character/Socket/SocketSetComponent.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "SocketSet" );

    namespace
    {
        struct SocketSetComponentInternal
        {
            /** @brief 유닛의 본(이름 · 레퍼런스 포즈)을 소켓 대조용 본 배열로 옮깁니다. */
            static void makeBoneArray( const Skeleton& skeleton, CharacterBoneArray& outBones )
            {
                outBones = CharacterBoneArray{};
                for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
                {
                    const SkeletonBone& bone = skeleton.getBone( boneIndex );
                    (void)outBones.addBone( bone._name, bone._parentIndex, bone._referencePose.toMatrix() );
                }
                outBones.computeModelTransforms();
            }

            /** @brief 본 하나의 유닛 공간 행렬입니다(유닛이 없거나 본이 없으면 false). */
            static bool findBone( const SkeletalMeshComponent* pUnit, const hashed_string& boneName, float4x4& outTransform )
            {
                return pUnit != nullptr && pUnit->findBoneModelTransform( boneName, outTransform );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SocketSetComponent::SocketSetComponent()
        : _socketSetPath{}
        , _socketSet{}
    {
        setCanEverTick( false );
    }

    void SocketSetComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        loadSocketSet();
    }

    void SocketSetComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_pathName( "_socketSetPath" );
        if ( propertyName == s_pathName )
            loadSocketSet();
    }

    void SocketSetComponent::setSocketSetPath( string_view path )
    {
        _socketSetPath = string{ path };
        loadSocketSet();
    }

    const SocketSet* SocketSetComponent::getSocketSet() const
    {
        return _socketSet.get();
    }

    void SocketSetComponent::loadSocketSet()
    {
        _socketSet = _socketSetPath.empty() ? nullptr : SocketSetCache::acquire( _socketSetPath );
        if ( _socketSetPath.empty() == false && _socketSet == nullptr )
            SW_LOG_ERROR( "'%#': socket set '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _socketSetPath.c_str() );
        validateAgainstUnit();
    }

    void SocketSetComponent::validateAgainstUnit() const
    {
        const GameObject*            pOwner = getOwner();
        const SkeletalMeshComponent* pUnit  = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( _socketSet == nullptr || pUnit == nullptr )
            return;
        CharacterBoneArray bones;
        SocketSetComponentInternal::makeBoneArray( pUnit->getSkeleton(), bones );
        string error;
        if ( _socketSet->validateBones( bones, &error ) == false )
            SW_LOG_ERROR( "'%#': socket set '%#' names bones the skeleton does not have: %#", pOwner->getName().c_str(), _socketSetPath.c_str(), error.c_str() );
    }

    bool SocketSetComponent::computeSocketUnitTransform( const hashed_string& socketName, const SkeletalMeshComponent* pUnit, float4x4& outUnitTransform ) const
    {
        if ( _socketSet == nullptr )
            return false;
        const SocketDef* pSocket = _socketSet->findSocket( socketName );
        if ( pSocket == nullptr )
            return false;
        const float4x4 local = pSocket->makeLocalTransform();
        if ( pSocket->_parent.empty() )
        {
            outUnitTransform = local;
            return true;
        }
        float4x4 parent;
        if ( SocketSetComponentInternal::findBone( pUnit, pSocket->_parent, parent ) )
        {
            outUnitTransform = local * parent;
            return true;
        }
        // 가상 본 — 두 본 사이를 섞은 자리다.
        const VirtualBoneDef* pVirtual = _socketSet->findVirtualBone( pSocket->_parent );
        float4x4              from;
        float4x4              to;
        if ( pVirtual == nullptr || SocketSetComponentInternal::findBone( pUnit, pVirtual->_from, from ) == false ||
             SocketSetComponentInternal::findBone( pUnit, pVirtual->_to, to ) == false )
            return false;
        outUnitTransform = local * CharacterGeometryUtil::blendTransforms( from, to, pVirtual->_weight );
        return true;
    }

    bool SocketLookupUtil::findSocketWorldTransform( const GameObject& object, const hashed_string& name, float4x4& outWorldTransform )
    {
        const SkeletalMeshComponent* pUnit  = object.getComponent<SkeletalMeshComponent>();
        const SceneComponent*        pFrame = pUnit != nullptr ? pUnit : object.getPrimarySceneComponent();
        if ( pFrame == nullptr || name.empty() )
            return false;
        const SocketSetComponent* pSockets = object.getComponent<SocketSetComponent>();
        float4x4                  unitTransform;
        const bool                bFound = ( pSockets != nullptr && pSockets->computeSocketUnitTransform( name, pUnit, unitTransform ) ) ||
                            ( pUnit != nullptr && pUnit->findBoneModelTransform( name, unitTransform ) );
        if ( bFound == false )
            return false;
        outWorldTransform = unitTransform * pFrame->getWorldMatrix();
        return true;
    }

    bool SocketLookupUtil::findSocketWorldPosition( const GameObject& object, const hashed_string& name, float3& outPosition )
    {
        float4x4 world;
        if ( findSocketWorldTransform( object, name, world ) )
        {
            outPosition = world.getTranslation();
            return true;
        }
        const SceneComponent* pRoot = object.getPrimarySceneComponent();
        outPosition                 = pRoot != nullptr ? pRoot->getWorldPosition() : float3{};
        return false;
    }
} // namespace sw
