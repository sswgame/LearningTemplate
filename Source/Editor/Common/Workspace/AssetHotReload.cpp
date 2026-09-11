#include "pch.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/TextureBaker.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw::editor
{
    namespace
    {
        struct AssetHotReloadInternal
        {
            /** @brief 소스 이미지를 두는 폴더. 구운 DDS 는 옆 `textures/` 로 간다. */
            inline static constexpr const utf8* _s_pRawTextureFolder = "textures_raw";

            /** @brief 에셋 종류 하나를 어떻게 다시 읽을지. */
            struct ReloadRule
            {
                EditorAssetKind _kind;
                void ( *_pfnReload )( string_view relativePath );
            };

            static void reloadMaterial( string_view relativePath )
            {
                // 에디터는 `EngineServices.h` 를 볼 수 없다(모듈 경계). 호스트가 꽂아 준 서비스로 간다.
                ResourceManager* pResources = getService<ResourceManager>();
                if ( pResources != nullptr )
                    pResources->getMaterialManager().reload( relativePath );
            }

            static void reloadPrefab( string_view relativePath )
            {
                ResourceManager* pResources = getService<ResourceManager>();
                if ( pResources == nullptr )
                    return;

                // 캐시만 버린다 — 이미 스폰된 오브젝트는 그대로다(그건 오버라이드 전파라는 다른 기능이다).
                pResources->getPrefabManager().reload( relativePath );
            }

            /** @brief `textures_raw/` 아래 소스 이미지를 옆 `textures/` 의 DDS 로 굽습니다. */
            static bool bakeSourceImage( string_view relativePath )
            {
                const string absPath = FileUtil::joinPath( ResourceUtil::getRootFolderPath(), relativePath );
                const size_t rawPos  = FileUtil::normalizeSeparators( absPath ).find( _s_pRawTextureFolder );
                if ( rawPos == string::npos )
                    return false;

                const string normalized = FileUtil::normalizeSeparators( absPath );
                const string outputPath = FileUtil::replaceExtension(
                    normalized.substr( 0, rawPos ) + "textures" + normalized.substr( rawPos + strlen( _s_pRawTextureFolder ) ), ".dds" );

                // 설정은 매번 읽는다 — 작은 JSON 이고, 사람이 이미지를 저장했을 때만 온다.
                // 한 번 읽어 캐시하면 임포트 규칙을 고쳐도 재시작 전까지 반영되지 않는다.
                TextureImportConfig config{};
                config.loadFromFile( EditorUtil::resolveEditorConfigFile( getEditorData()._textureImportConfigFile.c_str() ) );
                if ( TextureBaker::bakeTextureWithConfig( normalized, outputPath, config ) == false )
                {
                    SW_LOG_ERROR( "텍스처 베이크 실패: %#", relativePath );
                    return false;
                }

                // 구운 DDS 가 감시 대상이라, 그 쓰기가 다시 이벤트로 돌아와 캐시를 갱신한다.
                SW_LOG_INFO( "텍스처를 구웠습니다: %# -> %#", relativePath, outputPath.c_str() );
                return true;
            }

            static void reloadTexture( string_view relativePath )
            {
                // 런타임이 읽는 것은 DDS 뿐이다(`Texture2D::loadFromResource` -> `DdsLoader`).
                // 소스 이미지는 **굽는 것**이 리로드다 — 구운 결과가 다음 이벤트로 돌아온다.
                if ( FileUtil::hasExtension( relativePath, ".dds" ) )
                {
                    ResourceManager* pResources = getService<ResourceManager>();
                    EditorContext*   pContext   = EditorContext::get();
                    if ( pResources == nullptr || pContext == nullptr )
                        return;
                    pResources->getTextureManager().reload( relativePath, pContext->getRhiDevice() );
                    return;
                }

                if ( FileUtil::hasExtension( relativePath, ".hdr" ) )
                {
                    // 굽지 않는다. 디코더가 stb_image 의 8비트 경로라(`ImageUtil::loadImageFromMemory`)
                    // HDR 을 구우면 값이 잘려 나간다 — 조용히 망가뜨리느니 하지 않는다고 말한다.
                    SW_LOG_WARNING( "HDR 은 자동 베이크 대상이 아닙니다 (8비트로 잘린다): %#", relativePath );
                    return;
                }

                if ( bakeSourceImage( relativePath ) == false )
                {
                    // `textures_raw/` 밖의 소스 이미지는 굽는 규칙이 없다. 어디에 둬야 하는지 말해 준다.
                    SW_LOG_WARNING( "소스 이미지는 `%#` 아래에 있어야 구워집니다: %#", _s_pRawTextureFolder, relativePath );
                }
            }

            // **다시 읽는 방법이 있는 종류만** 여기 있다. 확장자는 여기 적지 않는다 —
            // 그것은 `EditorAssetTypeRegistry` 의 일이고, 목록이 둘이면 한쪽만 늘어난다.
            // (실제로 예전 감시는 `.mat` 만 보고 있었고, 저장소의 에셋은 전부 `.material` 이라
            //  머티리얼 핫리로드가 한 번도 걸린 적이 없다.)
            inline static constexpr ReloadRule _s_arrReloadRule[] = {
                {EditorAssetKind::Material, &reloadMaterial},
                { EditorAssetKind::Texture,  &reloadTexture},
                {  EditorAssetKind::Prefab,   &reloadPrefab},
            };

            /** @brief 처리기가 있는 종류의 확장자를 전부 모읍니다. */
            static vector<string> collectHandledExtensions()
            {
                vector<string> listExtension{};
                for ( const ReloadRule& rule : _s_arrReloadRule )
                    EditorAssetTypeRegistry::appendSuffixes( rule._kind, listExtension );
                return listExtension;
            }

            /** @brief 확장자가 목록에 있으면 true (대소문자 무시). */
            static bool contains( const vector<string>& listExtension, string_view extension )
            {
                for ( const string& candidate : listExtension )
                {
                    if ( StringUtil::equals( candidate, extension, true ) )
                        return true;
                }
                return false;
            }

            /**
             * @brief 실제로 감시할 확장자 목록을 정합니다.
             * @details `editordata.json` 의 `_listHotReloadExtension` 이 정책이고, 처리기 표가 한계다.
             *          설정이 비어 있으면 처리기가 있는 확장자 전부를 본다. 설정에 처리기 없는
             *          확장자가 적혀 있으면 **경고를 남기고 뺀다** — 예전처럼 이벤트만 받아
             *          조용히 버리면, 감시 비용을 내면서 "리로드가 안 된다" 만 남는다.
             */
            static vector<string> resolveWatchExtensions()
            {
                const vector<string>  listHandled    = collectHandledExtensions();
                const vector<string>& listConfigured = getEditorData()._listHotReloadExtension;
                if ( listConfigured.empty() )
                    return listHandled;

                vector<string> listExtension{};
                listExtension.reserve( listConfigured.size() );
                for ( const string& extension : listConfigured )
                {
                    if ( contains( listHandled, extension ) )
                    {
                        listExtension.push_back( extension );
                        continue;
                    }
                    SW_LOG_WARNING( "핫리로드 확장자 '%#' 는 처리기가 없어 무시합니다 (editordata.json)", extension.c_str() );
                }
                return listExtension;
            }
        };
    } // namespace

    SW_LOG_CALLER( "AssetHotReload" );

    AssetHotReload::AssetHotReload()
        : _pReloadFileManager{ nullptr }
        , _resourceWatchHandle{}
    {
    }

    AssetHotReload::~AssetHotReload()
    {
        shutdown();
    }

    bool AssetHotReload::initialize()
    {
        shutdown();

        const vector<string> listExtension = AssetHotReloadInternal::resolveWatchExtensions();
        if ( listExtension.empty() )
        {
            SW_LOG_INFO( "에셋 핫리로드: 감시할 확장자가 없어 켜지 않습니다." );
            return false;
        }

        _pReloadFileManager = make_unique<ReloadFileManager>();
        if ( _pReloadFileManager->initialize() == false )
        {
            _pReloadFileManager.reset();
            return false;
        }

        FileWatchMatchDelegate fileWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AssetHotReload::onResourceFileChanged, this ) };
        // 감시 접두어는 **절대 경로**여야 한다. 워처가 올리는 이벤트의 `_directory` 는 워처가
        // 열어 둔 절대 경로(= 리소스 루트)이고, 접두어 비교는 그 둘을 그대로 맞춰 본다.
        // 예전에는 `"Resource/"` 라는 상대 접두어를 줘서 비교가 **항상** 실패했다 —
        // 확장자가 `.mat` 이었던 것과 겹쳐, 에셋 핫리로드는 한 번도 걸린 적이 없다.
        _resourceWatchHandle = _pReloadFileManager->registerWatch( ResourceUtil::getRootFolderPath(), listExtension, fileWatchDelegate );
        return _resourceWatchHandle.isValid();
    }

    void AssetHotReload::shutdown()
    {
        if ( _pReloadFileManager == nullptr )
            return;

        if ( _resourceWatchHandle.isValid() )
            _pReloadFileManager->unregisterWatch( _resourceWatchHandle );
        _resourceWatchHandle = {};

        _pReloadFileManager->shutdown();
        _pReloadFileManager.reset();
    }

    void AssetHotReload::update()
    {
        if ( _pReloadFileManager != nullptr )
            _pReloadFileManager->update();
    }

    void AssetHotReload::onResourceFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action != FileWatcherAction::Modified )
            return;

        string relPath{};
        if ( FileUtil::makePathRelative( ResourceUtil::getRootFolderPath(), FileUtil::joinPath( changeEvent._directory, changeEvent._filename ), relPath ) == false )
            return;

        if ( relPath.empty() )
            return;

        // 종류 판별도 `EditorAssetTypeRegistry` 가 한다 — 감시 필터와 같은 정본을 쓰므로
        // "필터는 통과했는데 처리기가 없다" 가 생길 수 없다.
        for ( const AssetHotReloadInternal::ReloadRule& rule : AssetHotReloadInternal::_s_arrReloadRule )
        {
            if ( EditorAssetTypeRegistry::matches( rule._kind, relPath ) == false )
                continue;

            SW_LOG_INFO( "Hot-Reloading asset: %#", relPath.c_str() );
            rule._pfnReload( relPath );
            return;
        }
    }
} // namespace sw::editor
