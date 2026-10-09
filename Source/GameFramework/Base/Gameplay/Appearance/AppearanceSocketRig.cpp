#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceSocketRig.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"

namespace sw
{
    SW_LOG_CALLER( "AppearanceSocketRig" );

    namespace
    {
        struct AppearanceSocketRigInternal
        {
            static constexpr const utf8* kDefaultSocketKinds = "engine/character/default.socketkinds.xml";
            static constexpr const utf8* kRigidRootBone      = "root";

            /** @brief 몸 부품 — 주인이 빈 첫 스킨드 부품입니다. 없으면 -1 입니다. */
            static int32 findBodyPart( const ResolvedAppearance& resolved )
            {
                for ( size_t partIndex = 0; partIndex < resolved._listPart.size(); ++partIndex )
                {
                    const ResolvedPart& part = resolved._listPart[partIndex];
                    if ( part._owner.empty() && part._kind == AppearancePartKind::Skinned )
                        return static_cast<int32>( partIndex );
                }
                return -1;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AppearanceSocketSetCache::AppearanceSocketSetCache()
        : _mapSocketSet{}
        , _kinds{}
        , _bKindsLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    AppearanceSocketSetCache::~AppearanceSocketSetCache() = default;

    const SocketSet* AppearanceSocketSetCache::findSocketSet( const hashed_string& path )
    {
        if ( path.empty() )
            return nullptr;
        const auto found = _mapSocketSet.find( path );
        if ( found != _mapSocketSet.end() )
            return found->second.get();
        if ( _bKindsLoaded == SW_FALSE )
        {
            _bKindsLoaded = SW_TRUE;
            if ( _kinds.loadFromResource( AppearanceSocketRigInternal::kDefaultSocketKinds ) == false )
                SW_LOG_ERROR( "Socket kinds '%#' could not be loaded - sockets with a kind will fail", AppearanceSocketRigInternal::kDefaultSocketKinds );
        }
        unique_ptr<SocketSet> socketSet = make_unique<SocketSet>();
        if ( socketSet->loadFromResource( path.c_str(), _kinds ) == false )
        {
            SW_LOG_ERROR( "Socket asset '%#' could not be loaded", path.c_str() );
            socketSet.reset();
        }
        const SocketSet* pSocketSet = socketSet.get();
        _mapSocketSet[path]         = std::move( socketSet );
        return pSocketSet;
    }

    void AppearanceSocketSetCache::clear()
    {
        _mapSocketSet.clear();
        _kinds        = SocketKindTable{};
        _bKindsLoaded = SW_FALSE;
    }
} // namespace sw

namespace sw
{
    AppearanceSocketRig::AppearanceSocketRig()
        : _table{}
        , _rigidBones{}
        , _listUnitPart{}
        , _listPartUnit{}
    {
        (void)_rigidBones.addBone( hashed_string( AppearanceSocketRigInternal::kRigidRootBone ), -1, float4x4::Identity );
    }

    bool AppearanceSocketRig::rebuild( const ResolvedAppearance& resolved, const CharacterBoneArray& bodyBindBones, IAppearanceSocketSource& source, string* pOutError )
    {
        _table.beginResolve();
        _listUnitPart.clear();
        _listPartUnit.assign( resolved._listPart.size(), kNoUnit );
        bool bSucceeded = true;

        // 유닛 0 = 몸. 소켓 에셋이 없어도 자리를 둔다(덮어쓰기만으로 소켓이 생길 수 있다).
        const int32 bodyPart = AppearanceSocketRigInternal::findBodyPart( resolved );
        SocketSet   bodySockets;
        if ( bodyPart >= 0 )
        {
            const hashed_string& bodySocketPath = resolved._listPart[static_cast<size_t>( bodyPart )]._socketSet;
            const SocketSet*     pBodySockets   = bodySocketPath.empty() ? nullptr : source.findSocketSet( bodySocketPath );
            if ( pBodySockets != nullptr )
                bodySockets = *pBodySockets;
            else if ( bodySocketPath.empty() == false )
                bSucceeded = false;
        }
        applySocketOverrides( resolved, bodyBindBones, bodySockets );
        _listUnitPart.push_back( bodyPart >= 0 ? static_cast<uint32>( bodyPart ) : kNoUnit );
        if ( bodyPart >= 0 )
            _listPartUnit[static_cast<size_t>( bodyPart )] = kBodyUnit;
        if ( _table.addUnit( hashed_string{}, kBodyUnit, bodySockets, bodyBindBones, nullptr, pOutError ) == false )
            bSucceeded = false;

        // 부품 유닛 — 소켓 에셋을 가진 부품마다 하나(강체: 본 "root" 하나). 이름은 주인 이름이 앞에 붙는다.
        for ( size_t partIndex = 0; partIndex < resolved._listPart.size(); ++partIndex )
        {
            const ResolvedPart& part = resolved._listPart[partIndex];
            if ( static_cast<int32>( partIndex ) == bodyPart || part._socketSet.empty() )
                continue;
            const SocketSet* pSockets = source.findSocketSet( part._socketSet );
            if ( pSockets == nullptr )
            {
                bSucceeded = false;
                continue;
            }
            const uint32 unitIndex = static_cast<uint32>( _listUnitPart.size() );
            if ( _table.addUnit( part._owner, unitIndex, *pSockets, _rigidBones, nullptr, pOutError ) == false )
            {
                bSucceeded = false;
                continue;
            }
            _listUnitPart.push_back( static_cast<uint32>( partIndex ) );
            _listPartUnit[partIndex] = unitIndex;
        }
        _table.endResolve();
        return bSucceeded;
    }

    uint32 AppearanceSocketRig::findPartUnit( uint32 partIndex ) const
    {
        return partIndex < _listPartUnit.size() ? _listPartUnit[partIndex] : kNoUnit;
    }

    bool AppearanceSocketRig::computePlacement( const AppearancePlacement& placement, const CharacterBoneArray& bodyBones, float4x4& outInUnit, uint32& outUnitIndex ) const
    {
        const SocketId socketId = _table.findFirstActiveSocket( vector_reference<const hashed_string>( placement._listSocket ) );
        const SocketId target   = _table.resolveTarget( socketId );
        if ( target == kInvalidSocketId )
            return false;
        outUnitIndex                        = _table.getSocketUnit( target );
        const CharacterBoneArray& unitBones = outUnitIndex == kBodyUnit ? bodyBones : _rigidBones;
        float4x4                  socketInUnit;
        if ( _table.computeUnitTransform( target, unitBones, socketInUnit ) == false )
            return false;
        // 행벡터 — 배치 오프셋은 소켓 공간에서 먼저 적용된다.
        outInUnit = makePlacementTransform( placement ) * socketInUnit;
        return true;
    }

    bool AppearanceSocketRig::findSocketWorldTransform( const hashed_string& fullName, const CharacterBoneArray& bodyBones, vector_reference<const float4x4> listUnitWorld,
                                                        float4x4& outWorldTransform ) const
    {
        const SocketId target = _table.resolveTarget( _table.findSocket( fullName ) );
        if ( target == kInvalidSocketId )
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
        const quaternion rotation = quaternion::createFromYawPitchRoll( placement._rotation * MathUtil::kDegreeToRadian );
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
            def._rotation    = quaternion::createFromYawPitchRoll( socketOverride._placement._rotation * MathUtil::kDegreeToRadian );
            def._fieldMask   = SocketFieldBit::kTranslation | SocketFieldBit::kRotation;
            // 부모 후보는 본 또는 몸 소켓 이름 — 소켓이면 그 소켓의 부모 본을 따른다(소켓 위 소켓은 표가 풀지 않는다).
            for ( const hashed_string& candidate : socketOverride._placement._listSocket )
            {
                const SocketDef* pExisting = inoutBodySockets.findSocket( candidate );
                if ( bodyBindBones.findBone( candidate ) >= 0 )
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
