#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/RetargetPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Resource/ResourcePaths.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct RetargetPanelInternal
        {
            /** @brief `RetargetTranslationMode` 순서의 표기입니다(파일 표기와 같다). */
            static constexpr const utf8* kArrTranslationMode[] = { "ScaleByPelvisHeight", "Copy", "None" };
            /** @brief 글 칸 오른쪽에 붙는 이름의 너비(96 DPI 픽셀)입니다 — 원본 · 대상 두 칸을 가른다. */
            static constexpr float32 kLabelWidth = 150.0f;

            /** @brief 쉼표 · 빈칸으로 나눈 본 이름 목록입니다. */
            static vector<hashed_string> parseNames( string_view text )
            {
                vector<hashed_string> listName;
                StringUtil::forEachToken( text, ", ", [&listName]( string_view token )
                {
                    if ( token.empty() == false )
                        listName.push_back( hashed_string( token ) );
                } );
                return listName;
            }

            /** @brief 본 이름 목록을 `a, b, c` 로 잇습니다. */
            static string joinNames( const vector<hashed_string>& listName )
            {
                string text;
                for ( const hashed_string& name : listName )
                {
                    if ( text.empty() == false )
                        text += ", ";
                    text += name.c_str();
                }
                return text;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "RetargetPanel" );
    SW_EDITOR_PANEL( RetargetPanel, RetargetPanel::kPanelID, EditorPanelCategory::Custom, 9040 );

    RetargetPanel::RetargetPanel()
        : EditorDocumentPanel{ EditorAssetType::Data, false }
        , _profile{}
        , _inspection{}
        , _listChainText{}
        , _inspectionError{}
        , _status{}
        , _sourceSkeleton{}
        , _targetSkeleton{}
        , _arrPairName{}
        , _newChainName{ "Chain" }
        , _newPairSource{}
        , _newPairTarget{}
    {
    }

    void RetargetPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "retarget" );
        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a .retarget.xml to pair the chains of two skeletons and check how they bind." );
            return;
        }
        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::CollapsingHeader( "Skeletons", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawSkeletonRows();
        if ( ImGui::CollapsingHeader( "Root and pelvis", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawRootAndPelvisRows();
        if ( ImGui::CollapsingHeader( "Options", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawOptionRows();
        if ( ImGui::CollapsingHeader( "Chains", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawChainRows();
        if ( ImGui::CollapsingHeader( "Bone map", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawBonePairRows();
        if ( ImGui::CollapsingHeader( "Check", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawInspection();
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    bool RetargetPanel::saveDocument()
    {
        // 묶이지 않는 프로필은 런타임이 쓸 수 없다 — 저장하지 않는다.
        if ( RetargetProfileInspector::inspect( _profile, _inspection, _inspectionError ) == false )
        {
            _status = "Not saved: " + _inspectionError;
            return false;
        }
        const string writePath = ResourcePaths::getWritePath( string_view( getLoadedAssetPath() ) );
        if ( writePath.empty() || FileUtil::writeTextFile( writePath, _profile.toXML() ) == false )
        {
            _status = writePath.empty() ? string( "No write path for this asset" ) : string( "The profile could not be written" );
            return false;
        }
        SW_LOG_INFO( "Retarget profile saved to %#", writePath.c_str() );
        _status = "Saved";
        return true;
    }

    bool RetargetPanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, RetargetProfile::kExtension );
    }

    ToolAssetLoadResult RetargetPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        if ( _profile.loadFromResource( path ) == false )
        {
            _status = "The retarget profile could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        syncFromProfile();
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void RetargetPanel::applyDocumentText( string_view text )
    {
        if ( _profile.parseXML( text, "retarget undo" ) )
            syncFromProfile();
    }

    void RetargetPanel::syncFromProfile()
    {
        _sourceSkeleton = string_view( _profile.getSourceSkeletonPath() );
        _targetSkeleton = string_view( _profile.getTargetSkeletonPath() );
        _arrPairName[0] = _profile.getSourceRoot().c_str();
        _arrPairName[1] = _profile.getTargetRoot().c_str();
        _arrPairName[2] = _profile.getSourcePelvis().c_str();
        _arrPairName[3] = _profile.getTargetPelvis().c_str();
        _listChainText.assign( _profile.getChains().size(), ChainText{} );
        for ( size_t chainIndex = 0; chainIndex < _profile.getChains().size(); ++chainIndex )
        {
            const RetargetChain& chain         = _profile.getChains()[chainIndex];
            _listChainText[chainIndex]._source = string_view( RetargetPanelInternal::joinNames( chain._listSourceBone ) );
            _listChainText[chainIndex]._target = string_view( RetargetPanelInternal::joinNames( chain._listTargetBone ) );
        }
        (void)RetargetProfileInspector::inspect( _profile, _inspection, _inspectionError ); // 실패는 점검 칸이 보인다
    }

    void RetargetPanel::markEdited( const utf8* pLabel )
    {
        notifyDocumentEdited( pLabel );
        syncFromProfile();
    }

    void RetargetPanel::drawSkeletonRows()
    {
        const float32 fieldWidth = -RetargetPanelInternal::kLabelWidth * EditorThemeUtil::getDpiScale();
        ImGui::SetNextItemWidth( fieldWidth );
        ImGui::InputTextWithHint( "Source skeleton", "*.skeleton.xml", _sourceSkeleton.data(), _sourceSkeleton.max_size() );
        const bool bSourceDone = ImGui::IsItemDeactivatedAfterEdit();
        EditorSelfTestMarks::note( "retarget.sourceSkeleton" );
        ImGui::SetNextItemWidth( fieldWidth );
        ImGui::InputTextWithHint( "Target skeleton", "*.skeleton.xml", _targetSkeleton.data(), _targetSkeleton.max_size() );
        const bool bTargetDone = ImGui::IsItemDeactivatedAfterEdit();
        EditorSelfTestMarks::note( "retarget.targetSkeleton" );
        if ( bSourceDone || bTargetDone )
        {
            _profile.setSkeletonPaths( _sourceSkeleton.c_str(), _targetSkeleton.c_str() );
            markEdited( "Retarget skeletons" );
        }
    }

    void RetargetPanel::drawRootAndPelvisRows()
    {
        const utf8* const arrLabel[4] = { "Root (source)", "Root (target)", "Pelvis (source)", "Pelvis (target)" };
        bool              bChanged    = false;
        for ( uint32 index = 0; index < 4; ++index )
        {
            ImGui::PushID( static_cast<int32>( index ) );
            ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
            ImGui::InputText( arrLabel[index], _arrPairName[index].data(), _arrPairName[index].max_size() );
            bChanged = ImGui::IsItemDeactivatedAfterEdit() || bChanged;
            if ( index % 2 == 0 )
                ImGui::SameLine();
            ImGui::PopID();
        }
        if ( bChanged == false )
            return;
        _profile.setRootAndPelvis( hashed_string( _arrPairName[0].c_str() ), hashed_string( _arrPairName[1].c_str() ), hashed_string( _arrPairName[2].c_str() ),
                                   hashed_string( _arrPairName[3].c_str() ), _profile.getTranslationMode() );
        markEdited( "Retarget root and pelvis" );
    }

    void RetargetPanel::drawOptionRows()
    {
        int32 mode = static_cast<int32>( _profile.getTranslationMode() );
        ImGui::SetNextItemWidth( 200.0f * EditorThemeUtil::getDpiScale() );
        if ( ImGui::Combo( "Translation", &mode, RetargetPanelInternal::kArrTranslationMode, static_cast<int32>( SW_COUNT_OF( RetargetPanelInternal::kArrTranslationMode ) ) ) )
        {
            _profile.setRootAndPelvis( _profile.getSourceRoot(), _profile.getTargetRoot(), _profile.getSourcePelvis(), _profile.getTargetPelvis(),
                                       static_cast<RetargetTranslationMode>( mode ) );
            markEdited( "Retarget translation" );
        }
        EditorWidgets::drawTooltip( "How root and pelvis movement is carried over (scaled by the pelvis height ratio, copied, or kept in place)" );
        bool bMapSameNames = _profile.isMappingSameNames();
        if ( ImGui::Checkbox( "Map same names", &bMapSameNames ) )
        {
            _profile.setMapSameNames( bMapSameNames );
            markEdited( "Retarget same names" );
        }
        ImGui::SameLine();
        float32 heightRatio = _profile.getHeightRatioOverride();
        ImGui::SetNextItemWidth( 120.0f * EditorThemeUtil::getDpiScale() );
        ImGui::DragFloat( "Height ratio (0 = pelvis)", &heightRatio, 0.01f, 0.0f, 10.0f, "%.3f" );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            _profile.setHeightRatioOverride( heightRatio );
            markEdited( "Retarget height ratio" );
        }
    }

    void RetargetPanel::drawChainRows()
    {
        vector<RetargetChain>& listChain   = _profile.getChainsForEdit();
        uint32                 removeIndex = invalid_index::kUint32;
        bool                   bChanged    = false;
        for ( uint32 chainIndex = 0; chainIndex < static_cast<uint32>( listChain.size() ) && chainIndex < _listChainText.size(); ++chainIndex )
        {
            RetargetChain& chain = listChain[chainIndex];
            ChainText&     text  = _listChainText[chainIndex];
            ImGui::PushID( static_cast<int32>( chainIndex ) );
            ImGui::SeparatorText( chain._name.c_str() );
            const float32 fieldWidth = -RetargetPanelInternal::kLabelWidth * EditorThemeUtil::getDpiScale();
            ImGui::SetNextItemWidth( fieldWidth );
            ImGui::InputTextWithHint( "Source bones", "root to end: a, b, c", text._source.data(), text._source.max_size() );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                chain._listSourceBone = RetargetPanelInternal::parseNames( text._source.c_str() );
                bChanged              = true;
            }
            ImGui::SetNextItemWidth( fieldWidth );
            ImGui::InputTextWithHint( "Target bones", "root to end: a, b, c", text._target.data(), text._target.max_size() );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                chain._listTargetBone = RetargetPanelInternal::parseNames( text._target.c_str() );
                bChanged              = true;
            }
            bool bIKGoal = chain._bIKGoal == SW_TRUE;
            if ( ImGui::Checkbox( "IK goal", &bIKGoal ) )
            {
                chain._bIKGoal = bIKGoal ? SW_TRUE : SW_FALSE;
                bChanged       = true;
            }
            EditorWidgets::drawTooltip( "Solve the end onto the source end position (feet that plant without sliding)" );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Remove" ) )
                removeIndex = chainIndex;
            ImGui::PopID();
        }
        if ( removeIndex != invalid_index::kUint32 )
        {
            listChain.erase( listChain.begin() + removeIndex );
            bChanged = true;
        }

        ImGui::SetNextItemWidth( 160.0f * EditorThemeUtil::getDpiScale() );
        ImGui::InputText( "##NewChain", _newChainName.data(), _newChainName.max_size() );
        ImGui::SameLine();
        const hashed_string newName( _newChainName.c_str() );
        bool                bNameTaken = newName.empty();
        for ( const RetargetChain& chain : listChain )
        {
            bNameTaken = bNameTaken || chain._name == newName;
        }
        ImGui::BeginDisabled( bNameTaken ); // 겹친 사슬 이름은 다시 읽을 때 오류다
        if ( ImGui::Button( "Add chain" ) )
        {
            RetargetChain chain{};
            chain._name = newName;
            listChain.push_back( chain );
            bChanged = true;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "retarget.addChain" );
        if ( bChanged )
            markEdited( "Retarget chains" );
    }

    void RetargetPanel::drawBonePairRows()
    {
        vector<RetargetBonePair>& listPair    = _profile.getBonePairsForEdit();
        uint32                    removeIndex = invalid_index::kUint32;
        for ( uint32 pairIndex = 0; pairIndex < static_cast<uint32>( listPair.size() ); ++pairIndex )
        {
            ImGui::PushID( static_cast<int32>( pairIndex ) );
            ImGui::Text( "%s -> %s", listPair[pairIndex]._source.c_str(), listPair[pairIndex]._target.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Remove" ) )
                removeIndex = pairIndex;
            ImGui::PopID();
        }
        bool bChanged = false;
        if ( removeIndex != invalid_index::kUint32 )
        {
            listPair.erase( listPair.begin() + removeIndex );
            bChanged = true;
        }
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::InputTextWithHint( "##PairSource", "source bone", _newPairSource.data(), _newPairSource.max_size() );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 140.0f * EditorThemeUtil::getDpiScale() );
        ImGui::InputTextWithHint( "##PairTarget", "target bone", _newPairTarget.data(), _newPairTarget.max_size() );
        ImGui::SameLine();
        const bool bPairComplete = _newPairSource.size() > 0 && _newPairTarget.size() > 0;
        ImGui::BeginDisabled( bPairComplete == false );
        if ( ImGui::Button( "Add pair" ) )
        {
            listPair.push_back( RetargetBonePair{ hashed_string( _newPairSource.c_str() ), hashed_string( _newPairTarget.c_str() ) } );
            bChanged = true;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "retarget.addPair" );
        if ( bChanged )
            markEdited( "Retarget bone map" );
    }

    void RetargetPanel::drawInspection()
    {
        if ( _inspectionError.empty() == false )
        {
            EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
            ImGui::TextWrapped( "%s", _inspectionError.c_str() );
            EditorThemeUtil::popTextColor();
            return;
        }
        ImGui::Text( "Binds: %u of %u target bones follow the source, height ratio %.3f", _inspection._mappedTargetBoneCount, _inspection._targetBoneCount,
                     static_cast<float64>( _inspection._heightRatio ) );
        for ( const RetargetChainInspection& chain : _inspection._listChain )
        {
            const float32 ratio = chain._sourceLength > 0.0f ? chain._targetLength / chain._sourceLength : 0.0f;
            ImGui::BulletText( "%s: source %.3f m, target %.3f m (x%.2f)", chain._name.c_str(), static_cast<float64>( chain._sourceLength ),
                               static_cast<float64>( chain._targetLength ), static_cast<float64>( ratio ) );
        }
        EditorSelfTestMarks::note( "retarget.check" );
    }
} // namespace sw::editor
