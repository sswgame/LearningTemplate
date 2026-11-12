#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorPackData.h"

#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogRoot.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorPackDataInternal
        {
            static constexpr const utf8* kRootName          = "CharacterEditor";
            static constexpr const utf8* kSkeletonExtension = ".skeleton.xml";
            static constexpr const utf8* kRefPoseExtension  = ".refpose.xml";

            /**
             * @brief 레퍼런스 포즈 경로를 스켈레톤 경로에서 만듭니다(`a.skeleton.xml` → `a.refpose.xml`).
             * @details 레퍼런스 포즈는 창이 저장하는 스켈레톤의 짝 파일이라 아직 없을 수 있다 — 데이터에 따로 적으면 없는 파일을 가리키는 칸이 생기므로 스켈레톤 하나를 기준으로 둔다.
             *          스켈레톤이 `.skeleton.xml` 으로 끝나지 않으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool makeRefPosePath( const string& skeletonPath, string& outValue, string_view sourceName )
            {
                const string_view skeletonExtension{ kSkeletonExtension };
                if ( StringUtil::endsWith( skeletonPath, skeletonExtension ) == false )
                {
                    SW_LOG_ERROR_ID( "CharacterEditorPackData.SkeletonMustEndReference", "%#: skeleton '%#' must end with '%#' (the reference pose is named after it)", sourceName, skeletonPath.c_str(), kSkeletonExtension );
                    return false;
                }
                outValue = skeletonPath.substr( 0, skeletonPath.size() - skeletonExtension.size() ) + kRefPoseExtension;
                return true;
            }
        };
    } // namespace

    /** @brief 캐릭터 편집 데이터의 종류입니다(게임 팩의 `data/charactereditor.xml` — 스키마 테스트가 같은 로더로 읽는다). */
    SW_ASSET_TYPE( CharacterEditorPackData, AssetTypeDef::makeDataFile( CharacterEditorPackData::kDataKind, AssetPathRule{ "game/", {}, "/data/charactereditor.xml" }, AssetTypeLoader::of<CharacterEditorPackData>() ) );
} // namespace sw::editor

namespace sw::editor
{
    bool CharacterEditorPackData::loadFromAsset( const AssetID& assetID )
    {
        *this = CharacterEditorPackData{};
        if ( GameDataXML::loadFromAsset( *this, &CharacterEditorPackData::loadRoot, assetID, CharacterEditorPackDataInternal::kRootName ) )
            return true;
        *this = CharacterEditorPackData{};
        return false;
    }

    bool CharacterEditorPackData::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = CharacterEditorPackData{};
        if ( GameDataXML::loadFromXMLText( *this, &CharacterEditorPackData::loadRoot, xmlText, sourceName, CharacterEditorPackDataInternal::kRootName ) )
            return true;
        *this = CharacterEditorPackData{};
        return false;
    }

    bool CharacterEditorPackData::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        using Internal = CharacterEditorPackDataInternal;
        bool bOk       = true;
        bOk            = root.readProperties( *this, sourceName ) && bOk;
        if ( root.getRawNode().findChild().isValid() ) // 손 읽기: 자식이 하나라도 있으면 틀린 파일이다(읽을 원소 표가 없다)
        {
            SW_LOG_ERROR_ID( "CharacterEditorPackData.TakesAttributesOnly", "%#: <%#> takes attributes only", sourceName, Internal::kRootName );
            bOk = false;
        }
        if ( _skeletonPath.empty() == false )
            bOk = Internal::makeRefPosePath( _skeletonPath, _refPosePath, sourceName ) && bOk;
        return bOk;
    }
} // namespace sw::editor
