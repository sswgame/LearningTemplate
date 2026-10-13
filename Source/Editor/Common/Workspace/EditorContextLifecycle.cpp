/**
 * @file EditorContextLifecycle.cpp
 * @brief EditorContext 의 생성 · 초기화 · 종료입니다. 에디터 UI 매니저들을 실제로 만드는 곳입니다.
 *
 * @details EditorContext::get() 과 한 파일에 두지 말 것 — 그러면 **컨텍스트를 조회하기만 해도** 패널 · 팝업 ·
 *          인스펙터 매니저가 모두 링크에 끌려옵니다(그 끝은 ImGui 입니다). 생성 · 소멸(매니저 타입이 완전해야 하는 쪽)만
 *          이 TU 에 두어, EditorContext 를 조회만 하는 코드(EditorSceneCommands 단위 테스트 등)가 UI 없이 링크되게 합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorPaths.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorPanelHost.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorModuleUnloadListener.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorContextLifecycleInternal
        {
            /** @brief R8G8B8A8_UNORM 텍스처 전체를 한 색으로 올립니다. 올린 텍스처는 셰이더 읽기 상태입니다. */
            static bool fillTextureWithColor( IRHIResourceFactory& resource, RHITextureHandle texture, uint32 width, uint32 height, const float4& color )
            {
                // 메모리에서 R · G · B · A 순서가 되도록 낮은 바이트부터 채운다(엔진은 리틀 엔디언 64 비트만 짓는다).
                const uint32 packedColor = static_cast<uint32>( toUnorm8( color._x ) ) | ( static_cast<uint32>( toUnorm8( color._y ) ) << 8 ) |
                                           ( static_cast<uint32>( toUnorm8( color._z ) ) << 16 ) | ( static_cast<uint32>( toUnorm8( color._w ) ) << 24 );
                const vector<uint32> listPixel( static_cast<size_t>( width ) * height, packedColor );

                RHITextureUploadDesc upload{};
                upload._pData     = listPixel.data();
                upload._sizeBytes = static_cast<uint32>( listPixel.size() * sizeof( uint32 ) );
                upload._mipLevels = 1;
                return resource.uploadTexture2D( texture, upload );
            }

            static uint8 toUnorm8( float32 value ) { return static_cast<uint8>( MathUtil::round( MathUtil::saturate( value ) * 255.0f ) ); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorContext::EditorContext()
        : _subsystems{}
        , _pEditorSelection{ nullptr }
        , _pWorkspace{ nullptr }
        , _pNotificationManager{ nullptr }
        , _pCommandRegistry{ nullptr }
        , _pPanelManager{ nullptr }
        , _pPanelHost{ nullptr }
        , _pPopupManager{ nullptr }
        , _pAssetHotReload{ nullptr }
        , _pConfigHotReload{ nullptr }
        , _pAssetValidation{ nullptr }
        , _pSourceControl{ nullptr }
        , _pInspectorComponentManager{ nullptr }
        , _pInspectorPropertyManager{ nullptr }
        , _pDefaultObjects{ nullptr }
        , _pNetPlaySession{ nullptr }
        , _pRHIDevice{ nullptr }
        , _pDockLayout{ nullptr }
        , _pRendererBackend{ nullptr }
        , _arrViewTarget{}
        , _bGameViewHovered{ SW_FALSE }
        , _bGameViewFocused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorContext::~EditorContext()
    {
        shutdown();
    }

    void EditorContext::initialize()
    {
        // 하위 객체는 목록이 소유한다. 넣는 순서는 뜻이 없고, 초기화 순서는 각자 선언한 의존이 정하며 종료 · 소멸은 그 역순이다.
        // 애셋 · 설정 핫 리로드(개발 기능)는 에디터가 켜져 있을 때만 감시하고, 버전 관리는 git LFS 를 비동기로 묻는다(각 initializeSubsystem).
        _pEditorSelection           = &_subsystems.add( make_unique<EditorSelection>() );
        _pWorkspace                 = &_subsystems.add( make_unique<EditorWorkspace>( _pEditorSelection ) );
        _pNotificationManager       = &_subsystems.add( make_unique<EditorNotificationManager>() );
        _pCommandRegistry           = &_subsystems.add( make_unique<EditorCommandRegistry>() );
        _pPanelManager              = &_subsystems.add( make_unique<EditorPanelManager>() );
        _pPanelHost                 = &_subsystems.add( make_unique<EditorPanelHost>( *this, *_pPanelManager ) );
        _pPopupManager              = &_subsystems.add( make_unique<EditorPopupManager>() );
        _pAssetHotReload            = &_subsystems.add( make_unique<AssetHotReload>() );
        _pConfigHotReload           = &_subsystems.add( make_unique<ConfigHotReload>() );
        _pAssetValidation           = &_subsystems.add( make_unique<EditorAssetValidation>() );
        _pSourceControl             = &_subsystems.add( sw::make_unique<EditorSourceControl>( EditorPaths::getProjectRootPath() ) );
        _pInspectorComponentManager = &_subsystems.add( make_unique<InspectorComponentManager>() );
        _pInspectorPropertyManager  = &_subsystems.add( make_unique<InspectorPropertyManager>() );
        _pDefaultObjects            = &_subsystems.add( make_unique<EditorDefaultObjects>() );
        _pNetPlaySession            = &_subsystems.add( make_unique<EditorNetPlaySession>() );
        // 확장 모듈이 언로드되기 전에 그 모듈의 등록 줄로 만든 인스턴스를 지운다 — 훑는 매니저들에 의존하므로 그들보다 먼저 내려간다.
        _subsystems.add( make_unique<EditorModuleUnloadListener>( *this ) );

        setActive( this );

        // 에디터 UI 문구 — 엔진 표와 따로 올려 에디터가 있는 동안만 쓰인다(없으면 SW_LOCTEXT 가 영어 원문을 돌려준다).
        if ( LocalizationManager* pLocalization = getService<LocalizationManager>() )
        {
            if ( pLocalization->mountProject( EditorPaths::kLocalizationProject, LocalizationScope::Editor ) == false )
                SW_LOG_WARNING( "Editor localization project '%#' is not (fully) loaded", EditorPaths::kLocalizationProject );
        }

        if ( _subsystems.initializeAll( EngineInitTarget::Client ) == false )
            SW_LOG_ERROR( "Editor subsystems did not all initialize" );
    }

    void EditorContext::shutdown()
    {
        destroyViewTargets();

        if ( s_pActiveContext == this )
            setActive( nullptr );

        if ( LocalizationManager* pLocalization = getService<LocalizationManager>() )
            pLocalization->unmountProjects( LocalizationScope::Editor );

        // 초기화한 것을 역순으로 종료하고, 모두를 역순으로 소멸시킨다(넷 플레이 세션의 자식 프로세스 종료도 여기 — 엔진 로거는 아직 있다).
        _subsystems.destroyAll();
        _pRendererBackend = nullptr;
        _pRHIDevice       = nullptr;
    }

    void EditorContext::syncExtensionRegistrations()
    {
        _pPanelManager->syncWithRegistry( _pRHIDevice );
        _pPopupManager->syncWithRegistry();
        _pInspectorComponentManager->syncWithRegistry();
        _pInspectorPropertyManager->syncWithRegistry();
        EditorCommandGUI::syncWithRegistry();
    }

    void EditorContext::clearViewDrawnMarks()
    {
        for ( EditorViewTarget& view : _arrViewTarget )
        {
            view._bDrawn = SW_FALSE;
        }
    }

    void EditorContext::destroyViewTargets()
    {
        for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( EditorViewKind::Count ); ++kindIndex )
        {
            destroyViewTarget( static_cast<EditorViewKind>( kindIndex ) );
        }
    }

    void EditorContext::destroyViewTarget( EditorViewKind kind )
    {
        EditorViewTarget& view = _arrViewTarget[static_cast<uint32>( kind )];
        if ( view._pTextureID != nullptr && _pRendererBackend != nullptr )
        {
            _pRendererBackend->unregisterTexture( view._pTextureID );
            view._pTextureID = nullptr;
        }

        if ( view._renderTarget != 0 && _pRHIDevice != nullptr && _pRHIDevice->getResourceFactory() != nullptr )
        {
            // 줄 서 있는 패킷이 이 렌더 타깃에 그리고, 이미 낸 draw 스냅샷이 그것을 샘플링한다. ImGui 텍스처와 같은 큐에 맡겨 그 프레임들의
            // GPU 완료 뒤에 부순다. 렌더러 백엔드가 없으면(그릴 쪽이 없다) 곧바로 부순다.
            IRHIResourceFactory*   pResource    = _pRHIDevice->getResourceFactory();
            const RHITextureHandle renderTarget = view._renderTarget;
            if ( _pRendererBackend != nullptr )
            {
                _pRendererBackend->getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [pResource, renderTarget]()
                { pResource->destroyTexture( renderTarget ); } ) );
            }
            else
            {
                pResource->destroyTexture( renderTarget );
            }
            view._renderTarget = 0;
        }

        view._width  = 0;
        view._height = 0;
    }

    void EditorContext::ensureViewTargetSize( EditorViewKind kind, uint32 width, uint32 height )
    {
        EditorViewTarget& view = _arrViewTarget[static_cast<uint32>( kind )];
        if ( width == 0 || height == 0 )
            return;
        if ( width == view._width && height == view._height && view._renderTarget != 0 )
            return;
        if ( _pRHIDevice == nullptr || _pRHIDevice->getResourceFactory() == nullptr )
            return;

        destroyViewTarget( kind );

        // editortooldefaults.json 의 _clearColor 를 쓴다(값을 여기 박아 두면 설정 파일이 조용히 무시된다).
        const float4 viewClearColor = editor::getEditorToolDefaults()._clearColor;

        RHITextureDesc rtDesc{};
        rtDesc._width             = width;
        rtDesc._height            = height;
        rtDesc._format            = RHIFormat::R8G8B8A8_UNORM;
        rtDesc._bIsRenderTarget   = SW_TRUE;
        rtDesc._bIsShaderResource = SW_TRUE;
        rtDesc._mipLevels         = 1;
        rtDesc._clearColor        = viewClearColor;

        view._renderTarget = _pRHIDevice->getResourceFactory()->createTexture2D( rtDesc );
        if ( view._renderTarget == 0 )
            return;

        // 이번 프레임의 draw 스냅샷이 새 텍스처를 그리는데, 그 스냅샷은 이 렌더 타깃에 그릴 패킷보다 먼저 줄 선 패킷이 그릴 수 있다. 렌더러가 아직
        // 쓰지 않은 텍스처를 샘플링하지 않도록 클리어 색으로 채워 셰이더 읽기 상태로 둔다(Vulkan 은 UNDEFINED 레이아웃 샘플링이 검증 Error 다).
        // 닫혀 있다 열린 패널의 첫 프레임도 이 색이다(그 뷰는 보이는 동안만 그린다).
        if ( EditorContextLifecycleInternal::fillTextureWithColor( *_pRHIDevice->getResourceFactory(), view._renderTarget, width, height, viewClearColor ) == false )
            SW_LOG_WARNING( "View target %#x%# could not be cleared before its first frame", width, height );

        view._width  = width;
        view._height = height;
        if ( _pRendererBackend != nullptr )
            view._pTextureID = _pRendererBackend->registerTexture( view._renderTarget );
    }
} // namespace sw::editor
