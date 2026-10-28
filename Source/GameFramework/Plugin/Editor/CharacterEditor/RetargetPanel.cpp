#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/RetargetPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Asset/AssetIO.h"
#include "Engine/Asset/AssetPaths.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

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
            /** @brief 미리보기 뷰포트 높이(96 DPI 픽셀)입니다. */
            static constexpr float32 kPreviewHeight = 300.0f;
            /** @brief 미리보기 카메라가 담는 구의 여유 배율입니다. */
            static constexpr float32 kFramePadding = 1.15f;
            /** @brief 리타깃 포즈 돌림을 끄는 속도(도 / 픽셀)입니다. */
            static constexpr float32 kPoseDragSpeed = 0.5f;
            /** @brief 리타깃 포즈 돌림의 범위(도)입니다. */
            static constexpr float32 kPoseDegreeLimit = 180.0f;

            /** @brief 미리보기 선 색입니다 — 원본(파랑) · 대상 레퍼런스(회색, 리타깃 포즈가 있을 때만 긋는다) · 리타깃 포즈를 얹은 대상(주황). */
            static constexpr float4 kSourceColor{ 0.3f, 0.6f, 1.0f, 1.0f };
            static constexpr float4 kTargetReferenceColor{ 0.6f, 0.6f, 0.6f, 0.6f };
            static constexpr float4 kTargetPoseColor{ 1.0f, 0.55f, 0.2f, 1.0f };

            /** @brief 감시 값이 읽는 값 하나입니다 — 패널 인스턴스가 게시한 값 중 @p pField 를 @p outValue 에 씁니다. 패널이 없으면 false 입니다. */
            [[nodiscard]] static bool readPublished( uint32 RetargetPanel::Published::* pField, float64& outValue )
            {
                const EditorPanelManager* pManager = editor::findSubsystem<EditorPanelManager>();
                const RetargetPanel*      pPanel   = pManager != nullptr ? pManager->findPanel<RetargetPanel>() : nullptr;
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPublished().*pField );
                return true;
            }

            /**
             * @brief 접을 수 있는 구역 머리글입니다(처음에는 열려 있다). 열렸으면 true 입니다.
             * @details 머리글마다 표식 `retarget.section.<제목>` 을 남긴다 — 구역이 쌓여 창 아래 위젯이 보이는 영역 밖으로 밀리면 시나리오가 위 구역을 눌러 접고 닿게 한다.
             */
            [[nodiscard]] static bool drawSection( const utf8* pTitle )
            {
                const bool bOpen = ImGui::CollapsingHeader( pTitle, ImGuiTreeNodeFlags_DefaultOpen );
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( string( "retarget.section." ) + pTitle ).c_str() );
                return bOpen;
            }

            [[nodiscard]] static bool readTargetPoseCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &RetargetPanel::Published::_targetPoseCount, outValue );
            }
            [[nodiscard]] static bool readPreviewSegmentCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &RetargetPanel::Published::_previewSegmentCount, outValue );
            }
            [[nodiscard]] static bool readChainCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &RetargetPanel::Published::_chainCount, outValue );
            }

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

            /** @brief 자세의 모델 공간 본 위치를 스켈레톤 순서로 모읍니다. */
            static void computeBonePositions( const Skeleton& skeleton, const Pose& pose, vector<float3>& outListPosition )
            {
                vector<float4x4> listModel;
                pose.computeModelSpace( skeleton.getParentIndices(), listModel );
                outListPosition.clear();
                for ( const float4x4& model : listModel )
                {
                    outListPosition.push_back( model.getTranslation() );
                }
            }

            /** @brief 본마다 부모까지 선을 긋습니다(위치는 스켈레톤 순서). */
            static void drawBones( EditorProjectedLineRenderer& renderer, const Skeleton& skeleton, const vector<float3>& listPosition, const float4& color )
            {
                const vector<int32>& listParent = skeleton.getParentIndices();
                for ( size_t boneIndex = 0; boneIndex < listPosition.size() && boneIndex < listParent.size(); ++boneIndex )
                {
                    const int32 parent = listParent[boneIndex];
                    if ( parent >= 0 && static_cast<size_t>( parent ) < listPosition.size() )
                        renderer.drawLine( listPosition[static_cast<size_t>( parent )], listPosition[boneIndex], color );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "RetargetPanel" );
    SW_EDITOR_PANEL( RetargetPanel, RetargetPanel::kPanelID, EditorPanelCategory::Custom, 9040 );

    SW_AUTOMATION_WATCH( editorRetargetPoseCount, "Editor.RetargetPoseCount", "Retarget pose rows (target bone turns) in the profile the Retarget Profile window has open",
                         &RetargetPanelInternal::readTargetPoseCount );
    SW_AUTOMATION_WATCH( editorRetargetPreviewSegments, "Editor.RetargetPreviewSegments", "Skeleton line segments the Retarget Profile window's 3D preview drew last frame",
                         &RetargetPanelInternal::readPreviewSegmentCount );
    SW_AUTOMATION_WATCH( editorRetargetChainCount, "Editor.RetargetChainCount", "Chains in the profile the Retarget Profile window has open", &RetargetPanelInternal::readChainCount );

    RetargetPanel::RetargetPanel()
        : EditorPreviewPanel{ EditorAssetType::Data, false }
        , _profile{}
        , _inspection{}
        , _previewSource{}
        , _previewTarget{}
        , _listSourcePosition{}
        , _listTargetReferencePosition{}
        , _listTargetPosePosition{}
        , _listChainText{}
        , _inspectionError{}
        , _status{}
        , _sourceSkeleton{}
        , _targetSkeleton{}
        , _newDocumentPath{}
        , _arrPairName{}
        , _newChainName{ "Chain" }
        , _newPairSource{}
        , _newPairTarget{}
        , _newPoseBone{}
        , _published{}
        , _bPreviewSourceReady{ SW_FALSE }
        , _bPreviewTargetReady{ SW_FALSE }
        , _bFramePending{ SW_FALSE }
    {
    }

    void RetargetPanel::drawDocumentContent()
    {
        beginPreviewFrame();
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawNewDocumentRow();
        drawDocumentOpenBar( "retarget" );
        _published._targetPoseCount = static_cast<uint32>( _profile.getTargetPose().size() );
        _published._chainCount      = static_cast<uint32>( _profile.getChains().size() );
        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a .retarget.xml, or type a new path and press New, to pair the chains of two skeletons and check how they bind." );
            return;
        }
        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        using Internal = RetargetPanelInternal;
        if ( Internal::drawSection( "Skeletons" ) )
            drawSkeletonRows();
        if ( Internal::drawSection( "Preview" ) )
            drawPreviewSection();
        if ( Internal::drawSection( "Root and pelvis" ) )
            drawRootAndPelvisRows();
        if ( Internal::drawSection( "Options" ) )
            drawOptionRows();
        if ( Internal::drawSection( "Chains" ) )
            drawChainRows();
        if ( Internal::drawSection( "Retarget pose" ) )
            drawTargetPoseRows();
        if ( Internal::drawSection( "Bone map" ) )
            drawBonePairRows();
        if ( Internal::drawSection( "Check" ) )
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
        const string writePath = AssetPaths::getWritePath( string_view( getLoadedAssetPath() ) );
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
        if ( AssetIO::hasResource( AssetID{ path } ) == false )
        {
            // 없는 경로는 새 프로필이다 — 빈 문서로 시작하고 저장하면 파일을 만든다(스켈레톤 · 뿌리와 골반이 묶이기 전에는 저장되지 않는다).
            _profile.clear();
            syncFromProfile();
            _status = "New profile - set both skeletons and the root and pelvis pairs, then save";
            return ToolAssetLoadResult::Missing;
        }
        if ( _profile.loadFromAsset( AssetID{ path } ) == false )
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
        reloadPreviewSkeletons();
    }

    void RetargetPanel::reloadPreviewSkeletons()
    {
        _bPreviewSourceReady = _profile.getSourceSkeletonPath().empty() == false && _previewSource.loadFromAsset( AssetID{ _profile.getSourceSkeletonPath() } ) ? SW_TRUE : SW_FALSE;
        _bPreviewTargetReady = _profile.getTargetSkeletonPath().empty() == false && _previewTarget.loadFromAsset( AssetID{ _profile.getTargetSkeletonPath() } ) ? SW_TRUE : SW_FALSE;
        _bFramePending       = SW_TRUE;
        rebuildPreviewPoses();
    }

    void RetargetPanel::rebuildPreviewPoses()
    {
        _listSourcePosition.clear();
        _listTargetReferencePosition.clear();
        _listTargetPosePosition.clear();
        if ( _bPreviewSourceReady == SW_TRUE )
        {
            Pose sourcePose;
            sourcePose.setToReference( _previewSource );
            RetargetPanelInternal::computeBonePositions( _previewSource, sourcePose, _listSourcePosition );
        }
        if ( _bPreviewTargetReady == SW_FALSE )
            return;
        Pose targetPose;
        targetPose.setToReference( _previewTarget );
        RetargetPanelInternal::computeBonePositions( _previewTarget, targetPose, _listTargetReferencePosition );
        // 리타기터와 같은 길로 리타깃 포즈를 얹는다. 없는 본을 적었으면 얹지 못하고(오류는 로그) 레퍼런스 그대로 보인다.
        if ( RetargetPoseUtil::applyTargetPose( _profile, _previewTarget, targetPose ) == false )
            targetPose.setToReference( _previewTarget );
        RetargetPanelInternal::computeBonePositions( _previewTarget, targetPose, _listTargetPosePosition );
    }

    void RetargetPanel::markEdited( const utf8* pLabel )
    {
        notifyDocumentEdited( pLabel );
        syncFromProfile();
    }

    void RetargetPanel::drawNewDocumentRow()
    {
        EditorChrome::setNextItemWidthColumns( 48.0f );
        ImGui::InputTextWithHint( "##NewRetargetPath", "<resource id>.retarget.xml", _newDocumentPath.data(), _newDocumentPath.max_size() );
        EditorSelfTestMarks::note( "retarget.newPath" );
        ImGui::SameLine();
        const bool bPathValid = acceptsDocumentPath( string_view( _newDocumentPath.c_str() ) );
        ImGui::BeginDisabled( bPathValid == false );
        if ( ImGui::Button( "New" ) )
            requestOpenDocument( string_view( _newDocumentPath.c_str() ) ); // 없는 파일이면 `loadDocument` 가 빈 문서로 연다
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "retarget.new" );
        EditorWidgets::drawTooltip( "Start an empty profile at this resource id (an existing file is opened instead)" );
    }

    void RetargetPanel::drawSkeletonRows()
    {
        const float32 fieldWidth = -RetargetPanelInternal::kLabelWidth * EditorThemeUtil::getDPIScale();
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

    void RetargetPanel::drawPreviewSection()
    {
        if ( _bPreviewSourceReady == SW_FALSE && _bPreviewTargetReady == SW_FALSE )
        {
            EditorWidgets::drawEmptyHint( "Set the source and target skeletons to see them overlaid here." );
            return;
        }
        if ( _bFramePending == SW_TRUE )
        {
            // 두 스켈레톤이 담기는 구로 카메라를 맞춘다.
            float3 minimum{ 1.0e9f };
            float3 maximum{ -1.0e9f };
            bool   bAny = false;
            for ( const vector<float3>* pList : { &_listSourcePosition, &_listTargetReferencePosition, &_listTargetPosePosition } )
            {
                for ( const float3& position : *pList )
                {
                    minimum = float3{ MathUtil::min( minimum._x, position._x ), MathUtil::min( minimum._y, position._y ), MathUtil::min( minimum._z, position._z ) };
                    maximum = float3{ MathUtil::max( maximum._x, position._x ), MathUtil::max( maximum._y, position._y ), MathUtil::max( maximum._z, position._z ) };
                    bAny    = true;
                }
            }
            if ( bAny )
                getPreviewViewport().frameSphere( ( minimum + maximum ) * 0.5f, MathUtil::max( ( maximum - minimum ).getLength() * 0.5f * RetargetPanelInternal::kFramePadding, 0.5f ) );
            _bFramePending = SW_FALSE;
        }
        ImGui::TextColored( ImVec4( RetargetPanelInternal::kSourceColor._x, RetargetPanelInternal::kSourceColor._y, RetargetPanelInternal::kSourceColor._z, 1.0f ), "source" );
        ImGui::SameLine();
        ImGui::TextColored( ImVec4( RetargetPanelInternal::kTargetPoseColor._x, RetargetPanelInternal::kTargetPoseColor._y, RetargetPanelInternal::kTargetPoseColor._z, 1.0f ),
                            "target (with retarget pose)" );
        ImGui::SameLine();
        ImGui::TextDisabled( "target reference" );
        const float2 available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        drawPreviewViewport( float2{ available._x, RetargetPanelInternal::kPreviewHeight * EditorThemeUtil::getDPIScale() }, false );
        EditorSelfTestMarks::note( "retarget.preview" );
        drawSkeletonLines();
    }

    void RetargetPanel::drawSkeletonLines()
    {
        using Internal                        = RetargetPanelInternal;
        const EditorPreviewViewport& viewport = getPreviewViewport();
        const float2                 position = viewport.getImagePosition();
        const float2                 size     = viewport.getImageSize();
        if ( size._x <= 0.0f || size._y <= 0.0f )
            return;
        float4x4 viewProjection{};
        float3   eye{};
        viewport.computeViewProjection( size._x / size._y, viewProjection, eye );
        EditorProjectedLineRenderer renderer{ ImGui::GetWindowDrawList(), viewProjection, position, size };
        if ( _bPreviewSourceReady == SW_TRUE )
            Internal::drawBones( renderer, _previewSource, _listSourcePosition, Internal::kSourceColor );
        if ( _bPreviewTargetReady == SW_TRUE )
        {
            // 리타깃 포즈가 있을 때만 레퍼런스를 따로 긋는다 — 없으면 둘이 같아 선이 겹친다.
            if ( _profile.getTargetPose().empty() == false )
                Internal::drawBones( renderer, _previewTarget, _listTargetReferencePosition, Internal::kTargetReferenceColor );
            Internal::drawBones( renderer, _previewTarget, _listTargetPosePosition, Internal::kTargetPoseColor );
        }
        _published._previewSegmentCount = renderer.getDrawnSegmentCount();
    }

    void RetargetPanel::drawRootAndPelvisRows()
    {
        const utf8* const arrLabel[4] = { "Root (source)", "Root (target)", "Pelvis (source)", "Pelvis (target)" };
        bool              bChanged    = false;
        for ( uint32 index = 0; index < 4; ++index )
        {
            ImGui::PushID( static_cast<int32>( index ) );
            EditorChrome::setNextItemWidthColumns( 21.0f );
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
        EditorChrome::setNextItemWidthColumns( 27.0f );
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
        EditorChrome::setNextItemWidthColumns( 16.0f );
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
            const float32 fieldWidth = -RetargetPanelInternal::kLabelWidth * EditorThemeUtil::getDPIScale();
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
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove" ) );
            if ( ImGui::SmallButton( "Remove" ) )
                removeIndex = chainIndex;
            ImGui::PopID();
        }
        if ( removeIndex != invalid_index::kUint32 )
        {
            listChain.erase( listChain.begin() + removeIndex );
            bChanged = true;
        }

        EditorChrome::setNextItemWidthColumns( 21.0f );
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

    void RetargetPanel::drawTargetPoseRows()
    {
        using Internal                        = RetargetPanelInternal;
        vector<RetargetPoseBone>& listPose    = _profile.getTargetPoseForEdit();
        uint32                    removeIndex = invalid_index::kUint32;
        bool                      bChanged    = false;
        bool                      bDragged    = false;
        const float32             fieldWidth  = 220.0f * EditorThemeUtil::getDPIScale();
        for ( uint32 poseIndex = 0; poseIndex < static_cast<uint32>( listPose.size() ); ++poseIndex )
        {
            RetargetPoseBone& poseBone = listPose[poseIndex];
            ImGui::PushID( static_cast<int32>( poseIndex ) );
            ImGui::Text( "%s", poseBone._bone.c_str() );
            ImGui::SameLine( 200.0f * EditorThemeUtil::getDPIScale() );
            float32 arrDegree[3] = { poseBone._rotationDegrees._x, poseBone._rotationDegrees._y, poseBone._rotationDegrees._z };
            ImGui::SetNextItemWidth( fieldWidth );
            if ( ImGui::DragFloat3( "##Rotation", arrDegree, Internal::kPoseDragSpeed, -Internal::kPoseDegreeLimit, Internal::kPoseDegreeLimit, "%.1f" ) )
            {
                poseBone._rotationDegrees = float3{ arrDegree[0], arrDegree[1], arrDegree[2] };
                bDragged                  = true; // 끄는 동안 미리보기가 바로 바뀐다 — 되돌리기 단계는 놓을 때 한 번
            }
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                bChanged = true;
            EditorSelfTestMarks::note( "retarget.targetPose.rotation" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove" ) );
            if ( ImGui::SmallButton( "Remove" ) )
                removeIndex = poseIndex;
            ImGui::PopID();
        }
        if ( removeIndex != invalid_index::kUint32 )
        {
            listPose.erase( listPose.begin() + removeIndex );
            bChanged = true;
        }
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputTextWithHint( "##NewPoseBone", "target bone", _newPoseBone.data(), _newPoseBone.max_size() );
        EditorSelfTestMarks::note( "retarget.targetPose.newBone" );
        ImGui::SameLine();
        const hashed_string newBone( _newPoseBone.c_str() );
        bool                bBoneUsable = _bPreviewTargetReady == SW_TRUE && _previewTarget.findBoneIndex( newBone ) >= 0; // 대상 스켈레톤에 있는 본만 받는다
        for ( const RetargetPoseBone& poseBone : listPose )
        {
            bBoneUsable = bBoneUsable && poseBone._bone != newBone; // 같은 본을 두 번 적으면 다시 읽을 때 오류다
        }
        ImGui::BeginDisabled( bBoneUsable == false );
        if ( ImGui::Button( "Add bone turn" ) )
        {
            listPose.push_back( RetargetPoseBone{ newBone, float3{} } );
            bChanged = true;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "retarget.targetPose.add" );
        EditorWidgets::drawTooltip( "Turn a target bone in its parent space (degrees) so the target reference matches the source reference, e.g. lower the arms of a T-pose source onto an A-pose target" );
        if ( bChanged )
            markEdited( "Retarget pose" );
        else if ( bDragged )
            rebuildPreviewPoses();
    }

    void RetargetPanel::drawBonePairRows()
    {
        vector<RetargetBonePair>& listPair    = _profile.getBonePairsForEdit();
        uint32                    removeIndex = invalid_index::kUint32;
        for ( uint32 pairIndex = 0; pairIndex < static_cast<uint32>( listPair.size() ); ++pairIndex )
        {
            ImGui::PushID( static_cast<int32>( pairIndex ) );
            ImGui::Text( "%s -> %s", listPair[pairIndex]._source.c_str(), listPair[pairIndex]._target.c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove" ) );
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
        EditorChrome::setNextItemWidthColumns( 19.0f );
        ImGui::InputTextWithHint( "##PairSource", "source bone", _newPairSource.data(), _newPairSource.max_size() );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 19.0f );
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
