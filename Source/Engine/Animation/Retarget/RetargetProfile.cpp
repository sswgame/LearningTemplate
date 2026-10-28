#include "pch.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Asset/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "RetargetProfile" );

    namespace
    {
        struct RetargetProfileInternal
        {
            /** @brief `RetargetTranslationMode` 순서의 표기입니다. */
            static constexpr const utf8* kArrTranslationMode[] = { "ScaleByPelvisHeight", "Copy", "None" };

            /** @brief 이름 목록을 `<item>이름</item>` 목록으로 씁니다(`RigXMLReader::readNameList` 의 꼴). */
            static void writeNames( const XMLNode& list, const vector<hashed_string>& listName )
            {
                for ( const hashed_string& name : listName )
                {
                    list.appendChild( "item" ).setValue( string_view( name.c_str() ) );
                }
            }

            /** @brief `source` · `target` 속성 쌍 하나를 씁니다. */
            static void writePair( const XMLNode& element, const hashed_string& source, const hashed_string& target )
            {
                element.appendAttribute( "source", string_view( source.c_str() ) );
                element.appendAttribute( "target", string_view( target.c_str() ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RetargetProfile::RetargetProfile()
        : _listChain{}
        , _listBonePair{}
        , _listTargetPose{}
        , _sourceSkeletonPath{}
        , _targetSkeletonPath{}
        , _sourceRoot{}
        , _targetRoot{}
        , _sourcePelvis{}
        , _targetPelvis{}
        , _heightRatioOverride{ 0.0f }
        , _translationMode{ RetargetTranslationMode::ScaleByPelvisHeight }
        , _bMapSameNames{ SW_FALSE }
    {
    }

    void RetargetProfile::clear()
    {
        _listChain.clear();
        _listBonePair.clear();
        _listTargetPose.clear();
        _sourceSkeletonPath.clear();
        _targetSkeletonPath.clear();
        _sourceRoot          = hashed_string{};
        _targetRoot          = hashed_string{};
        _sourcePelvis        = hashed_string{};
        _targetPelvis        = hashed_string{};
        _heightRatioOverride = 0.0f;
        _translationMode     = RetargetTranslationMode::ScaleByPelvisHeight;
        _bMapSameNames       = SW_FALSE;
    }

    void RetargetProfile::setRootAndPelvis( const hashed_string& sourceRoot, const hashed_string& targetRoot, const hashed_string& sourcePelvis,
                                            const hashed_string& targetPelvis, RetargetTranslationMode mode )
    {
        _sourceRoot      = sourceRoot;
        _targetRoot      = targetRoot;
        _sourcePelvis    = sourcePelvis;
        _targetPelvis    = targetPelvis;
        _translationMode = mode;
    }

    bool RetargetProfile::parseXML( string_view xml, string_view sourceLabel )
    {
        clear();
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "RetargetProfile", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR( "Retarget profile '%#': malformed XML (expected a <RetargetProfile> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
            return true;
        clear();
        return false;
    }

    bool RetargetProfile::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR( "Retarget profile '%#' could not be read", path );
            clear();
            return false;
        }
        return parseXML( text, path );
    }

    string RetargetProfile::toXML() const
    {
        using Internal = RetargetProfileInternal;
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "RetargetProfile" );
        if ( _sourceSkeletonPath.empty() == false )
            root.appendAttribute( "source_skeleton", string_view( _sourceSkeletonPath ) );
        if ( _targetSkeletonPath.empty() == false )
            root.appendAttribute( "target_skeleton", string_view( _targetSkeletonPath ) );
        root.appendAttribute( "translation", Internal::kArrTranslationMode[static_cast<uint32>( _translationMode )] );
        if ( _bMapSameNames == SW_TRUE )
            root.appendAttribute( "map_same_names", true );
        if ( _heightRatioOverride > 0.0f )
            root.appendAttribute( "height_ratio", _heightRatioOverride );
        Internal::writePair( root.appendChild( "root" ), _sourceRoot, _targetRoot );
        Internal::writePair( root.appendChild( "pelvis" ), _sourcePelvis, _targetPelvis );
        const XMLNode chains = root.appendChild( "chains" );
        for ( const RetargetChain& chain : _listChain )
        {
            const XMLNode entry = chains.appendChild( "item" );
            entry.appendAttribute( "name", string_view( chain._name.c_str() ) );
            if ( chain._bIKGoal == SW_TRUE )
                entry.appendAttribute( "ik_goal", true );
            Internal::writeNames( entry.appendChild( "source" ), chain._listSourceBone );
            Internal::writeNames( entry.appendChild( "target" ), chain._listTargetBone );
        }
        if ( _listBonePair.empty() == false )
        {
            const XMLNode boneMap = root.appendChild( "bone_map" );
            for ( const RetargetBonePair& pair : _listBonePair )
            {
                Internal::writePair( boneMap.appendChild( "item" ), pair._source, pair._target );
            }
        }
        if ( _listTargetPose.empty() == false )
        {
            const XMLNode targetPose = root.appendChild( "target_pose" );
            for ( const RetargetPoseBone& poseBone : _listTargetPose )
            {
                const XMLNode item = targetPose.appendChild( "item" );
                item.appendAttribute( "bone", string_view( poseBone._bone.c_str() ) );
                item.appendAttribute( "rotation", string_view{ XMLReadUtil::makeFloatListText( &poseBone._rotationDegrees._x, 3 ) } );
            }
        }
        return document.saveToString();
    }

    void RetargetProfile::setSkeletonPaths( string_view sourcePath, string_view targetPath )
    {
        _sourceSkeletonPath = string{ sourcePath };
        _targetSkeletonPath = string{ targetPath };
    }

    bool RetargetProfile::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        RigXMLReader  reader( root, sourceLabel );
        hashed_string sourceSkeleton{};
        hashed_string targetSkeleton{};
        uint32        modeIndex = 0;
        bool          bOk       = reader.readName( "source_skeleton", sourceSkeleton, false ) && reader.readName( "target_skeleton", targetSkeleton, false ) &&
                   reader.readChoice( "translation", RetargetProfileInternal::kArrTranslationMode, SW_COUNT_OF( RetargetProfileInternal::kArrTranslationMode ),
                                      modeIndex, false );
        _sourceSkeletonPath = sourceSkeleton.empty() ? string{} : string{ sourceSkeleton.c_str() };
        _targetSkeletonPath = targetSkeleton.empty() ? string{} : string{ targetSkeleton.c_str() };
        _translationMode    = static_cast<RetargetTranslationMode>( modeIndex );

        const XMLNode    arrPair[2]  = { reader.readObject( "root", true ), reader.readObject( "pelvis", true ) };
        const RigXMLList chains      = reader.readArray( "chains", true );
        const RigXMLList boneMap     = reader.readArray( "bone_map", false );
        const RigXMLList targetPose  = reader.readArray( "target_pose", false );
        bool             bMapSame    = false;
        float32          heightRatio = 0.0f;
        bOk                          = reader.readBool( "map_same_names", bMapSame, false ) && reader.readFloat( "height_ratio", heightRatio, false ) && bOk;
        bOk                          = reader.finish() && bOk;
        if ( bOk == false )
            return false;
        if ( root.findAttribute( "height_ratio" ) != nullptr && heightRatio <= 0.0f )
        {
            SW_LOG_ERROR( "Retarget profile '%#': 'height_ratio' must be positive", sourceLabel );
            return false;
        }
        _bMapSameNames       = bMapSame ? SW_TRUE : SW_FALSE;
        _heightRatioOverride = heightRatio;

        for ( size_t index = 0; boneMap.isValid() && index < boneMap.size(); ++index )
        {
            RigXMLReader     entryReader( boneMap.at( index ), sourceLabel );
            RetargetBonePair pair{};
            bOk = entryReader.readName( "source", pair._source, true ) && entryReader.readName( "target", pair._target, true );
            if ( ( entryReader.finish() && bOk ) == false )
                return false;
            _listBonePair.push_back( pair );
        }

        for ( size_t index = 0; targetPose.isValid() && index < targetPose.size(); ++index )
        {
            RigXMLReader     entryReader( targetPose.at( index ), sourceLabel );
            RetargetPoseBone poseBone{};
            bOk = entryReader.readName( "bone", poseBone._bone, true ) && entryReader.readFloat3( "rotation", poseBone._rotationDegrees, true );
            if ( ( entryReader.finish() && bOk ) == false )
                return false;
            for ( const RetargetPoseBone& existing : _listTargetPose )
            {
                if ( existing._bone == poseBone._bone )
                {
                    SW_LOG_ERROR( "Retarget profile '%#': duplicate target pose bone '%#'", sourceLabel, poseBone._bone.c_str() );
                    return false;
                }
            }
            _listTargetPose.push_back( poseBone );
        }

        hashed_string* arrSource[2] = { &_sourceRoot, &_sourcePelvis };
        hashed_string* arrTarget[2] = { &_targetRoot, &_targetPelvis };
        for ( uint32 pairIndex = 0; pairIndex < 2; ++pairIndex )
        {
            RigXMLReader pairReader( arrPair[pairIndex], sourceLabel );
            bOk = pairReader.readName( "source", *arrSource[pairIndex], true ) && pairReader.readName( "target", *arrTarget[pairIndex], true );
            if ( ( pairReader.finish() && bOk ) == false )
                return false;
        }

        for ( size_t index = 0; index < chains.size(); ++index )
        {
            RigXMLReader  chainReader( chains.at( index ), sourceLabel );
            RetargetChain chain{};
            bool          bIKGoal = false;
            bOk                   = chainReader.readName( "name", chain._name, true ) && chainReader.readNameList( "source", chain._listSourceBone, true ) &&
                  chainReader.readNameList( "target", chain._listTargetBone, true ) && chainReader.readBool( "ik_goal", bIKGoal, false );
            chain._bIKGoal = bIKGoal ? SW_TRUE : SW_FALSE;
            if ( ( chainReader.finish() && bOk ) == false )
                return false;
            for ( const RetargetChain& existing : _listChain )
            {
                if ( existing._name == chain._name )
                {
                    SW_LOG_ERROR( "Retarget profile '%#': duplicate chain '%#'", sourceLabel, chain._name.c_str() );
                    return false;
                }
            }
            _listChain.push_back( chain );
        }
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( RetargetProfile, RetargetProfile::kAssetType );
} // namespace sw
