#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/RetargetPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileEdit.h"

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
            /** @brief 리타깃 포즈 돌림을 끄는 속도(도 / 픽셀)입니다. */
            static constexpr float32 kPoseDragSpeed = 0.5f;
            /** @brief 리타깃 포즈 돌림의 범위(도)입니다. */
            static constexpr float32 kPoseDegreeLimit = 180.0f;
            /** @brief 본 목록 칸의 안내입니다. */
            static constexpr const utf8* kBoneListHint = "root to end: a, b, c";

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
             * @brief 접을 수 있는 구역 머리글입니다(처음에는 열려 있다). 열렸으면 true 입니다. @p pLabel 은 지금 언어의 제목, @p pStableID 는 영어 이름(창 안 ID · 테스트 표지)이다.
             * @details 머리글마다 표식 `retarget.section.<제목>` 을 남긴다 — 구역이 쌓여 창 아래 위젯이 보이는 영역 밖으로 밀리면 시나리오가 위 구역을 눌러 접고 닿게 한다.
             */
            [[nodiscard]] static bool drawSection( const utf8* pLabel, const utf8* pStableID )
            {
                const bool bOpen = ImGui::CollapsingHeader( EditorThemeUtil::makeStableLabel( pLabel, pStableID ), ImGuiTreeNodeFlags_DefaultOpen );
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::record( ( string( "retarget.section." ) + pStableID ).c_str() );
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
            [[nodiscard]] static bool readPoseTurnDegrees( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readPublished( &RetargetPanel::Published::_poseTurnDegrees, outValue );
            }

            /** @brief 리타깃 포즈 돌림 크기의 합(도)을 반올림합니다. */
            [[nodiscard]] static uint32 sumPoseTurnDegrees( const vector<RetargetPoseBone>& listPose )
            {
                float32 sum = 0.0f;
                for ( const RetargetPoseBone& poseBone : listPose )
                {
                    sum += MathUtil::abs( poseBone._rotationDegrees._x ) + MathUtil::abs( poseBone._rotationDegrees._y ) + MathUtil::abs( poseBone._rotationDegrees._z );
                }
                return static_cast<uint32>( MathUtil::round( sum ) );
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
    SW_EDITOR_PANEL( RetargetPanel, RetargetPanel::kPanelID, EditorPanelCategory::Custom, 9040 );

    SW_AUTOMATION_WATCH( editorRetargetPoseCount, "Editor.RetargetPoseCount", "Retarget pose rows (target bone turns) in the profile the Retarget Profile window has open",
                         &RetargetPanelInternal::readTargetPoseCount );
    SW_AUTOMATION_WATCH( editorRetargetPreviewSegments, "Editor.RetargetPreviewSegments", "Skeleton line segments the Retarget Profile window's 3D preview drew last frame",
                         &RetargetPanelInternal::readPreviewSegmentCount );
    SW_AUTOMATION_WATCH( editorRetargetChainCount, "Editor.RetargetChainCount", "Chains in the profile the Retarget Profile window has open", &RetargetPanelInternal::readChainCount );
    SW_AUTOMATION_WATCH( editorRetargetPoseTurnDegrees, "Editor.RetargetPoseTurnDegrees", "Sum of the retarget pose turn sizes (degrees, rounded) in the profile the Retarget Profile window has open",
                         &RetargetPanelInternal::readPoseTurnDegrees );

    RetargetPanel::RetargetPanel()
        : EditorPreviewPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _profile{}
        , _inspection{}
        , _preview{}
        , _listChainText{}
        , _inspectionError{}
        , _status{}
        , _sourceSkeleton{}
        , _targetSkeleton{}
        , _newDocumentPath{}
        , _arrPairName{}
        , _newChainName{ "Chain" }
        , _newChainSource{}
        , _newChainTarget{}
        , _newPairSource{}
        , _newPairTarget{}
        , _newPoseBone{}
        , _published{}
    {
    }

    void RetargetPanel::drawDocumentContent()
    {
        beginPreviewFrame();
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawNewDocumentRow();
        drawDocumentOpenBar( "retarget" );
        _published._targetPoseCount     = static_cast<uint32>( _profile.getTargetPose().size() );
        _published._chainCount          = static_cast<uint32>( _profile.getChains().size() );
        _published._poseTurnDegrees     = RetargetPanelInternal::sumPoseTurnDegrees( _profile.getTargetPose() );
        _published._previewSegmentCount = 0; // 미리보기를 접거나 스켈레톤을 지운 프레임에 앞 값이 남지 않게 그릴 때만 채운다
        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "OpenARetargetXmlOrTypeANew" ) );
            return;
        }
        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        using Internal = RetargetPanelInternal;
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "Skeletons" ), "Skeletons" ) )
            drawSkeletonRows();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "Preview" ), "Preview" ) )
            drawPreviewSection();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "RootAndPelvis" ), "Root and pelvis" ) )
            drawRootAndPelvisRows();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "Options" ), "Options" ) )
            drawOptionRows();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "Chains" ), "Chains" ) )
            drawChainRows();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "RetargetPose" ), "Retarget pose" ) )
            drawTargetPoseRows();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "BoneMap" ), "Bone map" ) )
            drawBonePairRows();
        if ( Internal::drawSection( SW_LOCKEY( "EdPluginWidget", "Check" ), "Check" ) )
            drawInspection();
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    bool RetargetPanel::saveDocument()
    {
        // 묶이지 않는 프로필은 런타임이 쓸 수 없다 — 저장하지 않는다.
        const expected<void> inspected = RetargetProfileInspection::inspect( _profile, _inspection );
        _inspectionError               = inspected.has_value() ? string{} : EditorWidgets::makeStatusText( inspected.error() );
        if ( inspected.has_value() == false )
        {
            _status = "Not saved: " + _inspectionError;
            return false;
        }
        const auto writePath = getDocumentDiskPath();                                       // 읽은 그 자리
        if ( writePath.has_value() == false || _profile.saveToFile( *writePath ) == false ) // 변형(`_base`)은 부모와의 차이로 쓴다
        {
            _status = writePath.has_value() == false ? string( "No write path for this asset" ) : string( "The profile could not be written (see the log)" );
            return false;
        }
        SW_LOG_INFO_ID( "RetargetPanel.RetargetProfileSaved", "Retarget profile saved to %#", writePath->c_str() );
        syncDocumentUndoBaseline(); // 저장한 글이 새 기준이다 — 안 그러면 앞 글로 되돌렸을 때 디스크와 달라도 "저장됨" 으로 보인다
        _status = "Saved";
        return true;
    }

    bool RetargetPanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, RetargetProfile::kExtension );
    }

    ToolAssetLoadResult RetargetPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( AssetIO::hasAsset( AssetID{ path } ) == false )
        {
            // 없는 경로는 새 프로필이다 — 빈 문서로 시작하고 저장하면 파일을 만든다(스켈레톤 · 뿌리와 골반이 묶이기 전에는 저장되지 않는다).
            _profile.clear();
            syncFromProfile();
            _status = "New profile - set both skeletons and the root and pelvis pairs, then save";
            return ToolAssetLoadResult::Missing;
        }
        // 파일은 런타임과 같은 공유 캐시의 한 벌로 읽고 창은 그 사본을 고친다(저장하면 핫 리로드가 한 벌을 제자리에서 다시 읽는다). 못 읽으면 빈 문서다.
        const shared_ptr<const RetargetProfile> pLoaded = SharedAssetCache<RetargetProfile>::acquire( path );
        const bool                              bLoaded = pLoaded != nullptr;
        if ( bLoaded )
            _profile = *pLoaded;
        else
            _profile.clear();
        // 문서를 새로 읽으면 스켈레톤 파일도 지금 것을 본다. 못 읽어도 비워진 프로필에 맞춘다 — 앞 문서의 글이 칸에 남아 있으면 고칠 때 섞인다.
        _preview.reloadSkeletons( _profile );
        syncFromProfile();
        if ( bLoaded == false )
        {
            _status = "The retarget profile could not be read - see the log";
            return ToolAssetLoadResult::Malformed;
        }
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void RetargetPanel::applyDocumentText( string_view text )
    {
        // 글을 먼저 다른 프로필에 읽는다 — `loadFromXMLText` 은 실패하면 내용을 비우므로, 제자리에서 읽으면 되돌리기 한 번에 문서가 사라진다.
        RetargetProfile restored;
        if ( restored.loadFromXMLText( text, "retarget undo" ) == false )
        {
            _status = "This undo step could not be read - see the log";
            return;
        }
        restored.setBaseID( _profile.getBaseID() ); // 되돌리기 글은 펼친 한 벌이다 — 문서의 부모는 그대로 둬야 저장이 차이로 쓴다
        _profile = std::move( restored );
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
            _listChainText[chainIndex]._name   = chain._name.c_str();
            _listChainText[chainIndex]._source = RetargetProfileEdit::joinNames( chain._listSourceBone );
            _listChainText[chainIndex]._target = RetargetProfileEdit::joinNames( chain._listTargetBone );
        }
        if ( _preview.isStale( _profile ) )
            _preview.reloadSkeletons( _profile );
        else
            _preview.rebuildPoses( _profile );
        // 실패는 점검 칸이 보인다
        const expected<void> inspected = RetargetProfileInspection::inspect( _profile, _preview.isSourceReady() ? &_preview.getSourceSkeleton() : nullptr, _preview.isTargetReady() ? &_preview.getTargetSkeleton() : nullptr, _inspection );
        _inspectionError               = inspected.has_value() ? string{} : EditorWidgets::makeStatusText( inspected.error() );
    }

    void RetargetPanel::markEdited( const utf8* pLabel )
    {
        _status.clear(); // "Saved" 같은 앞 결과는 고친 뒤에는 맞지 않다
        notifyDocumentEdited( pLabel );
        syncFromProfile();
    }

    void RetargetPanel::drawNewDocumentRow()
    {
        EditorChrome::setNextItemWidthColumns( 48.0f );
        (void)EditorWidgets::drawHintField( "##NewRetargetPath", _newDocumentPath.data(), _newDocumentPath.max_size(), "<resource id>.retarget.xml", "retarget.newPath" );
        ImGui::SameLine();
        const bool bPathValid = canAcceptDocumentPath( string_view( _newDocumentPath.c_str() ) );
        ImGui::BeginDisabled( bPathValid == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "New" ) ) )
            requestOpenDocument( string_view( _newDocumentPath.c_str() ) ); // 없는 파일이면 `loadDocument` 가 빈 문서로 연다
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "retarget.new" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "StartAnEmptyProfileAtThisResource" ) );
    }

    void RetargetPanel::drawSkeletonRows()
    {
        const float32 fieldWidth = -RetargetPanelInternal::kLabelWidth * EditorThemeUtil::getDPIScale();
        ImGui::SetNextItemWidth( fieldWidth );
        const bool bSourceDone = EditorWidgets::drawAssetPathField( "Source skeleton", _sourceSkeleton.data(), _sourceSkeleton.max_size(), "*.skeleton.xml", ".skeleton.xml", "retarget.sourceSkeleton" )._bDeactivatedAfterEdit;
        ImGui::SetNextItemWidth( fieldWidth );
        const bool bTargetDone = EditorWidgets::drawAssetPathField( "Target skeleton", _targetSkeleton.data(), _targetSkeleton.max_size(), "*.skeleton.xml", ".skeleton.xml", "retarget.targetSkeleton" )._bDeactivatedAfterEdit;
        if ( bSourceDone || bTargetDone )
        {
            _profile.setSkeletonPaths( _sourceSkeleton.c_str(), _targetSkeleton.c_str() );
            markEdited( "Retarget skeletons" );
        }
    }

    void RetargetPanel::drawPreviewSection()
    {
        if ( _preview.isAnyReady() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "SetTheSourceAndTargetSkeletonsToSee" ) );
            return;
        }
        if ( _preview.consumeFramePending() )
        {
            // 두 스켈레톤이 담기는 구로 카메라를 맞춘다.
            float3  center{};
            float32 radius = 0.0f;
            if ( _preview.findFrameSphere( center, radius ) )
                getPreviewViewport().frameSphere( center, radius );
        }
        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Viewport", "BoneSource" ) ), "source" );
        ImGui::SameLine();
        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Viewport", "BonePose" ) ),
                            "target (with retarget pose)" );
        ImGui::SameLine();
        ImGui::TextDisabled( "target reference" );
        const float2 available = float2{ ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y };
        drawPreviewViewport( float2{ available._x, RetargetPanelInternal::kPreviewHeight * EditorThemeUtil::getDPIScale() }, false );
        EditorSelfTestMarks::record( "retarget.preview" );
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
        if ( _preview.isSourceReady() )
            Internal::drawBones( renderer, _preview.getSourceSkeleton(), _preview.getSourcePositions(), SW_THEME_COLOR( "Viewport", "BoneSource" ).toFloat4() );
        if ( _preview.isTargetReady() )
        {
            // 리타깃 포즈가 있을 때만 레퍼런스를 따로 긋는다 — 없으면 둘이 같아 선이 겹친다.
            if ( _profile.getTargetPose().empty() == false )
                Internal::drawBones( renderer, _preview.getTargetSkeleton(), _preview.getTargetReferencePositions(), SW_THEME_COLOR( "Viewport", "BoneReference" ).toFloat4() );
            Internal::drawBones( renderer, _preview.getTargetSkeleton(), _preview.getTargetPosePositions(), SW_THEME_COLOR( "Viewport", "BonePose" ).toFloat4() );
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
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "Translation" ) );
        EditorChrome::setNextItemWidthColumns( 27.0f );
        if ( ImGui::Combo( "##Translation", &mode, RetargetPanelInternal::kArrTranslationMode, static_cast<int32>( SW_COUNT_OF( RetargetPanelInternal::kArrTranslationMode ) ) ) )
        {
            _profile.setRootAndPelvis( _profile.getSourceRoot(), _profile.getTargetRoot(), _profile.getSourcePelvis(), _profile.getTargetPelvis(),
                                       static_cast<RetargetTranslationMode>( mode ) );
            markEdited( "Retarget translation" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "HowRootAndPelvisMovementIsCarried" ) );
        bool bMapSameNames = _profile.isMappingSameNames();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "MapSameNames" ), &bMapSameNames ) )
        {
            _profile.setMapSameNames( bMapSameNames );
            markEdited( "Retarget same names" );
        }
        ImGui::SameLine();
        float32 heightRatio = _profile.getHeightRatioOverride();
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdPluginField", "HeightRatio0Pelvis" ) );
        EditorChrome::setNextItemWidthColumns( 16.0f );
        ImGui::DragFloat( "##Heightratio0pelvis", &heightRatio, 0.01f, 0.0f, 10.0f, "%.3f" );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            _profile.setHeightRatioOverride( heightRatio );
            markEdited( "Retarget height ratio" );
        }
    }

    bool RetargetPanel::drawChainBoneField( const utf8* pID, string& inoutText, vector<hashed_string>& inoutListBone )
    {
        (void)EditorWidgets::drawTextField( pID, inoutText, -1.0f );
        const bool bCommitted = ImGui::IsItemDeactivatedAfterEdit();
        EditorWidgets::drawTooltip( RetargetPanelInternal::kBoneListHint );
        if ( bCommitted == false )
            return false;
        vector<hashed_string> listBone = RetargetProfileEdit::parseNames( inoutText );
        if ( listBone.empty() )
        {
            // 본 목록이 빈 사슬은 다시 읽을 때 오류다 — 되돌리기가 그 글로 돌아오면 프로필이 비어 버린다. 사슬을 지우는 길은 Remove 다.
            _status   = "A chain needs at least one bone on each side - use Remove to delete it";
            inoutText = RetargetProfileEdit::joinNames( inoutListBone );
            return false;
        }
        inoutListBone = std::move( listBone );
        return true;
    }

    void RetargetPanel::drawChainRows()
    {
        vector<RetargetChain> listChain   = _profile.getChains(); // 사본을 고쳐 바뀌었을 때 넣는다
        uint32                removeIndex = constant::invalid_index::kUint32;
        bool                  bChanged    = false;
        for ( uint32 chainIndex = 0; chainIndex < static_cast<uint32>( listChain.size() ) && chainIndex < _listChainText.size(); ++chainIndex )
        {
            RetargetChain& chain = listChain[chainIndex];
            ChainText&     text  = _listChainText[chainIndex];
            ImGui::PushID( static_cast<int32>( chainIndex ) );
            ImGui::SeparatorText( chain._name.c_str() );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "Name" ) );
            (void)EditorWidgets::drawTextField( "##ChainName", text._name, -1.0f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                // 빈 이름 · 겹친 이름은 다시 읽을 때 오류다 — 받지 않고 칸을 되돌린다.
                const hashed_string newChainName( string_view( text._name ) );
                if ( RetargetProfileEdit::renameChain( listChain, chainIndex, newChainName ) )
                {
                    bChanged = true;
                }
                else if ( ( chain._name == newChainName ) == false )
                {
                    _status    = "A chain name must be new and not empty";
                    text._name = chain._name.c_str();
                }
            }
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "SourceBones" ) );
            bChanged = drawChainBoneField( "##SourceBones", text._source, chain._listSourceBone ) || bChanged;
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "TargetBones" ) );
            bChanged     = drawChainBoneField( "##TargetBones", text._target, chain._listTargetBone ) || bChanged;
            bool bIKGoal = chain._bIKGoal == SW_TRUE;
            if ( ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "IKGoal" ), &bIKGoal ) )
            {
                chain._bIKGoal = bIKGoal ? SW_TRUE : SW_FALSE;
                bChanged       = true;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "SolveTheEndOntoTheSourceEnd" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) )
                removeIndex = chainIndex;
            ImGui::PopID();
        }
        if ( removeIndex != constant::invalid_index::kUint32 )
        {
            listChain.erase( listChain.begin() + removeIndex );
            bChanged = true;
        }

        ImGui::SeparatorText( SW_LOCKEY( "EdPlugin", "AddChain" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPlugin", "Name" ) );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputText( "##NewChain", _newChainName.data(), _newChainName.max_size() );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "SourceBones" ) );
        (void)EditorWidgets::drawTextField( "##NewChainSource", _newChainSource, -1.0f );
        EditorWidgets::drawTooltip( RetargetPanelInternal::kBoneListHint );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "TargetBones" ) );
        (void)EditorWidgets::drawTextField( "##NewChainTarget", _newChainTarget, -1.0f );
        EditorWidgets::drawTooltip( RetargetPanelInternal::kBoneListHint );
        RetargetChain newChain{};
        // 빈 본 목록 · 겹친 이름은 다시 읽을 때 오류다 — 다 채워야 더한다.
        const bool bNewChainValid = RetargetProfileEdit::makeChain( listChain, _newChainName.c_str(), _newChainSource, _newChainTarget, newChain );
        ImGui::BeginDisabled( bNewChainValid == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AddChain" ) ) )
        {
            listChain.push_back( std::move( newChain ) );
            _newChainSource.clear();
            _newChainTarget.clear();
            bChanged = true;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "retarget.addChain" );
        if ( bChanged )
        {
            _profile.setChains( std::move( listChain ) );
            markEdited( "Retarget chains" );
        }
    }

    void RetargetPanel::drawTargetPoseRows()
    {
        using Internal                       = RetargetPanelInternal;
        vector<RetargetPoseBone> listPose    = _profile.getTargetPose(); // 사본을 고쳐 바뀌었을 때 넣는다
        uint32                   removeIndex = constant::invalid_index::kUint32;
        bool                     bChanged    = false;
        bool                     bDragged    = false;
        const float32            fieldWidth  = 220.0f * EditorThemeUtil::getDPIScale();
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
            EditorSelfTestMarks::record( "retarget.targetPose.rotation" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) )
                removeIndex = poseIndex;
            ImGui::PopID();
        }
        if ( removeIndex != constant::invalid_index::kUint32 )
        {
            listPose.erase( listPose.begin() + removeIndex );
            bChanged = true;
        }
        EditorChrome::setNextItemWidthColumns( 21.0f );
        ImGui::InputTextWithHint( "##NewPoseBone", "target bone", _newPoseBone.data(), _newPoseBone.max_size() );
        EditorSelfTestMarks::record( "retarget.targetPose.newBone" );
        ImGui::SameLine();
        const hashed_string newBone( _newPoseBone.c_str() );
        const bool          bBoneUsable = _preview.hasTargetBone( newBone ) && RetargetProfileEdit::canAddPoseBone( listPose, newBone ); // 대상 스켈레톤에 있는 본만 받는다
        ImGui::BeginDisabled( bBoneUsable == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AddBoneTurn" ) ) )
        {
            listPose.push_back( RetargetPoseBone{ newBone, float3{} } );
            bChanged = true;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "retarget.targetPose.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdPluginTip", "TurnATargetBoneInItsParent" ) );
        // 끄는 값도 프로필에 넣는다 — 사본만 고치면 다음 프레임이 프로필에서 다시 복사해 끌기가 사라지고 미리보기도 앞 값을 그린다. 되돌리기 단계는 놓을 때 한 번(기준 글은 끌기 전 그대로다).
        if ( bChanged || bDragged )
            _profile.setTargetPose( std::move( listPose ) );
        if ( bChanged )
            markEdited( "Retarget pose" );
        else if ( bDragged )
            _preview.rebuildPoses( _profile );
    }

    void RetargetPanel::drawBonePairRows()
    {
        vector<RetargetBonePair> listPair    = _profile.getBonePairs(); // 사본을 고쳐 바뀌었을 때 넣는다
        uint32                   removeIndex = constant::invalid_index::kUint32;
        for ( uint32 pairIndex = 0; pairIndex < static_cast<uint32>( listPair.size() ); ++pairIndex )
        {
            ImGui::PushID( static_cast<int32>( pairIndex ) );
            ImGui::Text( "%s -> %s", listPair[pairIndex]._source.c_str(), listPair[pairIndex]._target.c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) )
                removeIndex = pairIndex;
            ImGui::PopID();
        }
        bool bChanged = false;
        if ( removeIndex != constant::invalid_index::kUint32 )
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
        const hashed_string newSource( _newPairSource.c_str() );
        const hashed_string newTarget( _newPairTarget.c_str() );
        // 두 스켈레톤에 있는 본만 받는다. 없는 이름을 적어 두면 프로필이 묶이지 않아 저장되지 않는다.
        const bool bPairUsable = _preview.hasSourceBone( newSource ) && _preview.hasTargetBone( newTarget ) && RetargetProfileEdit::canAddBonePair( listPair, newSource, newTarget );
        ImGui::BeginDisabled( bPairUsable == false );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AddPair" ) ) )
        {
            listPair.push_back( RetargetBonePair{ newSource, newTarget } );
            bChanged = true;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "retarget.addPair" );
        if ( bChanged )
        {
            _profile.setBonePairs( std::move( listPair ) );
            markEdited( "Retarget bone map" );
        }
    }

    void RetargetPanel::drawInspection()
    {
        if ( _inspectionError.empty() == false )
        {
            EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Warning" ) );
            if ( _inspection._listProblem.empty() )
                ImGui::TextWrapped( "%s", _inspectionError.c_str() );
            for ( const string& problem : _inspection._listProblem )
            {
                ImGui::TextWrapped( "%s", problem.c_str() );
            }
            EditorThemeUtil::popTextColor();
            return;
        }
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "BindsOfTargetBones", TextArgumentList().addInteger( "mappedTargetBoneCount", static_cast<int64>( _inspection._mappedTargetBoneCount ) ).addInteger( "targetBoneCount", static_cast<int64>( _inspection._targetBoneCount ) ).addDecimal( "heightRatio", static_cast<float64>( _inspection._heightRatio ), 3 ) ).c_str() );
        for ( const RetargetChainInspection& chain : _inspection._listChain )
        {
            const float32 ratio = chain.computeLengthRatio();
            ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "SourceMTargetM", TextArgumentList().addText( "name", chain._name.c_str() ).addDecimal( "sourceLength", static_cast<float64>( chain._sourceLength ), 3 ).addDecimal( "targetLength", static_cast<float64>( chain._targetLength ), 3 ).addDecimal( "ratio", static_cast<float64>( ratio ), 2 ) ).c_str() );
        }
        EditorSelfTestMarks::record( "retarget.check" );
        EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Warning" ) );
        for ( const string& warning : _inspection._listWarning )
        {
            ImGui::TextWrapped( "%s", warning.c_str() );
        }
        EditorThemeUtil::popTextColor();
    }
} // namespace sw::editor
