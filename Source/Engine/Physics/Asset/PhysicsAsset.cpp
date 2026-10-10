#include "pch.h"

#include "Engine/Physics/Asset/PhysicsAsset.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "PhysicsAsset" );

    bool PhysicsAsset::loadFromResource( string_view resourcePath )
    {
        string text;
        if ( ResourceUtil::readTextResource( resourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Physics asset not found: %#", resourcePath );
            return false;
        }
        return loadFromXMLText( text, resourcePath );
    }

    bool PhysicsAsset::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = PhysicsAsset{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "%#: physics asset could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool PhysicsAsset::validate( string_view sourceName ) const
    {
        bool bValid = true;
        for ( size_t bodyIndex = 0; bodyIndex < _listBody.size(); ++bodyIndex )
        {
            const PhysicsAssetBodyDef& body = _listBody[bodyIndex];
            if ( body._bone.empty() )
            {
                SW_LOG_ERROR( "%#: body %# has no bone name", sourceName, bodyIndex );
                bValid = false;
            }
            if ( body._listShape.empty() )
            {
                SW_LOG_ERROR( "%#: body '%#' has no shape", sourceName, body._bone.c_str() );
                bValid = false;
            }
            for ( size_t otherIndex = bodyIndex + 1; otherIndex < _listBody.size(); ++otherIndex )
            {
                if ( _listBody[otherIndex]._bone == body._bone )
                {
                    SW_LOG_ERROR( "%#: bone '%#' has two bodies", sourceName, body._bone.c_str() );
                    bValid = false;
                }
            }
        }
        for ( const PhysicsAssetPairDef& pair : _listDisabledPair )
        {
            if ( findBodyIndex( pair._boneA ) < 0 || findBodyIndex( pair._boneB ) < 0 )
            {
                SW_LOG_ERROR( "%#: disabled pair '%#' - '%#' names a bone without a body", sourceName, pair._boneA.c_str(), pair._boneB.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    bool PhysicsAsset::validateAgainstSkeleton( span<const hashed_string> listBoneName, string_view sourceName ) const
    {
        bool bValid = true;
        for ( const PhysicsAssetBodyDef& body : _listBody )
        {
            bool bFound = false;
            for ( const hashed_string& boneName : listBoneName )
            {
                if ( boneName == body._bone )
                {
                    bFound = true;
                    break;
                }
            }
            if ( bFound == false )
            {
                SW_LOG_ERROR( "%#: bone '%#' is not in the skeleton", sourceName, body._bone.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    int32 PhysicsAsset::findBodyIndex( const hashed_string& bone ) const
    {
        for ( size_t bodyIndex = 0; bodyIndex < _listBody.size(); ++bodyIndex )
        {
            if ( _listBody[bodyIndex]._bone == bone )
                return static_cast<int32>( bodyIndex );
        }
        return -1;
    }
} // namespace sw
