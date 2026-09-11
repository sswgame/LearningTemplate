#include "pch.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw::editor
{
    namespace
    {
        struct AssetHotReloadInternal
        {
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

            // **다시 읽는 방법이 있는 종류만** 여기 있다. 확장자는 여기 적지 않는다 —
            // 그것은 `EditorAssetTypeRegistry` 의 일이고, 목록이 둘이면 한쪽만 늘어난다.
            // (실제로 예전 감시는 `.mat` 만 보고 있었고, 저장소의 에셋은 전부 `.material` 이라
            //  머티리얼 핫리로드가 한 번도 걸린 적이 없다.)
            inline static constexpr ReloadRule _s_arrReloadRule[] = {
                { EditorAssetKind::Material, &reloadMaterial },
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
