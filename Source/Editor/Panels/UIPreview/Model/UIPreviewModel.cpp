#include "pch.h"

#include "Editor/Panels/UIPreview/Model/UIPreviewModel.h"

#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetNavigation.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw::editor
{
    SW_LOG_CALLER( "UIPreviewModel" );

    /** @brief 되돌리기 항목이 모델을 가리키는 길입니다. 모델이 사라지면 `_pModel` 이 비어 항목은 아무것도 하지 않습니다. */
    struct UIPreviewModel::EditSession
    {
        UIPreviewModel* _pModel{ nullptr };

        void apply( uint32 generation, uint32 ordinal, const string& propertyName, const string& text ) const
        {
            if ( _pModel != nullptr )
                _pModel->applyEditedText( generation, ordinal, propertyName, text );
        }

        void insert( uint32 generation, const hashed_string& typeName, uint32 parentOrdinal, uint32 index ) const
        {
            if ( _pModel != nullptr )
                _pModel->applyInsert( generation, typeName, parentOrdinal, index );
        }

        void detach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder ) const
        {
            if ( _pModel != nullptr )
                _pModel->applyDetach( generation, parentOrdinal, index, pHolder );
        }

        void reattach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder ) const
        {
            if ( _pModel != nullptr )
                _pModel->applyReattach( generation, parentOrdinal, index, pHolder );
        }

        void move( uint32 generation, uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex ) const
        {
            if ( _pModel != nullptr )
                _pModel->applyMove( generation, parentOrdinal, fromIndex, toIndex );
        }
    };

    /** @brief 지운 위젯을 되돌리기 항목 둘(지우기를 되돌리기 · 다시 하기)이 나눠 보관하고 있는 상자입니다. */
    struct UIPreviewModel::RemovedWidget
    {
        unique_ptr<Widget> _pWidget{};
    };

    namespace
    {
        struct UIPreviewModelInternal
        {
            static constexpr uint32 kPaletteRefreshFrames = 180; ///< 팔레트를 다시 모으는 간격(게임 모듈이 위젯 타입을 더하거나 뺄 수 있다)
            /** @brief 입력 흉내 버튼 순서(`UIPreviewPointerInput` 의 배열 순서)의 UI 버튼입니다. */
            static constexpr MouseButton kArrButton[UIPreviewPointerInput::kButtonCount] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };
        };
    } // namespace

    UIPreviewModel::UIPreviewModel( const DataPath& initialDocument )
        : _undoOwner{}
        , _onDirtyChanged{}
        , _onTargetReleasing{}
        , _listRow{}
        , _pRowTree{ nullptr }
        , _rowTreeGeneration{ 0 }
        , _listLanguage{}
        , _listBaseline{}
        , _listPaletteType{}
        , _paletteType{}
        , _documentPath{ initialDocument }
        , _openedDocument{}
        , _targetPath{}
        , _error{}
        , _lastFocusKey{}
        , _language{}
        , _theme{}
        , _animation{}
        , _viewport{}
        , _pointer{}
        , _focus{}
        , _pLocalization{}
        , _pSession{ sw::make_shared<EditSession>() }
        , _probe{}
        , _screen{ kInvalidUIScreenHandle }
        , _selected{ kInvalidWidgetID }
        , _seenRootID{ kInvalidWidgetID }
        , _baselineWidget{ kInvalidWidgetID }
        , _selectedOrdinal{ UIPreviewLogic::kNoOrdinal }
        , _generation{ 0 }
        , _gameTextRevision{ 0 }
        , _languageIndex{ 0 }
        , _paletteFrame{ 0 }
        , _slotEditCount{ 0 }
        , _resolutionTable{}
        , _resolutionIndex{ 0 }
        , _bResolutionTableAttempted{ false }
        , _uiScale{ 1.0f }
        , _textScale{ 1.0f }
        , _safeZone{ 0.0f }
        , _animationScrub{ 0.0f }
        , _lastPointer{}
        , _bInteract{ false }
        , _bNavigationMode{ false }
        , _bMutated{ false }
        , _bAnimationPaused{ false }
        , _bAnimationLoop{ false }
        , _bPointerInside{ false }
        , _bPointerPressed{ false }
        , _bApplyLocalization{ false }
        , _bAwaitReload{ false }
        , _bReloadedAfterSave{ false }
    {
        _pSession->_pModel = this;
    }

    UIPreviewModel::~UIPreviewModel()
    {
        _pSession->_pModel = nullptr; // 쌓여 있는 되돌리기 항목이 사라진 모델을 부르지 않게 한다
    }

    void UIPreviewModel::setDirty( bool bDirty )
    {
        if ( _onDirtyChanged.isBound() )
            _onDirtyChanged( bDirty );
    }

    void UIPreviewModel::notifyTargetReleasing()
    {
        if ( _onTargetReleasing.isBound() )
            _onTargetReleasing();
    }

    void UIPreviewModel::onDocumentPathCommitted( const string& previous, bool bDirty )
    {
        if ( bDirty )
            SW_LOG_WARNING_ID( "UIPreview.UnsavedEditsDiscarded", "UI Preview: unsaved edits of '%#' are discarded because the path changed", previous.c_str() );
        _error.clear();
    }

    void UIPreviewModel::syncFocusedDocument( bool bDirty )
    {
        // 콘텐츠 브라우저에서 `*.ui.xml` 을 두 번 누르면(`EditorAssetCommands::openPath`) 작업공간 포커스가 그 경로가 되고 이 패널이 열린다.
        if ( EditorAssetTypeRegistry::consumeWorkspaceFocusKey( _lastFocusKey, 0 ) == false )
            return;
        const string_view focused = EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetType{ UIDocumentAsset::kAssetType } );
        if ( focused.empty() || _documentPath == focused )
            return;
        const string previous = _documentPath.get();
        if ( _documentPath.commit( focused ) == false )
            return; // 리소스 id 가 아니면 commit 이 알렸다 — 지금 문서를 그대로 둔다
        if ( bDirty )
            SW_LOG_WARNING_ID( "UIPreview.UnsavedEditsDiscarded", "UI Preview: unsaved edits of '%#' are discarded because '%#' was opened", previous.c_str(), _documentPath.c_str() );
        _error.clear();
    }

    const UIResolutionPreset* UIPreviewModel::findSelectedResolution()
    {
        if ( _bResolutionTableAttempted == false )
        {
            // 테이블 경로는 엔진 기본 에셋 테이블이 원본이다. 읽지 못하면(이유는 테이블이 알린다) 목록이 비어 미리보기를 세우지 않는다.
            _bResolutionTableAttempted                = true;
            const EngineDefaultAssets* pDefaultAssets = editor::findSubsystem<const EngineDefaultAssets>();
            if ( pDefaultAssets == nullptr )
            {
                SW_LOG_ERROR( "UI preview needs the engine default assets for its resolution table - the preview stays empty" );
                return nullptr;
            }
            if ( _resolutionTable.loadFromAsset( AssetID{ pDefaultAssets->_uiResolutionPresets.get() } ) == false )
                return nullptr;
        }
        if ( _resolutionTable._listResolution.empty() )
            return nullptr;
        if ( _resolutionIndex >= _resolutionTable._listResolution.size() )
            _resolutionIndex = 0;
        return &_resolutionTable._listResolution[_resolutionIndex];
    }

    void UIPreviewModel::closePreview()
    {
        // 닫힐 때는 UI 시스템이 먼저 내려갔을 수 있다(소멸자 · 종료) — 풀 화면이 이미 사라진 것이라 오류가 아니다.
        UISystem* pUI = editor::findSubsystem<UISystem>(); // 종료 순서에 따라 이미 없을 수 있다
        if ( pUI != nullptr && _screen != kInvalidUIScreenHandle )
        {
            UIScreen* pScreen = pUI->getOffscreenScreens().findScreen( _screen );
            if ( pScreen != nullptr )
            {
                _pointer.forgetTree( pScreen->getTree() ); // 트리를 지우기 전에 호버와 포커스를 놓는다
                _focus.forgetTree( pScreen->getTree() );
            }
            pUI->getOffscreenScreens().remove( _screen );
        }
        _screen = kInvalidUIScreenHandle;
        _openedDocument.clear();
        _targetPath.clear();
        _selected           = kInvalidWidgetID;
        _selectedOrdinal    = UIPreviewLogic::kNoOrdinal;
        _seenRootID         = kInvalidWidgetID;
        _baselineWidget     = kInvalidWidgetID;
        _bMutated           = false;
        _bPointerInside     = false;
        _bPointerPressed    = false;
        _bNavigationMode    = false;
        _bApplyLocalization = true;
        ++_generation;
        _listBaseline.clear();
        setDirty( false );
    }

    void UIPreviewModel::shutdown()
    {
        closePreview();
        _pLocalization.reset(); // 화면을 닫은 뒤에 놓는다(화면이 관리자를 가리킨다)
        _probe = UIPreviewProbe{};
    }

    void UIPreviewModel::syncPreview( UISystem& ui )
    {
        const UIResolutionPreset* pResolution = findSelectedResolution();
        if ( pResolution == nullptr )
        {
            _error = "UI preview resolutions could not be read (EngineDefaultAssets _uiResolutionPresets - see the log)";
            return;
        }
        _viewport                        = UIPreviewLogic::makeViewport( ui.getScaleSettings(), *pResolution, _uiScale, _safeZone );
        const string targetPath          = UIPreviewLogic::makeTargetPath( _viewport );
        const bool   bReopen             = _screen == kInvalidUIScreenHandle || ui.getOffscreenScreens().findScreen( _screen ) == nullptr || _documentPath != _openedDocument;
        const bool   bPreviousOpenFailed = bReopen && _error.empty() == false && _documentPath == _openedDocument;
        if ( bPreviousOpenFailed )
            return; // 같은 문서가 지난번에 만들지 못했다 — 고칠 때까지(경로를 바꾸거나 다시 열기) 프레임마다 다시 시도하지 않는다
        if ( bReopen )
        {
            const string documentPath = _documentPath.get(); // 확정 경로는 늘 값이 있다(EditableAssetPath)
            notifyTargetReleasing();                         // 크기가 바뀌면 렌더 텍스처도 새것이다
            closePreview();                                  // 포인터 · 포커스를 놓고 화면을 닫는다(되돌리기 세대도 올린다)
            _screen             = ui.openOffscreenScreen( documentPath, targetPath );
            _openedDocument     = documentPath;
            _targetPath         = targetPath;
            _error              = _screen == kInvalidUIScreenHandle ? SW_LOCKEY( "EditorUIPreview", "BuildFailed" ) : "";
            _bApplyLocalization = true;
        }
        else if ( _targetPath != targetPath )
        {
            // 해상도를 바꿔도 화면(트리, 고친 값)은 그대로 두고 그릴 렌더 텍스처만 새 크기의 것으로 옮긴다.
            notifyTargetReleasing();
            ui.getOffscreenScreens().setTargetPath( _screen, hashed_string( targetPath ) );
            _targetPath = targetPath;
        }
        if ( _screen != kInvalidUIScreenHandle )
        {
            ui.setOffscreenView( _screen, _viewport, _textScale, _theme );
            if ( UIScreen* pScreen = ui.getOffscreenScreens().findScreen( _screen ) )
                pScreen->setReportsUnhandledCommands( false ); // 미리보기에는 게임 코드가 없어 명령을 받을 쪽이 없다
        }
    }

    void UIPreviewModel::syncTree( UIScreen& screen )
    {
        // 문서를 다시 읽으면(저장, 에디터 밖 편집) 핫 리로드가 트리를 새로 지어 위젯 번호가 모두 바뀐다 — 고른 위젯은 문서 순서 번호로 이어 간다.
        WidgetTree&    tree   = screen.getTree();
        const Widget*  pRoot  = tree.getRoot();
        const WidgetID rootID = pRoot != nullptr ? pRoot->getID() : kInvalidWidgetID;
        if ( rootID == _seenRootID )
            return;
        const bool bRebuilt = _seenRootID != kInvalidWidgetID;
        _seenRootID         = rootID;
        _pointer.forgetTree( tree );
        _focus.forgetTree( tree );
        _bPointerInside  = false;
        _bPointerPressed = false;
        ++_generation;
        _listBaseline.clear();
        _baselineWidget = kInvalidWidgetID;
        _selected       = _selectedOrdinal != UIPreviewLogic::kNoOrdinal ? UIPreviewLogic::findWidgetByOrdinal( tree, _selectedOrdinal ) : kInvalidWidgetID;
        if ( bRebuilt )
        {
            // 다시 만든 트리는 디스크 문서 그대로다 — 고친 값과 입력 흉내의 흔적이 모두 사라졌다.
            _bMutated = false;
            setDirty( false );
            if ( _bAwaitReload )
            {
                _bAwaitReload       = false;
                _bReloadedAfterSave = true;
            }
        }
    }

    void UIPreviewModel::syncLanguage( UISystem& ui )
    {
        LocalizationManager* pGame = editor::findSubsystem<LocalizationManager>();
        if ( _listLanguage.empty() )
        {
            UIPreviewLogic::makeLanguageChoices( pGame != nullptr ? pGame->getAvailableLanguages() : vector<string>{}, _listLanguage );
        }
        if ( _language.empty() || pGame == nullptr )
        {
            if ( _pLocalization != nullptr )
            {
                ui.getOffscreenScreens().setLocalization( _screen, nullptr );
                _pLocalization.reset();
            }
            else if ( _bApplyLocalization && _screen != kInvalidUIScreenHandle )
            {
                ui.getOffscreenScreens().setLocalization( _screen, nullptr );
            }
            _bApplyLocalization = false;
            return;
        }

        // 문화권이 전역이라 게임 UI 의 언어는 건드리지 않는다 — 게임 관리자의 사본을 이 화면에만 적용한다. 게임 텍스트가 다시 읽히면(번역 핫 리로드) 사본도 다시 만든다.
        const uint32 gameRevision = pGame->getTextRevision();
        const bool   bNeedCopy    = _pLocalization == nullptr || gameRevision != _gameTextRevision;
        if ( bNeedCopy )
        {
            if ( _pLocalization == nullptr )
                _pLocalization = sw::make_unique<LocalizationManager>();
            _pLocalization->copyStateFrom( *pGame );
            _gameTextRevision   = gameRevision;
            _bApplyLocalization = true;
        }
        if ( _pLocalization->getCurrentLanguage() != _language )
        {
            (void)_pLocalization->setCurrentLanguage( _language );
            _bApplyLocalization = true;
        }
        if ( _bApplyLocalization && _screen != kInvalidUIScreenHandle )
        {
            ui.getOffscreenScreens().setLocalization( _screen, _pLocalization.get() );
            _bApplyLocalization = false;
        }
    }

    void UIPreviewModel::selectLanguage( uint32 index )
    {
        if ( index >= _listLanguage.size() )
            return;
        _language      = _listLanguage[index];
        _languageIndex = index;
    }

    UIScreen* UIPreviewModel::findPreviewScreen() const
    {
        UISystem* pUI = editor::findSubsystem<UISystem>();
        return pUI != nullptr ? pUI->getOffscreenScreens().findScreen( _screen ) : nullptr;
    }

    bool UIPreviewModel::canSaveDocument() const
    {
        return _bMutated == false && _screen != kInvalidUIScreenHandle && _openedDocument.empty() == false;
    }

    bool UIPreviewModel::saveDocument()
    {
        UISystem*        pUI        = editor::findSubsystem<UISystem>();
        UIDocumentCache* pDocuments = editor::findSubsystem<UIDocumentCache>();
        const UIScreen*  pScreen    = pUI != nullptr ? pUI->getOffscreenScreens().findScreen( _screen ) : nullptr;
        if ( pScreen == nullptr || pDocuments == nullptr )
            return false;
        string text;
        string error;
        if ( UIPreviewLogic::makeDocumentText( *pDocuments, *pScreen, _openedDocument, text, error ) == false )
        {
            SW_LOG_ERROR( "UI Preview: '%#' cannot be saved - %#", _openedDocument.c_str(), error.c_str() );
            return false;
        }
        const string path = AssetPaths::getWritePath( _openedDocument );
        if ( path.empty() || FileUtil::writeTextFile( path, text ) == false )
        {
            SW_LOG_ERROR( "UI Preview: '%#' could not be written to '%#'", _openedDocument.c_str(), path.c_str() );
            return false;
        }
        SW_LOG_INFO( "UI Preview: saved '%#'", _openedDocument.c_str() );
        _bAwaitReload       = true;
        _bReloadedAfterSave = false;
        return true;
    }

    void UIPreviewModel::revertDocument()
    {
        closePreview();
        _error.clear();
    }

    void UIPreviewModel::select( WidgetID widget, uint32 ordinal )
    {
        _selected        = widget;
        _selectedOrdinal = ordinal;
    }

    void UIPreviewModel::selectAt( const WidgetTree& tree, const float2& point )
    {
        _selected        = UIPreviewLogic::findWidgetAt( tree, point );
        _selectedOrdinal = _selected != kInvalidWidgetID ? UIPreviewLogic::findWidgetOrdinal( tree, _selected ) : UIPreviewLogic::kNoOrdinal;
    }

    const vector<UIPreviewWidgetRow>& UIPreviewModel::getWidgetRows( const WidgetTree& tree )
    {
        // 행은 위젯 번호 · 깊이 · 타입 · 이름이라 구조(붙기 · 떨어지기 · 이름)가 바뀔 때만 다시 짓는다.
        const bool bRowsStale = _pRowTree != &tree || _rowTreeGeneration != tree.getStructureGeneration();
        if ( bRowsStale )
        {
            UIPreviewLogic::collectWidgetRows( tree, _listRow );
            _pRowTree          = &tree;
            _rowTreeGeneration = tree.getStructureGeneration();
        }
        return _listRow;
    }

    void UIPreviewModel::captureBaseline( const Widget& widget )
    {
        _listBaseline.clear();
        _baselineWidget       = widget.getID();
        const TypeInfo* pType = widget.getTypeInfo();
        if ( pType == nullptr )
            return;
        for ( const PropertyInfo& prop : pType->getPropertiesWithBase() )
        {
            if ( prop._containerKind != ContainerKind::None )
                continue; // 컨테이너는 텍스트 한 줄로 되돌릴 수 없다
            BaselineValue value{};
            value._name = prop._name.c_str();
            value._text = UIPreviewLogic::formatWidgetProperty( widget, prop );
            _listBaseline.push_back( std::move( value ) );
        }
    }

    void UIPreviewModel::syncBaseline( const Widget& widget )
    {
        if ( _baselineWidget != widget.getID() )
            captureBaseline( widget );
    }

    void UIPreviewModel::onPropertyEdited( const PropertyInfo& prop )
    {
        UIScreen* pScreen = findPreviewScreen();
        Widget*   pWidget = pScreen != nullptr ? pScreen->getTree().findWidgetByID( _selected ) : nullptr;
        if ( pWidget == nullptr )
            return;
        setDirty( true );
        if ( prop._containerKind != ContainerKind::None )
        {
            captureBaseline( *pWidget ); // 컨테이너 편집은 되돌리기 항목을 남기지 못한다 — 그 뒤의 값을 기준으로 잡는다
            return;
        }
        const string name    = prop._name.c_str();
        const string newText = UIPreviewLogic::formatWidgetProperty( *pWidget, prop );
        for ( BaselineValue& baseline : _listBaseline )
        {
            if ( baseline._name != name )
                continue;
            if ( baseline._text == newText )
                return;
            const string oldText = baseline._text;
            baseline._text       = newText;
            // 끌어 바꾸는 값은 프레임마다 알려 온다 — 같은 위젯 · 같은 프로퍼티의 연속 편집은 되돌리기 항목 하나로 합친다.
            pushPropertyEdit( _selectedOrdinal, name, oldText, newText, formatToString( "uipreview.%#.%#", _selectedOrdinal, name ) );
            return;
        }
    }

    bool UIPreviewModel::commitSlotEdit( uint32 ordinal, const string& oldText, const string& newText )
    {
        if ( newText == oldText )
            return false;
        pushPropertyEdit( ordinal, "_slot", oldText, newText, string{} );
        setDirty( true );
        _baselineWidget = kInvalidWidgetID; // 인스펙터의 기준 값을 다시 잡는다
        return true;
    }

    void UIPreviewModel::pushPropertyEdit( uint32 ordinal, const string& propertyName, const string& oldText, const string& newText, const string& coalesceKey )
    {
        if ( propertyName == "_slot" )
            ++_slotEditCount;
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, ordinal, propertyName, oldText]()
        {
            pSession->apply( generation, ordinal, propertyName, oldText );
        } ),
                                                          _undoOwner ),
                                 UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, ordinal, propertyName, newText]()
        {
            pSession->apply( generation, ordinal, propertyName, newText );
        } ),
                                                          _undoOwner ),
                                 "Edit UI Widget", coalesceKey );
    }

    void UIPreviewModel::afterStructureEdit( UIScreen& screen, uint32 selectedOrdinal )
    {
        // 위젯이 붙거나 떨어졌다 — 호버 경로가 옛 위젯 번호를 보관하고 있을 수 있어 비우고, 고름과 기준 값을 새 구조에 맞춘다.
        WidgetTree& tree = screen.getTree();
        _pointer.clearHover();
        _bPointerInside  = false;
        _baselineWidget  = kInvalidWidgetID;
        _selectedOrdinal = selectedOrdinal;
        _selected        = selectedOrdinal != UIPreviewLogic::kNoOrdinal ? UIPreviewLogic::findWidgetByOrdinal( tree, selectedOrdinal ) : kInvalidWidgetID;
        setDirty( true );
    }

    void UIPreviewModel::refreshPalette( uint32 frame )
    {
        TypeRegistry* pRegistry = editor::findSubsystem<TypeRegistry>();
        if ( pRegistry != nullptr && ( _listPaletteType.empty() || frame - _paletteFrame > UIPreviewModelInternal::kPaletteRefreshFrames ) )
        {
            UIPreviewLogic::collectPaletteTypes( *pRegistry, _listPaletteType );
            _paletteFrame = frame;
        }
    }

    bool UIPreviewModel::addWidget( UIScreen& screen, const hashed_string& typeName, Widget* pTarget )
    {
        TypeRegistry*   pRegistry = editor::findSubsystem<TypeRegistry>();
        const TypeInfo* pType     = pRegistry != nullptr ? pRegistry->findType( typeName ) : nullptr;
        WidgetTree&     tree      = screen.getTree();
        uint32          index{ 0 };
        PanelWidget*    pParent = UIPreviewLogic::findDropContainer( tree, pTarget, index );
        if ( pType == nullptr || pParent == nullptr )
            return false;
        const uint32 parentOrdinal = UIPreviewLogic::findWidgetOrdinal( tree, pParent->getID() );
        Widget*      pAdded        = UIPreviewLogic::insertWidget( tree, *pType, parentOrdinal, index );
        if ( pAdded == nullptr )
        {
            SW_LOG_WARNING( "UI Preview: a '%#' cannot be added to '%#' (the panel takes no more children or the type cannot be built)", typeName.c_str(),
                            pParent->getName().c_str() );
            return false;
        }
        afterStructureEdit( screen, UIPreviewLogic::findWidgetOrdinal( tree, pAdded->getID() ) );
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, index]()
        {
            pSession->detach( generation, parentOrdinal, index, shared_ptr<RemovedWidget>{} );
        } ),
                                                          _undoOwner ),
                                 UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, typeName, parentOrdinal, index]()
        {
            pSession->insert( generation, typeName, parentOrdinal, index );
        } ),
                                                          _undoOwner ),
                                 "Add UI Widget" );
        return true;
    }

    bool UIPreviewModel::canDeleteSelected( const WidgetTree& tree ) const
    {
        const Widget* pWidget = tree.findWidgetByID( _selected );
        return pWidget != nullptr && pWidget->getParent() != nullptr;
    }

    bool UIPreviewModel::deleteSelected( UIScreen& screen )
    {
        WidgetTree& tree    = screen.getTree();
        Widget*     pWidget = tree.findWidgetByID( _selected );
        if ( pWidget == nullptr || pWidget->getParent() == nullptr )
            return false; // 루트는 지우지 않는다(문서에는 루트가 있어야 한다)
        PanelWidget* pParent = pWidget->getParent();
        const uint32 index   = pParent->findChildIndex( pWidget );
        if ( index == invalid_index::kUint32 )
            return false;
        const uint32 parentOrdinal = UIPreviewLogic::findWidgetOrdinal( tree, pParent->getID() );
        _pointer.clearHover();
        shared_ptr<RemovedWidget> pHolder = sw::make_shared<RemovedWidget>();
        pHolder->_pWidget                 = UIPreviewLogic::detachWidget( tree, parentOrdinal, index );
        if ( pHolder->_pWidget == nullptr )
            return false;
        afterStructureEdit( screen, parentOrdinal );
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, index, pHolder]()
        {
            pSession->reattach( generation, parentOrdinal, index, pHolder );
        } ),
                                                          _undoOwner ),
                                 UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, index, pHolder]()
        {
            pSession->detach( generation, parentOrdinal, index, pHolder );
        } ),
                                                          _undoOwner ),
                                 "Delete UI Widget" );
        return true;
    }

    void UIPreviewModel::reorderChild( uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex )
    {
        if ( fromIndex == toIndex )
            return;
        applyMove( _generation, parentOrdinal, fromIndex, toIndex );
        ++_slotEditCount;
        const uint32                  generation = _generation;
        const shared_ptr<EditSession> pSession   = _pSession;
        EditorTransaction::push( UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, fromIndex, toIndex]()
        {
            pSession->move( generation, parentOrdinal, toIndex, fromIndex );
        } ),
                                                          _undoOwner ),
                                 UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pSession, generation, parentOrdinal, fromIndex, toIndex]()
        {
            pSession->move( generation, parentOrdinal, fromIndex, toIndex );
        } ),
                                                          _undoOwner ),
                                 "Reorder UI Widget" );
    }

    void UIPreviewModel::applyInsert( uint32 generation, const hashed_string& typeName, uint32 parentOrdinal, uint32 index )
    {
        UIScreen*       pScreen   = generation == _generation ? findPreviewScreen() : nullptr;
        TypeRegistry*   pRegistry = editor::findSubsystem<TypeRegistry>();
        const TypeInfo* pType     = pRegistry != nullptr ? pRegistry->findType( typeName ) : nullptr;
        if ( pScreen == nullptr || pType == nullptr )
            return;
        Widget* pAdded = UIPreviewLogic::insertWidget( pScreen->getTree(), *pType, parentOrdinal, index );
        if ( pAdded != nullptr )
            afterStructureEdit( *pScreen, UIPreviewLogic::findWidgetOrdinal( pScreen->getTree(), pAdded->getID() ) );
    }

    void UIPreviewModel::applyDetach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder )
    {
        UIScreen* pScreen = generation == _generation ? findPreviewScreen() : nullptr;
        if ( pScreen == nullptr )
            return;
        _pointer.clearHover();
        unique_ptr<Widget> removed = UIPreviewLogic::detachWidget( pScreen->getTree(), parentOrdinal, index );
        if ( removed == nullptr )
            return;
        if ( pHolder != nullptr )
            pHolder->_pWidget = std::move( removed ); // 지우기를 다시 하면 되돌릴 때 끼울 위젯을 다시 맡는다
        afterStructureEdit( *pScreen, parentOrdinal );
    }

    void UIPreviewModel::applyReattach( uint32 generation, uint32 parentOrdinal, uint32 index, const shared_ptr<RemovedWidget>& pHolder )
    {
        UIScreen*  pScreen             = generation == _generation ? findPreviewScreen() : nullptr;
        const bool bCannotAttachWidget = pScreen == nullptr || pHolder == nullptr || pHolder->_pWidget == nullptr;
        if ( bCannotAttachWidget )
            return;
        Widget* pAttached = UIPreviewLogic::attachWidget( pScreen->getTree(), std::move( pHolder->_pWidget ), parentOrdinal, index );
        if ( pAttached != nullptr )
            afterStructureEdit( *pScreen, UIPreviewLogic::findWidgetOrdinal( pScreen->getTree(), pAttached->getID() ) );
    }

    void UIPreviewModel::applyMove( uint32 generation, uint32 parentOrdinal, uint32 fromIndex, uint32 toIndex )
    {
        UIScreen* pScreen = generation == _generation ? findPreviewScreen() : nullptr;
        if ( pScreen == nullptr || fromIndex == toIndex )
            return;
        // 자식을 분리하고 끼워도 부모의 문서 순서 번호는 그대로다(부모가 자식보다 앞에 온다).
        WidgetTree& tree = pScreen->getTree();
        _pointer.clearHover();
        unique_ptr<Widget> moved = UIPreviewLogic::detachWidget( tree, parentOrdinal, fromIndex );
        if ( moved == nullptr )
            return;
        Widget* pAttached = UIPreviewLogic::attachWidget( tree, std::move( moved ), parentOrdinal, toIndex );
        if ( pAttached != nullptr )
            afterStructureEdit( *pScreen, UIPreviewLogic::findWidgetOrdinal( tree, pAttached->getID() ) );
    }

    void UIPreviewModel::applyEditedText( uint32 generation, uint32 ordinal, const string& propertyName, const string& text )
    {
        if ( generation != _generation )
            return; // 그 뒤에 트리를 다시 지었다 — 옛 항목이 새 트리에 값을 쓰지 않는다
        UIScreen* pScreen = findPreviewScreen();
        if ( pScreen == nullptr )
            return;
        Widget* pWidget = pScreen->getTree().findWidgetByID( UIPreviewLogic::findWidgetByOrdinal( pScreen->getTree(), ordinal ) );
        if ( pWidget == nullptr )
            return;
        const PropertyInfo* pProp = UIPreviewLogic::findWidgetProperty( *pWidget, propertyName );
        if ( pProp == nullptr || UIPreviewLogic::applyWidgetProperty( *pWidget, *pProp, text ) == false )
        {
            SW_LOG_WARNING( "UI Preview: '%#' could not be restored to '%#'", propertyName.c_str(), text.c_str() );
            return;
        }
        setDirty( true );
        if ( _baselineWidget == pWidget->getID() )
        {
            for ( BaselineValue& baseline : _listBaseline )
            {
                if ( baseline._name == propertyName )
                    baseline._text = text;
            }
        }
    }

    void UIPreviewModel::setNavigationMode( bool bNavigation )
    {
        _bNavigationMode = bNavigation;
        UISystem* pUI    = editor::findSubsystem<UISystem>();
        if ( pUI != nullptr )
            pUI->getOffscreenScreens().setNavigationMode( _screen, bNavigation );
    }

    void UIPreviewModel::setInteracting( bool bInteract )
    {
        _bInteract = bInteract;
        setNavigationMode( false );
        if ( bInteract == false )
            _pointer.clearHover();
    }

    bool UIPreviewModel::navigateFocus( UIScreen& screen, uint32 direction )
    {
        setNavigationMode( true );
        WidgetTree& tree = screen.getTree();
        if ( _focus.getFocusedTree() == &tree && tree.getFocusedWidget() != kInvalidWidgetID )
            return _focus.navigate( tree, static_cast<UINavigationDirection>( direction ) );
        // 포커스가 아직 없으면 탐색 방식은 기본 포커스부터 시작한다(게임 화면이 열릴 때와 같다) — 첫 포커스 위젯을 잡는다.
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        for ( const Widget* pWidget : listWidget )
        {
            if ( pWidget->supportsFocus() && _focus.setFocus( tree, pWidget->getID() ) )
                return true;
        }
        return false;
    }

    void UIPreviewModel::acceptFocus( UIScreen& screen )
    {
        WidgetTree&   tree     = screen.getTree();
        const Widget* pFocused = tree.findWidgetByID( tree.getFocusedWidget() );
        if ( pFocused == nullptr )
            return;
        const UIRect   bounds = pFocused->getGeometry().computeScreenBounds();
        UIPointerEvent event{};
        event._position = float2{ ( bounds._left + bounds._right ) * 0.5f, ( bounds._top + bounds._bottom ) * 0.5f };
        event._kind     = UIPointerEventKind::Move;
        (void)_pointer.process( tree, event );
        event._kind = UIPointerEventKind::Down;
        (void)_pointer.process( tree, event );
        event._kind = UIPointerEventKind::Up;
        (void)_pointer.process( tree, event );
        _lastPointer = event._position;
        markPreviewMutated();
    }

    void UIPreviewModel::feedPointer( UIScreen& screen, const UIPreviewPointerInput& input )
    {
        WidgetTree&    tree = screen.getTree();
        UIPointerEvent event{};
        event._position = input._position;
        event._delta    = float2{ event._position._x - _lastPointer._x, event._position._y - _lastPointer._y };
        if ( input._bHovered )
        {
            const bool bPointerEnteredOrMoved = _bPointerInside == false || event._delta._x != 0.0f || event._delta._y != 0.0f;
            if ( bPointerEnteredOrMoved )
            {
                if ( _bNavigationMode )
                    setNavigationMode( false ); // 마우스를 움직이면 포인터 방식으로 돌아간다(포커스 테두리를 숨긴다)
                event._kind = UIPointerEventKind::Move;
                (void)_pointer.process( tree, event );
                _lastPointer = event._position;
            }
            _bPointerInside = true;
            if ( input._wheel != 0.0f )
            {
                event._kind  = UIPointerEventKind::Wheel;
                event._wheel = input._wheel;
                (void)_pointer.process( tree, event );
                markPreviewMutated();
            }
        }
        else if ( _bPointerInside )
        {
            if ( _bPointerPressed == false )
                _pointer.clearHover();
            _bPointerInside = false;
        }

        for ( uint32 buttonIndex = 0; buttonIndex < UIPreviewPointerInput::kButtonCount; ++buttonIndex )
        {
            event._button = UIPreviewModelInternal::kArrButton[buttonIndex];
            if ( input._bHovered && input._arrPressed[buttonIndex] )
            {
                event._kind                  = UIPointerEventKind::Down;
                const UIPointerResult result = _pointer.process( tree, event );
                if ( result._focusRequest != kInvalidWidgetID )
                    (void)_focus.setFocus( tree, result._focusRequest );
                _bPointerPressed = true;
                markPreviewMutated();
            }
            // 이미지 밖에서 놓은 버튼도 위젯이 잡고 있으면 Up 을 보낸다(슬라이더를 끌다가 바깥에서 놓는 경우).
            if ( input._arrReleased[buttonIndex] && ( input._bHovered || _bPointerPressed ) )
            {
                event._kind = UIPointerEventKind::Up;
                (void)_pointer.process( tree, event );
                _bPointerPressed = false;
                markPreviewMutated();
            }
        }
    }

    const hashed_string& UIPreviewModel::syncAnimation( UIAnimationPlayer& player )
    {
        const vector<UIAnimation>& listAnimation = player.getAnimations();
        if ( listAnimation.empty() )
            return _animation;
        if ( _animation.empty() || player.findAnimation( _animation ) == nullptr )
            _animation = listAnimation[0]._name;
        return _animation;
    }

    void UIPreviewModel::chooseAnimation( UIAnimationPlayer& player, const hashed_string& name )
    {
        player.stopAll();
        _animation = name;
    }

    void UIPreviewModel::playAnimation( UIAnimationPlayer& player )
    {
        _bAnimationPaused = false;
        (void)player.play( _animation, 1.0f, _bAnimationLoop ? 0u : 1u );
        markPreviewMutated();
    }

    void UIPreviewModel::togglePauseAnimation( UIAnimationPlayer& player )
    {
        _bAnimationPaused = !_bAnimationPaused;
        player.setPaused( _animation, _bAnimationPaused );
    }

    void UIPreviewModel::stopAnimation( UIAnimationPlayer& player )
    {
        player.stop( _animation );
        _bAnimationPaused = false;
    }

    bool UIPreviewModel::readAnimationTime( const UIAnimationPlayer& player, float32& outTime, float32& outDuration )
    {
        outTime            = 0.0f;
        outDuration        = 0.0f;
        const bool bActive = player.getPlaybackTime( _animation, outTime, outDuration );
        if ( bActive )
        {
            _animationScrub = outTime;
            return true;
        }
        const UIAnimation* pChosen = player.findAnimation( _animation );
        outDuration                = pChosen != nullptr ? pChosen->computeDuration() : 0.0f;
        outTime                    = MathUtil::clamp( _animationScrub, 0.0f, outDuration );
        return false;
    }

    void UIPreviewModel::scrubAnimation( UIAnimationPlayer& player, float32 seconds )
    {
        _animationScrub   = seconds;
        _bAnimationPaused = true;
        (void)player.seek( _animation, seconds );
        markPreviewMutated();
    }

    void UIPreviewModel::updateProbe( const UIScreen* pScreen, bool bDirty, bool bTextureReady )
    {
        _probe = UIPreviewProbe{};
        if ( pScreen == nullptr )
            return;
        const WidgetTree& tree     = pScreen->getTree();
        _probe._bValid             = true;
        _probe._bDirty             = bDirty;
        _probe._bReloadedAfterSave = _bReloadedAfterSave;
        _probe._bTextureReady      = bTextureReady;
        _probe._languageIndex      = _languageIndex;
        const Widget* pRoot        = tree.getRoot();
        _probe._bRightToLeft       = pRoot != nullptr && pRoot->isRightToLeft();
        vector<Widget*> listWidget;
        tree.collectWidgetsInDocumentOrder( listWidget );
        _probe._widgetCount = static_cast<uint32>( listWidget.size() );
        for ( const Widget* pWidget : listWidget )
        {
            const TextWidget* pText = castTo<const TextWidget>( pWidget );
            if ( pText != nullptr )
            {
                _probe._firstTextBytes = static_cast<uint32>( pText->getDisplayText().size() );
                break;
            }
        }
        if ( _pointer.getHoverTree() == &tree )
        {
            const Widget* pHovered = tree.findWidgetByID( _pointer.getHoveredWidget() );
            if ( pHovered != nullptr )
            {
                _probe._hoveredOrdinal    = static_cast<int32>( UIPreviewLogic::findWidgetOrdinal( tree, pHovered->getID() ) );
                _probe._bHoveredFocusable = pHovered->supportsFocus();
            }
        }
        if ( tree.getFocusedWidget() != kInvalidWidgetID )
        {
            const uint32 focused = UIPreviewLogic::findWidgetOrdinal( tree, tree.getFocusedWidget() );
            if ( focused != UIPreviewLogic::kNoOrdinal )
                _probe._focusedOrdinal = static_cast<int32>( focused );
        }
        const Widget* pSelected = tree.findWidgetByID( _selected );
        if ( pSelected != nullptr )
        {
            _probe._bSelectedValid     = true;
            _probe._selectedOpacity    = pSelected->getOpacity();
            _probe._bSlotEditable      = UIPreviewLogic::canEditSlotByHandle( *pSelected );
            _probe._selectedOffsetMinX = pSelected->getLayoutSlot()._offsetMin._x;
        }
        _probe._paletteCount  = static_cast<uint32>( _listPaletteType.size() );
        _probe._slotEditCount = _slotEditCount;
        float32 time{ 0.0f };
        float32 duration{ 0.0f };
        if ( pScreen->getAnimationPlayer().getPlaybackTime( _animation, time, duration ) )
        {
            _probe._animationTime     = time;
            _probe._bAnimationPlaying = _bAnimationPaused == false;
        }
    }
} // namespace sw::editor
