#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterEditorPackData.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw::editor
{
    SW_LOG_CALLER( "CharacterEditorPackData" );

    namespace
    {
        struct CharacterEditorPackDataInternal
        {
            static constexpr const utf8* kRootName          = "CharacterEditor";
            static constexpr const utf8* kSkeletonExtension = ".skeleton.json";
            static constexpr const utf8* kRefPoseExtension  = ".refpose.xml";

            /**
             * @brief 레퍼런스 포즈 경로를 스켈레톤 경로에서 만듭니다(`a.skeleton.json` → `a.refpose.xml`).
             * @details 레퍼런스 포즈는 창이 저장하는 스켈레톤의 짝 파일이라 아직 없을 수 있다 — 데이터에 따로 적으면 없는 파일을 가리키는 칸이 생기므로 스켈레톤 하나를 정본으로 둔다.
             *          스켈레톤이 `.skeleton.json` 으로 끝나지 않으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool makeRefPosePath( const string& skeletonPath, string& outValue, string_view sourceName )
            {
                const string_view skeletonExtension{ kSkeletonExtension };
                if ( StringUtil::endsWith( skeletonPath, skeletonExtension ) == false )
                {
                    SW_LOG_ERROR( "%#: skeleton '%#' must end with '%#' (the reference pose is named after it)", sourceName, skeletonPath.c_str(), kSkeletonExtension );
                    return false;
                }
                outValue = skeletonPath.substr( 0, skeletonPath.size() - skeletonExtension.size() ) + kRefPoseExtension;
                return true;
            }

            /** @brief 속성 @p pName 을 필수 칸으로 읽습니다. 없거나 비면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readRequired( const XMLNode& root, const utf8* pName, string& outValue, string_view sourceName )
            {
                const string_view text = root.getAttributeText( pName );
                if ( text.empty() )
                {
                    SW_LOG_ERROR( "%#: <%#> needs the '%#' attribute", sourceName, kRootName, pName );
                    return false;
                }
                outValue = string( text );
                return true;
            }
        };
    } // namespace

    /** @brief 캐릭터 편집 데이터의 종류입니다(게임 팩의 `data/charactereditor.xml` — 스키마 테스트가 같은 로더로 읽는다). */
    SW_RESOURCE_DATA_KIND( CharacterEditorPackData, CharacterEditorPackData::kDataKind, "game/", nullptr, "/data/charactereditor.xml",
                           &ResourceDataKindUtil::loadCatalog<CharacterEditorPackData> );
} // namespace sw::editor

namespace sw::editor
{
    bool CharacterEditorPackData::loadFromResource( string_view path )
    {
        *this = CharacterEditorPackData{};
        if ( GameDataXML::loadFile( *this, &CharacterEditorPackData::loadRoot, path, CharacterEditorPackDataInternal::kRootName ) )
            return true;
        *this = CharacterEditorPackData{};
        return false;
    }

    bool CharacterEditorPackData::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = CharacterEditorPackData{};
        if ( GameDataXML::loadText( *this, &CharacterEditorPackData::loadRoot, xmlText, sourceName, CharacterEditorPackDataInternal::kRootName ) )
            return true;
        *this = CharacterEditorPackData{};
        return false;
    }

    bool CharacterEditorPackData::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Internal = CharacterEditorPackDataInternal;
        bool bOk       = true;
        for ( XMLAttribute attribute = root.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            const utf8* pName  = attribute.getName();
            const bool  bKnown = StringUtil::equals( pName, "sockets" ) || StringUtil::equals( pName, "skeleton" ) || StringUtil::equals( pName, "items" ) || StringUtil::equals( pName, "appearance" ) ||
                                StringUtil::equals( pName, "previewPreset" );
            if ( bKnown == false )
            {
                SW_LOG_ERROR( "%#: unknown attribute '%#' on <%#>", sourceName, pName, Internal::kRootName );
                bOk = false;
            }
        }
        if ( root.findChild().isValid() )
        {
            SW_LOG_ERROR( "%#: <%#> takes attributes only", sourceName, Internal::kRootName );
            bOk = false;
        }
        bOk = Internal::readRequired( root, "sockets", _socketPath, sourceName ) && bOk;
        bOk = Internal::readRequired( root, "skeleton", _skeletonPath, sourceName ) && bOk;
        bOk = Internal::readRequired( root, "items", _itemsPath, sourceName ) && bOk;
        bOk = Internal::readRequired( root, "appearance", _appearanceFolder, sourceName ) && bOk;
        bOk = Internal::readRequired( root, "previewPreset", _previewPresetID, sourceName ) && bOk;
        if ( _skeletonPath.empty() == false )
            bOk = Internal::makeRefPosePath( _skeletonPath, _refPosePath, sourceName ) && bOk;
        return bOk;
    }
} // namespace sw::editor
