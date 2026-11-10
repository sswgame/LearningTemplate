#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSocketRig.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterDataCache.h"

#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

namespace sw
{
    namespace
    {
        struct AppearanceSocketRigInternal
        {
            static constexpr const utf8* kRigidRootBone = "root";

            /** @brief 몸 부품입니다. 주인이 비어 있는 첫 휘는 스킨드 부품이며, 없으면 비어 있습니다. */
            static optional<uint32> findBodyPart( const ResolvedAppearance& resolved )
            {
                for ( size_t partIndex = 0; partIndex < resolved._listPart.size(); ++partIndex )
                {
                    const ResolvedPart& part = resolved._listPart[partIndex];
                    if ( part._owner.empty() && part.isSkinnedFollower() )
                        return static_cast<uint32>( partIndex );
                }
                return nullopt;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AppearanceSocketSetCache::AppearanceSocketSetCache()
        : _mapSocketSet{}
        , _socketSetReload{}
    {
        _socketSetReload.markSeen();
    }

    AppearanceSocketSetCache::~AppearanceSocketSetCache() = default;

    const SocketSet* AppearanceSocketSetCache::findSocketSet( const hashed_string& path )
    {
        return findSharedSocketSet( path ).get();
    }

    shared_ptr<const SocketSet> AppearanceSocketSetCache::findSharedSocketSet( const hashed_string& path )
    {
        if ( path.empty() )
            return nullptr;
        const auto found = _mapSocketSet.find( path );
        if ( found != _mapSocketSet.end() )
            return found->second;
        // 공유 캐시가 종류 표(엔진 기본)와 대조해 읽는다. 읽지 못하면 읽는 함수가 까닭을 남긴다.
        shared_ptr<const SocketSet> socketSet = SharedAssetCache<SocketSet>::acquire( path.viewOrEmpty() );
        if ( socketSet == nullptr )
            SW_LOG_ERROR_ID( "AppearanceSocketRig.SocketAssetCouldNot", "Socket asset '%#' could not be loaded", path.c_str() );
        _mapSocketSet[path] = socketSet;
        return socketSet;
    }

    void AppearanceSocketSetCache::clear()
    {
        _mapSocketSet.clear();
        _socketSetReload.markSeen();
    }

    bool AppearanceSocketSetCache::isStale() const
    {
        return _socketSetReload.isStale();
    }
} // namespace sw

namespace sw
{
    AppearanceSocketRig::AppearanceSocketRig()
        : _table{}
        , _rigidBones{}
        , _listUnitPart{}
    {
        (void)_rigidBones.addBone( hashed_string( AppearanceSocketRigInternal::kRigidRootBone ), constant::invalid_index::kInt32, float4x4::Identity );
    }

    bool AppearanceSocketRig::rebuild( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, IAppearanceSocketSource& source, CharacterIssueList& outIssues )
    {
        _table.beginResolve();
        _listUnitPart.clear();
        bool bSucceeded = true;

        // 유닛 0 은 몸이다. 소켓 에셋이 없어도 유닛을 만든다. 덮어쓰기만으로 소켓이 생길 수 있기 때문이다.
        const optional<uint32> bodyPart = AppearanceSocketRigInternal::findBodyPart( resolved );
        SocketSet              bodySockets;
        if ( bodyPart.has_value() )
        {
            const hashed_string& bodySocketPath = resolved._listPart[*bodyPart]._socketSet;
            const SocketSet*     pBodySockets   = bodySocketPath.empty() ? nullptr : source.findSocketSet( bodySocketPath );
            if ( pBodySockets != nullptr )
                bodySockets = *pBodySockets;
            else if ( bodySocketPath.empty() == false )
                bSucceeded = false;
        }
        applySocketOverrides( resolved, bodyBindBones, bodySockets );
        _listUnitPart.push_back( bodyPart );
        if ( _table.addUnit( hashed_string{}, kBodyUnit, bodySockets, bodyBindBones, nullptr, outIssues ) == false )
            bSucceeded = false;

        // 부품 유닛은 소켓 에셋을 가진 부품마다 하나이다(강체이며 본 "root" 하나). 이름 앞에는 주인 이름이 붙는다.
        for ( size_t partIndex = 0; partIndex < resolved._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = resolved._listPart[partIndex];
            if ( bodyPart == static_cast<uint32>( partIndex ) || part._socketSet.empty() )
                continue;
            const SocketSet* pSockets = source.findSocketSet( part._socketSet );
            if ( pSockets == nullptr )
            {
                bSucceeded = false;
                continue;
            }
            const uint32 unitIndex = static_cast<uint32>( _listUnitPart.size() );
            if ( _table.addUnit( part._owner, unitIndex, *pSockets, _rigidBones, nullptr, outIssues ) == false )
            {
                bSucceeded = false;
                continue;
            }
            _listUnitPart.push_back( static_cast<uint32>( partIndex ) );
        }
        _table.endResolve();
        return bSucceeded;
    }

    bool AppearanceSocketRig::computePlacement( const AppearancePlacement& placement, const CharacterBoneArray& bodyBones, float4x4& outInUnit, uint32& outUnitIndex ) const
    {
        const SocketID socketID = _table.findFirstActiveSocket( vector_reference<const hashed_string>( placement._listSocket ) );
        const SocketID target   = _table.resolveTarget( socketID );
        if ( target == kInvalidSocketID )
            return false;
        outUnitIndex                        = _table.getSocketUnit( target );
        const CharacterBoneArray& unitBones = outUnitIndex == kBodyUnit ? bodyBones : _rigidBones;
        float4x4                  socketInUnit;
        if ( _table.computeUnitTransform( target, unitBones, socketInUnit ) == false )
            return false;
        // 행벡터 규약이므로 배치 오프셋이 소켓 공간에서 먼저 적용된다.
        outInUnit = makePlacementTransform( placement ) * socketInUnit;
        return true;
    }

    bool AppearanceSocketRig::findSocketWorldTransform( const hashed_string& fullName, const CharacterBoneArray& bodyBones, vector_reference<const float4x4> listUnitWorld,
                                                        float4x4& outWorldTransform ) const
    {
        const SocketID target = _table.resolveTarget( _table.findSocket( fullName ) );
        if ( target == kInvalidSocketID )
            return false;
        const uint32 unitIndex = _table.getSocketUnit( target );
        if ( unitIndex >= listUnitWorld.size() )
            return false;
        const CharacterBoneArray& unitBones = unitIndex == kBodyUnit ? bodyBones : _rigidBones;
        float4x4                  socketInUnit;
        if ( _table.computeUnitTransform( target, unitBones, socketInUnit ) == false )
            return false;
        outWorldTransform = socketInUnit * listUnitWorld[unitIndex];
        return true;
    }

    float4x4 AppearanceSocketRig::makePlacementTransform( const AppearancePlacement& placement )
    {
        const quaternion rotation = quaternion::makeFromYawPitchRoll( placement._rotation * MathUtil::kDegreeToRadian );
        return CharacterGeometryUtil::makeTransform( placement._offset, rotation, float3( 1.0f ) );
    }

    void AppearanceSocketRig::applySocketOverrides( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, SocketSet& inoutBodySockets )
    {
        if ( resolved._listSocketOverride.empty() )
            return;
        SocketSet upper;
        for ( const AppearanceSocketOverride& socketOverride : resolved._listSocketOverride )
        {
            SocketDef def;
            def._name        = socketOverride._name;
            def._translation = socketOverride._placement._offset;
            def._rotation    = quaternion::makeFromYawPitchRoll( socketOverride._placement._rotation * MathUtil::kDegreeToRadian );
            def._fieldMask   = SocketFieldBit::kTranslation | SocketFieldBit::kRotation;
            // 부모 후보는 본 이름이거나 몸 소켓 이름이다. 소켓이면 그 소켓의 부모 본을 따른다. 소켓 위의 소켓은 테이블이 해석하지 않는다.
            for ( const hashed_string& candidate : socketOverride._placement._listSocket )
            {
                const SocketDef* pExisting = inoutBodySockets.findSocket( candidate );
                if ( bodyBindBones.findBone( candidate ).has_value() )
                    def._parent = candidate;
                else if ( pExisting != nullptr )
                    def._parent = pExisting->_parent;
                else
                    continue;
                def._fieldMask |= SocketFieldBit::kParent;
                break;
            }
            upper.addSocket( def );
        }
        inoutBodySockets.applyOverride( upper );
    }
} // namespace sw
