#include "pch.h"

#include "Engine/Character/Socket/ResolvedSocketTable.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/SurfaceBvh.h"

namespace sw
{
    namespace
    {
        struct ResolvedSocketTableInternal
        {
            /** @brief 표면 소켓이 바인드 형상에서 표면을 찾는 거리입니다(이보다 멀면 본을 따릅니다). */
            static constexpr float32 kSurfaceSearchDistance = 0.5f;

            static const hashed_string& getEmptyName()
            {
                static const hashed_string s_emptyName{};
                return s_emptyName;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ResolvedSocketTable::ResolvedSocketTable()
        : _listSlot{}
        , _listUnit{}
        , _mapNameToSocket{}
    {
    }

    void ResolvedSocketTable::beginResolve()
    {
        for ( Slot& slot : _listSlot )
        {
            slot._bActive  = SW_FALSE;
            slot._redirect = kInvalidSocketID;
        }
        _listUnit.clear();
    }

    hashed_string ResolvedSocketTable::makeFullName( const hashed_string& prefix, const hashed_string& name ) const
    {
        if ( prefix.empty() )
            return name;
        string fullName( prefix.view() );
        fullName += ".";
        fullName += name.view();
        return hashed_string( string_view( fullName ) );
    }

    SocketID ResolvedSocketTable::findOrAddSlot( const hashed_string& fullName )
    {
        const auto found = _mapNameToSocket.find( fullName );
        if ( found != _mapNameToSocket.end() )
            return found->second;
        const SocketID socketID = static_cast<SocketID>( _listSlot.size() );
        _listSlot.emplace_back();
        _listSlot.back()._fullName = fullName;
        _mapNameToSocket.emplace( fullName, socketID );
        return socketID;
    }

    const ResolvedSocketTable::Unit* ResolvedSocketTable::findUnit( uint32 unitIndex ) const
    {
        for ( const Unit& unit : _listUnit )
        {
            if ( unit._unitIndex == unitIndex )
                return &unit;
        }
        return nullptr;
    }

    bool ResolvedSocketTable::addUnit( const hashed_string& prefix, uint32 unitIndex, const SocketSet& sockets, const CharacterBoneArray& bindBones,
                                       const AppearanceGeometry* pBindGeometry, string* pOutError )
    {
        if ( sockets.validateBones( bindBones, pOutError ) == false )
            return false;
        Unit unit;
        unit._sockets   = sockets;
        unit._prefix    = prefix;
        unit._unitIndex = unitIndex;
        _listUnit.push_back( std::move( unit ) );
        const Unit& addedUnit = _listUnit.back();

        SurfaceBvh bindSurface;
        bool       bSurfaceBuilt = false;
        for ( uint32 unitSlot = 0; unitSlot < addedUnit._sockets.getSockets().size(); ++unitSlot )
        {
            const SocketDef& def      = addedUnit._sockets.getSockets()[unitSlot];
            const SocketID   socketID = findOrAddSlot( makeFullName( prefix, def._name ) );
            Slot&            slot     = _listSlot[socketID];
            slot._def                 = def;
            slot._unit                = unitIndex;
            slot._unitSlot            = unitSlot;
            slot._bActive             = SW_TRUE;
            slot._redirect            = kInvalidSocketID;
            slot._surfaceBinding      = SurfaceBinding{};
            slot._shapeOffsetLocal    = float3::Zero;

            // 부모의 바인드 모델 변환 — 표면 보정 델타를 부모 축으로 옮겨 애니메이션을 따르게 한다.
            SocketDef parentOnly    = def;
            parentOnly._translation = float3::Zero;
            parentOnly._rotation    = quaternion::Identity;
            parentOnly._scale       = float3( 1.0f );
            (void)addedUnit._sockets.computeSocketTransform( parentOnly, bindBones, slot._bindParentModel );

            const bool bWantsSurface = def._anchor == SocketAnchor::Surface && pBindGeometry != nullptr && pBindGeometry->getTriangleCount() > 0;
            if ( bWantsSurface == false )
                continue;
            if ( bSurfaceBuilt == false )
            {
                bindSurface.addSurface( 0, pBindGeometry->_listPosition, pBindGeometry->_listIndex );
                bindSurface.build();
                bSurfaceBuilt = true;
            }
            float4x4 bindTransform;
            if ( addedUnit._sockets.computeSocketTransform( def, bindBones, bindTransform ) == false )
                continue;
            const float3           bindPoint = bindTransform.getTranslation();
            vector<SurfaceBinding> listBinding;
            SurfaceTransferUtil::bindPoints( *pBindGeometry, bindSurface, vector_reference<const float3>( &bindPoint, 1 ),
                                             ResolvedSocketTableInternal::kSurfaceSearchDistance, listBinding );
            slot._surfaceBinding   = listBinding.front();
            slot._surfacePointBind = bindPoint;
        }
        return true;
    }

    void ResolvedSocketTable::endResolve()
    {
        for ( Slot& slot : _listSlot )
        {
            if ( slot._bActive == SW_FALSE || slot._def._listFallback.empty() )
                continue;
            const Unit* pUnit = findUnit( slot._unit );
            for ( const hashed_string& candidate : slot._def._listFallback )
            {
                SocketID candidateID = findSocket( candidate );
                if ( ( candidateID == kInvalidSocketID || isSocketActive( candidateID ) == false ) && pUnit != nullptr && pUnit->_prefix.empty() == false )
                    candidateID = findSocket( makeFullName( pUnit->_prefix, candidate ) );
                if ( candidateID != kInvalidSocketID && isSocketActive( candidateID ) && &_listSlot[candidateID] != &slot )
                {
                    slot._redirect = candidateID;
                    break;
                }
            }
        }
    }

    void ResolvedSocketTable::applyShapedGeometry( uint32 unitIndex, const AppearanceGeometry& shapedGeometry )
    {
        for ( Slot& slot : _listSlot )
        {
            if ( slot._bActive == SW_FALSE || slot._unit != unitIndex || slot._surfaceBinding._bBound == SW_FALSE )
                continue;
            if ( slot._surfaceBinding._triangle >= shapedGeometry.getTriangleCount() )
                continue;
            const float3 shapedPoint = SurfaceTransferUtil::evaluatePoint( shapedGeometry, slot._surfaceBinding );
            const float3 delta       = shapedPoint - slot._surfacePointBind;
            slot._shapeOffsetLocal   = float3::transformVector( delta, slot._bindParentModel.invert() );
        }
    }

    SocketID ResolvedSocketTable::findSocket( const hashed_string& fullName ) const
    {
        const auto found = _mapNameToSocket.find( fullName );
        return found == _mapNameToSocket.end() ? kInvalidSocketID : found->second;
    }

    SocketID ResolvedSocketTable::findFirstActiveSocket( vector_reference<const hashed_string> listCandidate ) const
    {
        for ( const hashed_string& candidate : listCandidate )
        {
            const SocketID socketID = findSocket( candidate );
            if ( isSocketActive( socketID ) )
                return socketID;
        }
        return kInvalidSocketID;
    }

    bool ResolvedSocketTable::isSocketActive( SocketID socketID ) const
    {
        return socketID < _listSlot.size() && _listSlot[socketID]._bActive == SW_TRUE;
    }

    SocketID ResolvedSocketTable::resolveTarget( SocketID socketID ) const
    {
        if ( isSocketActive( socketID ) == false )
            return kInvalidSocketID;
        // 후보가 다시 후보를 가리킬 수 있다 — 순환이면 소켓 수만큼 걷고 멈춘다.
        SocketID current = socketID;
        for ( size_t step = 0; step < _listSlot.size(); ++step )
        {
            const SocketID next = _listSlot[current]._redirect;
            if ( next == kInvalidSocketID || isSocketActive( next ) == false )
                return current;
            current = next;
        }
        return socketID;
    }

    uint32 ResolvedSocketTable::getSocketUnit( SocketID socketID ) const
    {
        return socketID < _listSlot.size() ? _listSlot[socketID]._unit : 0;
    }

    const hashed_string& ResolvedSocketTable::getSocketName( SocketID socketID ) const
    {
        return socketID < _listSlot.size() ? _listSlot[socketID]._fullName : ResolvedSocketTableInternal::getEmptyName();
    }

    const SocketDef* ResolvedSocketTable::findSocketDef( SocketID socketID ) const
    {
        return socketID < _listSlot.size() ? &_listSlot[socketID]._def : nullptr;
    }

    bool ResolvedSocketTable::computeUnitTransform( SocketID socketID, const CharacterBoneArray& unitBones, float4x4& outUnitTransform ) const
    {
        const SocketID target = resolveTarget( socketID );
        if ( target == kInvalidSocketID )
            return false;
        const Slot& slot  = _listSlot[target];
        const Unit* pUnit = findUnit( slot._unit );
        if ( pUnit == nullptr )
            return false;
        SocketDef corrected = slot._def;
        corrected._translation += slot._shapeOffsetLocal;
        return pUnit->_sockets.computeSocketTransform( corrected, unitBones, outUnitTransform );
    }

    bool ResolvedSocketTable::getSocketTransform( const hashed_string& fullName, const SocketPoseView& pose, float4x4& outWorldTransform ) const
    {
        return getSocketTransform( findSocket( fullName ), pose, outWorldTransform );
    }

    bool ResolvedSocketTable::getSocketTransform( SocketID socketID, const SocketPoseView& pose, float4x4& outWorldTransform ) const
    {
        const SocketID target = resolveTarget( socketID );
        if ( target == kInvalidSocketID )
            return false;
        const uint32 unitIndex = _listSlot[target]._unit;
        if ( unitIndex >= pose._listUnitBones.size() || pose._listUnitBones[unitIndex] == nullptr )
            return false;
        float4x4 unitTransform;
        if ( computeUnitTransform( target, *pose._listUnitBones[unitIndex], unitTransform ) == false )
            return false;
        outWorldTransform = unitIndex < pose._listUnitWorld.size() ? unitTransform * pose._listUnitWorld[unitIndex] : unitTransform;
        return true;
    }
} // namespace sw
