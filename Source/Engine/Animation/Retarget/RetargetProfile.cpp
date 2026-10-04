#include "pch.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "RetargetProfile" );

    namespace
    {
        struct RetargetProfileInternal
        {
            /** @brief `RetargetTranslationMode` 순서의 표기입니다. */
            static constexpr const utf8* kArrTranslationMode[] = { "ScaleByPelvisHeight", "Copy", "None" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    RetargetProfile::RetargetProfile()
        : _listChain{}
        , _sourceSkeletonPath{}
        , _targetSkeletonPath{}
        , _sourceRoot{}
        , _targetRoot{}
        , _sourcePelvis{}
        , _targetPelvis{}
        , _translationMode{ RetargetTranslationMode::ScaleByPelvisHeight }
    {
    }

    void RetargetProfile::clear()
    {
        _listChain.clear();
        _sourceSkeletonPath.clear();
        _targetSkeletonPath.clear();
        _sourceRoot      = hashed_string{};
        _targetRoot      = hashed_string{};
        _sourcePelvis    = hashed_string{};
        _targetPelvis    = hashed_string{};
        _translationMode = RetargetTranslationMode::ScaleByPelvisHeight;
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

    bool RetargetProfile::parseJson( string_view json, string_view sourceLabel )
    {
        clear();
        JsonDocument document;
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
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Retarget profile '%#' could not be read", path );
            clear();
            return false;
        }
        return parseJson( text, path );
    }

    bool RetargetProfile::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        RigJsonReader reader( root, sourceLabel );
        hashed_string sourceSkeleton{};
        hashed_string targetSkeleton{};
        uint32        modeIndex = 0;
        bool          bOk       = reader.readName( "source_skeleton", sourceSkeleton, false ) && reader.readName( "target_skeleton", targetSkeleton, false ) &&
                   reader.readChoice( "translation", RetargetProfileInternal::kArrTranslationMode, SW_COUNT_OF( RetargetProfileInternal::kArrTranslationMode ),
                                      modeIndex, false );
        _sourceSkeletonPath = sourceSkeleton.empty() ? string{} : string{ sourceSkeleton.c_str() };
        _targetSkeletonPath = targetSkeleton.empty() ? string{} : string{ targetSkeleton.c_str() };
        _translationMode    = static_cast<RetargetTranslationMode>( modeIndex );

        const JsonValue arrPair[2] = { reader.readObject( "root", true ), reader.readObject( "pelvis", true ) };
        const JsonValue chains     = reader.readArray( "chains", true );
        bOk                        = reader.finish() && bOk;
        if ( bOk == false )
            return false;

        hashed_string* arrSource[2] = { &_sourceRoot, &_sourcePelvis };
        hashed_string* arrTarget[2] = { &_targetRoot, &_targetPelvis };
        for ( uint32 pairIndex = 0; pairIndex < 2; ++pairIndex )
        {
            RigJsonReader pairReader( arrPair[pairIndex], sourceLabel );
            bOk = pairReader.readName( "source", *arrSource[pairIndex], true ) && pairReader.readName( "target", *arrTarget[pairIndex], true );
            if ( ( pairReader.finish() && bOk ) == false )
                return false;
        }

        for ( size_t index = 0; index < chains.size(); ++index )
        {
            RigJsonReader chainReader( chains.at( index ), sourceLabel );
            RetargetChain chain{};
            bool          bIkGoal = false;
            bOk                   = chainReader.readName( "name", chain._name, true ) && chainReader.readNameList( "source", chain._listSourceBone, true ) &&
                  chainReader.readNameList( "target", chain._listTargetBone, true ) && chainReader.readBool( "ik_goal", bIkGoal, false );
            chain._bIkGoal = bIkGoal ? SW_TRUE : SW_FALSE;
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
