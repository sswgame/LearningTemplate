#include "pch.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "RetargetProfile" );

    namespace
    {
        struct RetargetProfileInternal
        {
            /** @brief `RetargetTranslationMode` 순서의 표기입니다. */
            static constexpr const utf8* kArrTranslationMode[] = { "ScaleByPelvisHeight", "Copy", "None" };

            /** @brief 이름 목록을 문자열 배열로 씁니다. */
            static void writeNames( const JSONValue& array, const vector<hashed_string>& listName )
            {
                array.setArray();
                for ( const hashed_string& name : listName )
                {
                    array.pushBack().setString( name.c_str() );
                }
            }

            /** @brief `{ "source", "target" }` 쌍 하나를 씁니다. */
            static void writePair( const JSONValue& object, const hashed_string& source, const hashed_string& target )
            {
                object.setObject();
                object.set( "source" ).setString( source.c_str() );
                object.set( "target" ).setString( target.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RetargetProfile::RetargetProfile()
        : _listChain{}
        , _listBonePair{}
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

    bool RetargetProfile::parseJSON( string_view json, string_view sourceLabel )
    {
        clear();
        JSONDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Retarget profile '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        clear();
        return false;
    }

    bool RetargetProfile::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Retarget profile '%#' could not be read", path );
            clear();
            return false;
        }
        return parseJSON( text, path );
    }

    string RetargetProfile::toJSON() const
    {
        using Internal = RetargetProfileInternal;
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        // 형제 핸들은 하나씩 받아 다 채운 뒤 다음 것을 받는다(같은 부모에 더하면 앞 핸들이 무효가 된다).
        if ( _sourceSkeletonPath.empty() == false )
            root.set( "source_skeleton" ).setString( _sourceSkeletonPath );
        if ( _targetSkeletonPath.empty() == false )
            root.set( "target_skeleton" ).setString( _targetSkeletonPath );
        root.set( "translation" ).setString( Internal::kArrTranslationMode[static_cast<uint32>( _translationMode )] );
        Internal::writePair( root.set( "root" ), _sourceRoot, _targetRoot );
        Internal::writePair( root.set( "pelvis" ), _sourcePelvis, _targetPelvis );
        const JSONValue chains = root.set( "chains" );
        chains.setArray();
        for ( const RetargetChain& chain : _listChain )
        {
            const JSONValue entry = chains.pushBack();
            entry.setObject();
            entry.set( "name" ).setString( chain._name.c_str() );
            Internal::writeNames( entry.set( "source" ), chain._listSourceBone );
            Internal::writeNames( entry.set( "target" ), chain._listTargetBone );
            if ( chain._bIKGoal == SW_TRUE )
                entry.set( "ik_goal" ).setBool( true );
        }
        if ( _listBonePair.empty() == false )
        {
            const JSONValue boneMap = root.set( "bone_map" );
            boneMap.setArray();
            for ( const RetargetBonePair& pair : _listBonePair )
            {
                Internal::writePair( boneMap.pushBack(), pair._source, pair._target );
            }
        }
        if ( _bMapSameNames == SW_TRUE )
            root.set( "map_same_names" ).setBool( true );
        if ( _heightRatioOverride > 0.0f )
            root.set( "height_ratio" ).setFloat( static_cast<float64>( _heightRatioOverride ) );
        return document.dump( 2 );
    }

    void RetargetProfile::setSkeletonPaths( string_view sourcePath, string_view targetPath )
    {
        _sourceSkeletonPath = string{ sourcePath };
        _targetSkeletonPath = string{ targetPath };
    }

    bool RetargetProfile::parseRoot( const JSONValue& root, string_view sourceLabel )
    {
        RigJSONReader reader( root, sourceLabel );
        hashed_string sourceSkeleton{};
        hashed_string targetSkeleton{};
        uint32        modeIndex = 0;
        bool          bOk       = reader.readName( "source_skeleton", sourceSkeleton, false ) && reader.readName( "target_skeleton", targetSkeleton, false ) &&
                   reader.readChoice( "translation", RetargetProfileInternal::kArrTranslationMode, SW_COUNT_OF( RetargetProfileInternal::kArrTranslationMode ),
                                      modeIndex, false );
        _sourceSkeletonPath = sourceSkeleton.empty() ? string{} : string{ sourceSkeleton.c_str() };
        _targetSkeletonPath = targetSkeleton.empty() ? string{} : string{ targetSkeleton.c_str() };
        _translationMode    = static_cast<RetargetTranslationMode>( modeIndex );

        const JSONValue arrPair[2]  = { reader.readObject( "root", true ), reader.readObject( "pelvis", true ) };
        const JSONValue chains      = reader.readArray( "chains", true );
        const JSONValue boneMap     = reader.readArray( "bone_map", false );
        bool            bMapSame    = false;
        float32         heightRatio = 0.0f;
        bOk                         = reader.readBool( "map_same_names", bMapSame, false ) && reader.readFloat( "height_ratio", heightRatio, false ) && bOk;
        bOk                         = reader.finish() && bOk;
        if ( bOk == false )
            return false;
        if ( root.has( "height_ratio" ) && heightRatio <= 0.0f )
        {
            SW_LOG_ERROR( "Retarget profile '%#': 'height_ratio' must be positive", sourceLabel );
            return false;
        }
        _bMapSameNames       = bMapSame ? SW_TRUE : SW_FALSE;
        _heightRatioOverride = heightRatio;

        for ( size_t index = 0; index < boneMap.size(); ++index )
        {
            RigJSONReader    entryReader( boneMap.at( index ), sourceLabel );
            RetargetBonePair pair{};
            bOk = entryReader.readName( "source", pair._source, true ) && entryReader.readName( "target", pair._target, true );
            if ( ( entryReader.finish() && bOk ) == false )
                return false;
            _listBonePair.push_back( pair );
        }

        hashed_string* arrSource[2] = { &_sourceRoot, &_sourcePelvis };
        hashed_string* arrTarget[2] = { &_targetRoot, &_targetPelvis };
        for ( uint32 pairIndex = 0; pairIndex < 2; ++pairIndex )
        {
            RigJSONReader pairReader( arrPair[pairIndex], sourceLabel );
            bOk = pairReader.readName( "source", *arrSource[pairIndex], true ) && pairReader.readName( "target", *arrTarget[pairIndex], true );
            if ( ( pairReader.finish() && bOk ) == false )
                return false;
        }

        for ( size_t index = 0; index < chains.size(); ++index )
        {
            RigJSONReader chainReader( chains.at( index ), sourceLabel );
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
