#include "pch.h"

#include "Engine/Physics/Asset/PhysicsAsset.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    bool PhysicsAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "PhysicsAsset.PhysicsAssetNotFound", "Physics asset not found: %#", assetPath );
            return false;
        }
        return loadComposed( text, assetPath, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool PhysicsAsset::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return loadComposed( xmlText, sourceName, ResourceContext::forType( *StaticType() ) );
    }

    bool PhysicsAsset::loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context )
    {
        *this = PhysicsAsset{};
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xmlText, sourceName, context, flatText, bComposed ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), flatText.empty() ? xmlText : string_view{ flatText } ) == false )
        {
            SW_LOG_ERROR_ID( "PhysicsAsset.PhysicsAssetCouldNot", "%#: physics asset could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        _baseID = DataInheritance::findBasePath( xmlText ); // 저장이 부모와의 차이로 쓰려고 든다(읽은 칸은 펼친 한 벌이다)
        return validate( sourceName );
    }

    bool PhysicsAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( validate( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "PhysicsAsset.NotSaved", "%#: physics asset not saved - it does not pass validation", absPath.str() );
            return false;
        }
        return _baseID.empty() ? ReflectedXMLFile::saveDesc( absPath, *this ) : ReflectedXMLFile::saveDescAsChild( absPath, *this, AssetID{ _baseID } );
    }

    bool PhysicsAsset::validate( string_view sourceName ) const
    {
        bool bValid = true;
        for ( size_t bodyIndex = 0; bodyIndex < _listBody.size(); ++bodyIndex )
        {
            const PhysicsAssetBodyDef& body = _listBody[bodyIndex];
            if ( body._bone.empty() )
            {
                SW_LOG_ERROR_ID( "PhysicsAsset.BodyNoBoneName", "%#: body %# has no bone name", sourceName, bodyIndex );
                bValid = false;
            }
            if ( body._listShape.empty() )
            {
                SW_LOG_ERROR_ID( "PhysicsAsset.BodyNoShape", "%#: body '%#' has no shape", sourceName, body._bone.c_str() );
                bValid = false;
            }
            for ( size_t otherIndex = bodyIndex + 1; otherIndex < _listBody.size(); ++otherIndex )
            {
                if ( _listBody[otherIndex]._bone == body._bone )
                {
                    SW_LOG_ERROR_ID( "PhysicsAsset.BoneTwoBodies", "%#: bone '%#' has two bodies", sourceName, body._bone.c_str() );
                    bValid = false;
                }
            }
        }
        for ( const PhysicsAssetPairDef& pair : _listDisabledPair )
        {
            if ( findBodyIndex( pair._boneA ).has_value() == false || findBodyIndex( pair._boneB ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "PhysicsAsset.DisabledPairNamesBone", "%#: disabled pair '%#' - '%#' names a bone without a body", sourceName, pair._boneA.c_str(), pair._boneB.c_str() );
                bValid = false;
            }
        }
        if ( _partialRootBone.empty() == false && findBodyIndex( _partialRootBone ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "PhysicsAsset.PartialRootBoneNoBody", "%#: partial root bone '%#' has no body", sourceName, _partialRootBone.c_str() );
            bValid = false;
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
                SW_LOG_ERROR_ID( "PhysicsAsset.BoneNotSkeleton", "%#: bone '%#' is not in the skeleton", sourceName, body._bone.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    optional<uint32> PhysicsAsset::findBodyIndex( const hashed_string& bone ) const
    {
        for ( size_t bodyIndex = 0; bodyIndex < _listBody.size(); ++bodyIndex )
        {
            if ( _listBody[bodyIndex]._bone == bone )
                return static_cast<uint32>( bodyIndex );
        }
        return nullopt;
    }
} // namespace sw
