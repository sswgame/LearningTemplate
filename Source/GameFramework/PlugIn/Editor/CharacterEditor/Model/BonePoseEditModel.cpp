#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/BonePoseEditModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    namespace
    {
        struct BonePoseEditModelInternal
        {
            static constexpr float32 kSameRotationTolerance = 1.0e-6f;

            static bool isSameRotation( const quaternion& lhs, const quaternion& rhs )
            {
                const bool bSameXY = MathUtil::abs( lhs._x - rhs._x ) <= kSameRotationTolerance && MathUtil::abs( lhs._y - rhs._y ) <= kSameRotationTolerance;
                const bool bSameZW = MathUtil::abs( lhs._z - rhs._z ) <= kSameRotationTolerance && MathUtil::abs( lhs._w - rhs._w ) <= kSameRotationTolerance;
                return bSameXY && bSameZW;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    BonePoseEditModel::BonePoseEditModel()
        : _skeleton{}
        , _override{}
        , _savedText{ _override.saveToXMLText() }
        , _history{}
    {
    }

    bool BonePoseEditModel::loadSkeleton( string_view path )
    {
        Skeleton loaded;
        if ( loaded.loadFromResource( path ) == false )
            return false;
        _skeleton = std::move( loaded );
        return true;
    }

    void BonePoseEditModel::collectBoneNames( vector<hashed_string>& outListBoneName ) const
    {
        outListBoneName.clear();
        for ( uint32 index = 0; index < _skeleton.getBoneCount(); ++index )
        {
            outListBoneName.push_back( _skeleton.getBone( index )._name );
        }
    }

    void BonePoseEditModel::collectChildren( int32 parentIndex, vector<uint32>& outListChild ) const
    {
        outListChild.clear();
        for ( uint32 index = 0; index < _skeleton.getBoneCount(); ++index )
        {
            if ( _skeleton.getBone( index )._parentIndex == parentIndex )
                outListChild.push_back( index );
        }
    }

    bool BonePoseEditModel::getEffectiveTransform( const hashed_string& bone, BoneTransform& outTransform ) const
    {
        const int32 boneIndex = _skeleton.findBoneIndex( bone );
        if ( boneIndex < 0 )
            return false;
        outTransform                  = _skeleton.getBone( static_cast<uint32>( boneIndex ) )._referencePose;
        const BoneOverride* pOverride = _override.findOverride( bone );
        if ( pOverride == nullptr )
            return true;
        if ( ( pOverride->_fieldMask & BoneOverride::kTranslationBit ) != 0 )
            outTransform._translation = pOverride->_translation;
        if ( ( pOverride->_fieldMask & BoneOverride::kRotationBit ) != 0 )
            outTransform._rotation = pOverride->_rotation;
        if ( ( pOverride->_fieldMask & BoneOverride::kScaleBit ) != 0 )
            outTransform._scale = pOverride->_scale;
        return true;
    }

    bool BonePoseEditModel::findCurrent( const hashed_string& bone, BoneTransform& outCurrent, string& outError ) const
    {
        if ( getEffectiveTransform( bone, outCurrent ) )
            return true;
        outError = string( "Bone '" ) + bone.c_str() + "' is not in the skeleton";
        return false;
    }

    void BonePoseEditModel::recordUndo( const hashed_string& bone, string_view fieldName )
    {
        string key;
        if ( fieldName.empty() == false )
        {
            key = string( fieldName );
            key += ":";
            key += bone.c_str();
        }
        _history.record( _override, key );
    }

    bool BonePoseEditModel::setTranslation( const hashed_string& bone, const float3& translation, bool bContinuous, string& outError )
    {
        BoneTransform current;
        if ( findCurrent( bone, current, outError ) == false || current._translation == translation )
            return false;
        recordUndo( bone, bContinuous ? string_view( "translation" ) : string_view{} );
        BoneOverride edit;
        edit._bone        = bone;
        edit._translation = translation;
        edit._fieldMask   = BoneOverride::kTranslationBit;
        _override.setOverride( edit );
        return true;
    }

    bool BonePoseEditModel::setRotationDegrees( const hashed_string& bone, const float3& degrees, bool bContinuous, string& outError )
    {
        BoneTransform current;
        if ( findCurrent( bone, current, outError ) == false )
            return false;
        const quaternion rotation = quaternion::makeFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
        if ( BonePoseEditModelInternal::isSameRotation( current._rotation, rotation ) )
            return false;
        recordUndo( bone, bContinuous ? string_view( "rotation" ) : string_view{} );
        BoneOverride edit;
        edit._bone      = bone;
        edit._rotation  = rotation;
        edit._fieldMask = BoneOverride::kRotationBit;
        _override.setOverride( edit );
        return true;
    }

    bool BonePoseEditModel::setScale( const hashed_string& bone, const float3& scale, bool bContinuous, string& outError )
    {
        BoneTransform current;
        if ( findCurrent( bone, current, outError ) == false || current._scale == scale )
            return false;
        recordUndo( bone, bContinuous ? string_view( "scale" ) : string_view{} );
        BoneOverride edit;
        edit._bone      = bone;
        edit._scale     = scale;
        edit._fieldMask = BoneOverride::kScaleBit;
        _override.setOverride( edit );
        return true;
    }

    bool BonePoseEditModel::clearBone( const hashed_string& bone, string& outError )
    {
        if ( hasOverride( bone ) == false )
        {
            outError = string( "Bone '" ) + bone.c_str() + "' has no override";
            return false;
        }
        recordUndo( bone, {} );
        return _override.removeOverride( bone );
    }

    bool BonePoseEditModel::mirrorBone( const hashed_string& bone, const MirrorNameRules& rules, hashed_string& outMirrorBone, string& outError )
    {
        string mirrorName;
        if ( rules.findMirrorName( bone.view(), mirrorName ) == false )
        {
            outError = string( "Bone '" ) + bone.c_str() + "' has no left/right token";
            return false;
        }
        const hashed_string counterpart{ string_view( mirrorName ) };
        if ( _skeleton.findBoneIndex( counterpart ) < 0 )
        {
            outError = string( "Mirror bone '" ) + mirrorName + "' is not in the skeleton";
            return false;
        }
        if ( hasOverride( bone ) == false )
        {
            outError = string( "Bone '" ) + bone.c_str() + "' has no override to mirror";
            return false;
        }
        recordUndo( bone, {} );
        _override.addMirrorPair( bone, counterpart );
        const bool bMirrored = _override.mirrorOverride( bone );
        SW_ASSERT( bMirrored );
        (void)bMirrored;
        outMirrorBone = counterpart;
        return true;
    }

    bool BonePoseEditModel::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        ReferencePoseOverride loaded;
        if ( loaded.loadFromXMLText( xmlText, sourceName ) == false )
            return false;
        _override  = std::move( loaded );
        _savedText = _override.saveToXMLText();
        _history.clear();
        return true;
    }

    bool BonePoseEditModel::loadFromFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR( "Reference pose file could not be read: %#", string( absolutePath ).c_str() );
            return false;
        }
        return loadFromXMLText( text, absolutePath );
    }

    void BonePoseEditModel::clear()
    {
        _override  = ReferencePoseOverride{};
        _savedText = _override.saveToXMLText();
        _history.clear();
    }

    bool BonePoseEditModel::saveToFile( string_view absolutePath )
    {
        const string text = _override.saveToXMLText();
        if ( FileUtil::writeTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR( "Reference pose file could not be written: %#", string( absolutePath ).c_str() );
            return false;
        }
        _savedText = text;
        return true;
    }

    bool BonePoseEditModel::undo()
    {
        return _history.undo( _override );
    }

    bool BonePoseEditModel::redo()
    {
        return _history.redo( _override );
    }
} // namespace sw::editor
