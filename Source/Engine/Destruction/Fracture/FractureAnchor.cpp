#include "pch.h"

#include "Engine/Destruction/Fracture/FractureAnchor.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Destruction/Fracture/FractureComponentBase.h"

namespace sw
{
    bool FractureAnchorUtil::isBottomLeaf( const FractureAsset& asset, uint32 leaf, float32 tolerance, float32 scale )
    {
        return asset._listPiece[leaf]._boundsMin._y <= asset._boundsMin._y + tolerance / scale;
    }

    bool FractureAnchorUtil::isInsideAnyVolume( const float3& point, vector_reference<const FractureAnchorVolume> listVolume )
    {
        for ( const FractureAnchorVolume& volume : listVolume )
        {
            const float3 delta = point - volume._center;
            const bool   bIn   = MathUtil::abs( delta._x ) <= volume._halfExtents._x && MathUtil::abs( delta._y ) <= volume._halfExtents._y &&
                             MathUtil::abs( delta._z ) <= volume._halfExtents._z;
            if ( bIn )
                return true;
        }
        return false;
    }

    void FractureAnchorUtil::computeAnchors( const FractureAsset& asset, FractureAnchorMode mode, float32 tolerance, float32 scale,
                                             vector_reference<const FractureAnchorVolume> listVolume, vector_reference<const uint8> listWorldContact,
                                             vector<uint8>& outListAnchor )
    {
        outListAnchor.assign( asset.getPieceCount(), 0 );
        for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
        {
            bool bAnchored = false;
            if ( mode == FractureAnchorMode::Bottom )
                bAnchored = isBottomLeaf( asset, leaf, tolerance, scale );
            else if ( mode == FractureAnchorMode::World )
                bAnchored = leaf < listWorldContact.size() && listWorldContact[leaf] != 0;
            bAnchored           = bAnchored || isInsideAnyVolume( asset._graph._listNode[leaf]._centroid, listVolume );
            outListAnchor[leaf] = bAnchored ? 1 : 0;
        }
    }
} // namespace sw
