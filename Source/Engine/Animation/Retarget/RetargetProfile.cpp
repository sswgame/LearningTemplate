#include "pch.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
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
                XMLFields::writeProperties( element, RetargetBonePair{ source, target } );
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
        , _baseID{}
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
        _baseID.clear();
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

    bool RetargetProfile::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        return loadComposed( xml, sourceLabel, ResourceContext::forSchema( &RetargetProfileInternal::makeInheritanceSchema ) );
    }

    bool RetargetProfile::loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context )
    {
        clear();
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xml, sourceLabel, context, flatText, bComposed ) == false )
            return false;
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, flatText.empty() ? xml : string_view{ flatText }, sourceLabel, "RetargetProfile", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "RetargetProfile.RetargetProfileMalformedXML", "Retarget profile '%#': malformed XML (expected a <RetargetProfile> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
        {
            _baseID = DataInheritance::findBasePath( xml ); // 저장이 부모와의 차이로 쓰려고 든다(읽은 값은 펼친 한 벌이다)
            return true;
        }
        clear();
        return false;
    }

    bool RetargetProfile::saveToFile( const AbsolutePath& absPath ) const
    {
        const string fullText = toXML();
        string       text     = fullText;
        if ( _baseID.empty() == false )
        {
            RetargetProfile parent;
            if ( parent.loadFromAsset( AssetID{ _baseID } ) == false )
            {
                DataInheritance::reportBaseNotFound( _baseID, absPath.str() );
                return false;
            }
            return ResourceComposer::saveDelta( absPath.str(), fullText, parent.toXML(), RetargetProfileInternal::makeInheritanceSchema(), _baseID, [&absPath]( const string& deltaText )
            {
                RetargetProfile reread;
                return reread.loadFromXMLText( deltaText, absPath.str() ) ? reread.toXML() : string{};
            } );
        }
        if ( ResourceComposer::saveText( absPath.str(), text ) == false )
        {
            SW_LOG_ERROR_ID( "RetargetProfile.WriteFailed", "Retarget profile could not be written: %#", absPath.str() );
            return false;
        }
        return true;
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
        return loadComposed( text, path, ResourceContext::forSchema( &RetargetProfileInternal::makeInheritanceSchema ).withAsset( assetID ) );
    }

    string RetargetProfile::toXML() const
    {
        using Internal = RetargetProfileInternal;
        XMLDocument               document;
        const XMLNode             root = document.appendRoot( "RetargetProfile" );
        RetargetProfileRootParams rootParams;
        rootParams._sourceSkeleton = _sourceSkeletonPath;
        rootParams._targetSkeleton = _targetSkeletonPath;
        rootParams._translation    = _translationMode;
        rootParams._bMapSameNames  = _bMapSameNames == SW_TRUE;
        rootParams._heightRatio    = _heightRatioOverride;
        XMLFields::writeProperties( root, rootParams ); // 기본값과 같은 칸은 적지 않는다
        // 아직 고르지 않은 쌍(새 문서)은 적지 않는다 — 적으면 빈 이름이 필수 칸 검사에 걸려 되돌리기 스냅샷이 다시 읽히지 않는다. 쓰는 쪽(`PoseRetargeter`)이 없는 뼈를 오류로 낸다.
        if ( _sourceRoot.empty() == false || _targetRoot.empty() == false )
            Internal::writePair( root.appendChild( "root" ), _sourceRoot, _targetRoot );
        if ( _sourcePelvis.empty() == false || _targetPelvis.empty() == false )
            Internal::writePair( root.appendChild( "pelvis" ), _sourcePelvis, _targetPelvis );
        const XMLNode chains = root.appendChild( "chains" );
        for ( const RetargetChain& chain : _listChain )
        {
            const XMLNode       entry = chains.appendChild( "item" );
            RetargetChainParams chainParams;
            chainParams._name    = chain._name;
            chainParams._bIKGoal = chain._bIKGoal == SW_TRUE;
            XMLFields::writeProperties( entry, chainParams );
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
                XMLFields::writeProperties( targetPose.appendChild( "item" ), RetargetPoseBoneParams{ poseBone._bone, poseBone._rotationDegrees } );
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
        RetargetProfileRootParams rootParams;
        bool                      bOk = reader.readProperties( rootParams ); // 높이 비 > 0(적었을 때)은 PROPERTY( Min ) 가 본다
        _sourceSkeletonPath           = rootParams._sourceSkeleton;
        _targetSkeletonPath           = rootParams._targetSkeleton;
        _translationMode              = rootParams._translation;

        const XMLNode    arrPair[2] = { reader.readObject( "root", false ), reader.readObject( "pelvis", false ) }; // 없으면 아직 고르지 않은 쌍이다(새 문서)
        const RigXMLList chains     = reader.readArray( "chains", true );
        const RigXMLList boneMap    = reader.readArray( "bone_map", false );
        const RigXMLList targetPose = reader.readArray( "target_pose", false );
        bOk                         = reader.finish() && bOk;
        if ( bOk == false )
            return false;
        _bMapSameNames       = rootParams._bMapSameNames ? SW_TRUE : SW_FALSE;
        _heightRatioOverride = rootParams._heightRatio;

        for ( size_t index = 0; boneMap.isValid() && index < boneMap.size(); ++index )
        {
            RigXMLReader     entryReader( boneMap.at( index ), sourceLabel );
            RetargetBonePair pair{};
            bOk = entryReader.readProperties( pair );
            if ( ( entryReader.finish() && bOk ) == false )
                return false;
            _listBonePair.push_back( pair );
        }

        for ( size_t index = 0; targetPose.isValid() && index < targetPose.size(); ++index )
        {
            RigXMLReader           entryReader( targetPose.at( index ), sourceLabel );
            RetargetPoseBoneParams poseParams;
            bOk = entryReader.readProperties( poseParams );
            const RetargetPoseBone poseBone{ poseParams._bone, poseParams._rotation };
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
            if ( arrPair[pairIndex].isValid() == false )
                continue;
            RigXMLReader     pairReader( arrPair[pairIndex], sourceLabel );
            RetargetBonePair pair{};
            bOk = pairReader.readProperties( pair );
            if ( ( pairReader.finish() && bOk ) == false )
                return false;
            *arrSource[pairIndex] = pair._source;
            *arrTarget[pairIndex] = pair._target;
        }

        for ( size_t index = 0; index < chains.size(); ++index )
        {
            RigXMLReader        chainReader( chains.at( index ), sourceLabel );
            RetargetChain       chain{};
            RetargetChainParams chainParams;
            bOk            = chainReader.readProperties( chainParams );
            bOk            = chainReader.readNameList( "source", chain._listSourceBone, true ) && bOk;
            bOk            = chainReader.readNameList( "target", chain._listTargetBone, true ) && bOk;
            chain._name    = chainParams._name;
            chain._bIKGoal = chainParams._bIKGoal ? SW_TRUE : SW_FALSE;
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
    SW_SHARED_ASSET_CACHE( RetargetProfile, RetargetProfile, RetargetProfile::kAssetType.withLoader( AssetTypeLoader::of<RetargetProfile>( MemoryTag::Animation ) ) );
} // namespace sw
