#include "pch.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct RetargetProfileInternal
        {
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
                XMLFields::writeFields( element, RetargetBonePair{ source, target } );
            }

            /** @brief 열쇠 있는 목록의 규칙 하나입니다. */
            static DataInheritanceSchema makeKeyedList( const utf8* pName, const utf8* pKey )
            {
                DataInheritanceSchema list{ pName };
                list._keyAttribute = pKey;
                return list;
            }

            /**
             * @brief 상속 규칙입니다. `root` · `pelvis` 는 구조체 칸, 사슬은 이름, 뼈 짝은 원본 뼈, 자세는 뼈가 열쇠입니다.
             * @details 사슬의 뼈 목록은 열쇠가 없어 통째로 바뀐다(순서가 뜻이다). 같은 원본에 대상 뼈 이름만 다른 스켈레톤의 프로필이 차이 파일 하나다.
             */
            static DataInheritanceSchema makeInheritanceSchema()
            {
                DataInheritanceSchema schema{ "RetargetProfile" };
                schema._listField.emplace_back( "root" );
                schema._listField.emplace_back( "pelvis" );
                schema._listField.push_back( makeKeyedList( "chains", "name" ) );
                schema._listField.push_back( makeKeyedList( "bone_map", "source" ) );
                schema._listField.push_back( makeKeyedList( "target_pose", "bone" ) );
                return schema;
            }
        };
        SW_DATA_INHERITANCE_SCHEMA( RetargetProfile, &RetargetProfileInternal::makeInheritanceSchema ); // 팩 쿠커가 루트 이름으로 찾는다
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
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xml, sourceLabel, ResourceContext::forSchema( &RetargetProfileInternal::makeInheritanceSchema ), flatText, bComposed ) == false )
            return false;
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, flatText.empty() ? xml : string_view{ flatText }, sourceLabel, "RetargetProfile", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "RetargetProfile.RetargetProfileMalformedXML", "Retarget profile '%#': malformed XML (expected a <RetargetProfile> root)", sourceLabel );
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
            SW_LOG_ERROR_ID( "RetargetProfile.RetargetProfileCouldNot", "Retarget profile '%#' could not be read", path );
            clear();
            return false;
        }
        return parseXML( text, path );
    }

    string RetargetProfile::toXML() const
    {
        using Internal = RetargetProfileInternal;
        XMLDocument               document;
        const XMLNode             root = document.appendRoot( "RetargetProfile" );
        RetargetProfileRootFields rootFields;
        rootFields._sourceSkeleton = _sourceSkeletonPath;
        rootFields._targetSkeleton = _targetSkeletonPath;
        rootFields._translation    = _translationMode;
        rootFields._bMapSameNames  = _bMapSameNames == SW_TRUE;
        rootFields._heightRatio    = _heightRatioOverride;
        XMLFields::writeFields( root, rootFields ); // 기본값과 같은 칸은 적지 않는다
        Internal::writePair( root.appendChild( "root" ), _sourceRoot, _targetRoot );
        Internal::writePair( root.appendChild( "pelvis" ), _sourcePelvis, _targetPelvis );
        const XMLNode chains = root.appendChild( "chains" );
        for ( const RetargetChain& chain : _listChain )
        {
            const XMLNode       entry = chains.appendChild( "item" );
            RetargetChainFields chainFields;
            chainFields._name    = chain._name;
            chainFields._bIKGoal = chain._bIKGoal == SW_TRUE;
            XMLFields::writeFields( entry, chainFields );
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
                XMLFields::writeFields( targetPose.appendChild( "item" ), RetargetPoseBoneFields{ poseBone._bone, poseBone._rotationDegrees } );
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
        RigXMLReader              reader( root, sourceLabel );
        RetargetProfileRootFields rootFields;
        bool                      bOk = reader.readFields( rootFields ); // 높이 비 > 0(적었을 때)은 PROPERTY( Min ) 가 본다
        _sourceSkeletonPath           = rootFields._sourceSkeleton;
        _targetSkeletonPath           = rootFields._targetSkeleton;
        _translationMode              = rootFields._translation;

        const XMLNode    arrPair[2] = { reader.readObject( "root", true ), reader.readObject( "pelvis", true ) };
        const RigXMLList chains     = reader.readArray( "chains", true );
        const RigXMLList boneMap    = reader.readArray( "bone_map", false );
        const RigXMLList targetPose = reader.readArray( "target_pose", false );
        bOk                         = reader.finish() && bOk;
        if ( bOk == false )
            return false;
        _bMapSameNames       = rootFields._bMapSameNames ? SW_TRUE : SW_FALSE;
        _heightRatioOverride = rootFields._heightRatio;

        for ( size_t index = 0; boneMap.isValid() && index < boneMap.size(); ++index )
        {
            RigXMLReader     entryReader( boneMap.at( index ), sourceLabel );
            RetargetBonePair pair{};
            bOk = entryReader.readFields( pair );
            if ( ( entryReader.finish() && bOk ) == false )
                return false;
            _listBonePair.push_back( pair );
        }

        for ( size_t index = 0; targetPose.isValid() && index < targetPose.size(); ++index )
        {
            RigXMLReader           entryReader( targetPose.at( index ), sourceLabel );
            RetargetPoseBoneFields poseFields;
            bOk = entryReader.readFields( poseFields );
            const RetargetPoseBone poseBone{ poseFields._bone, poseFields._rotation };
            if ( ( entryReader.finish() && bOk ) == false )
                return false;
            for ( const RetargetPoseBone& existing : _listTargetPose )
            {
                if ( existing._bone == poseBone._bone )
                {
                    SW_LOG_ERROR_ID( "RetargetProfile.RetargetProfileDuplicateTarget", "Retarget profile '%#': duplicate target pose bone '%#'", sourceLabel, poseBone._bone.c_str() );
                    return false;
                }
            }
            _listTargetPose.push_back( poseBone );
        }

        hashed_string* arrSource[2] = { &_sourceRoot, &_sourcePelvis };
        hashed_string* arrTarget[2] = { &_targetRoot, &_targetPelvis };
        for ( uint32 pairIndex = 0; pairIndex < 2; ++pairIndex )
        {
            RigXMLReader     pairReader( arrPair[pairIndex], sourceLabel );
            RetargetBonePair pair{};
            bOk = pairReader.readFields( pair );
            if ( ( pairReader.finish() && bOk ) == false )
                return false;
            *arrSource[pairIndex] = pair._source;
            *arrTarget[pairIndex] = pair._target;
        }

        for ( size_t index = 0; index < chains.size(); ++index )
        {
            RigXMLReader        chainReader( chains.at( index ), sourceLabel );
            RetargetChain       chain{};
            RetargetChainFields chainFields;
            bOk            = chainReader.readFields( chainFields );
            bOk            = chainReader.readNameList( "source", chain._listSourceBone, true ) && bOk;
            bOk            = chainReader.readNameList( "target", chain._listTargetBone, true ) && bOk;
            chain._name    = chainFields._name;
            chain._bIKGoal = chainFields._bIKGoal ? SW_TRUE : SW_FALSE;
            if ( ( chainReader.finish() && bOk ) == false )
                return false;
            for ( const RetargetChain& existing : _listChain )
            {
                if ( existing._name == chain._name )
                {
                    SW_LOG_ERROR_ID( "RetargetProfile.RetargetProfileDuplicateChain", "Retarget profile '%#': duplicate chain '%#'", sourceLabel, chain._name.c_str() );
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
