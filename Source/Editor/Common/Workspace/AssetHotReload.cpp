#include "pch.h"

#include "Editor/Common/Workspace/AssetHotReload.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/IAssetCache.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct AssetHotReloadInternal
        {
            /** @brief 에셋 경로 프로퍼티(`PROPERTY( AssetPath )` · `AssetType = …`)의 값입니다. 경로 프로퍼티가 아니거나 문자열이 아니면 빈 값입니다. */
            static string_view readAssetPath( const PropertyInfo& prop, const Component* pComponent )
            {
                static const hashed_string s_string( "string" );
                static const hashed_string s_hashedString( "hashed_string" );
                if ( prop._metadata._bAssetPath == SW_FALSE && prop._metadata._assetType.empty() )
                    return {};
                if ( prop._typeName == s_string )
                {
                    const string* pValue = prop.getValuePtr<string>( pComponent );
                    return pValue != nullptr ? string_view( *pValue ) : string_view{};
                }
                if ( prop._typeName == s_hashedString )
                {
                    const hashed_string* pValue = prop.getValuePtr<hashed_string>( pComponent );
                    return pValue != nullptr ? string_view( pValue->c_str() ) : string_view{};
                }
                return {};
            }
            /** @brief 확장자가 목록에 있으면 true 입니다(대소문자 무시). */
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
             * @details `editortooldefaults.json` 의 `_listHotReloadExtension` 이 정책이고, 다시 읽을 캐시가 있는 종류(`EditorAssetTypeRegistry`)가 한계입니다.
             *          설정이 비어 있으면 그 종류 전부를 봅니다. 설정에 처리할 수 없는 확장자가 있으면 경고하고 뺍니다.
             */
            static vector<string> resolveWatchExtensions()
            {
                // 반환 객체는 하나다 — 두 지역 변수를 각각 return 하면 NRVO 가 빠진다(`-Wnrvo`).
                vector<string>        listExtension{};
                const vector<string>& listConfigured = getEditorToolDefaults()._listHotReloadExtension;
                if ( listConfigured.empty() )
                {
                    EditorAssetTypeRegistry::appendReloadableSuffixes( listExtension );
                    return listExtension;
                }

                vector<string> listHandled{};
                EditorAssetTypeRegistry::appendReloadableSuffixes( listHandled );
                listExtension.reserve( listConfigured.size() );
                for ( const string& extension : listConfigured )
                {
                    if ( contains( listHandled, extension ) )
                    {
                        listExtension.push_back( extension );
                        continue;
                    }
                    SW_LOG_WARNING( "핫리로드 확장자 '%#' 는 처리기가 없어 무시합니다 (editortooldefaults.json)", extension.c_str() );
                }
                return listExtension;
            }
        };
    } // namespace

    SW_LOG_CALLER( "AssetHotReload" );

    AssetHotReload::AssetHotReload()
        : _pFileWatchDispatcher{ nullptr }
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

        _pFileWatchDispatcher = make_unique<FileWatchDispatcher>();
        if ( _pFileWatchDispatcher->initialize() == false )
        {
            _pFileWatchDispatcher.reset();
            return false;
        }

        FileWatchMatchDelegate fileWatchDelegate{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AssetHotReload::onResourceFileChanged, this ) };
        // 감시 접두어는 **절대 경로**여야 한다 — 이벤트의 `_directory` 는 감시자가 열어 둔 절대 경로(리소스 루트)이고,
        // 접두어 비교는 그 둘을 그대로 맞춰 본다(상대 경로를 주면 비교가 항상 실패한다).
        _resourceWatchHandle = _pFileWatchDispatcher->registerWatch( ResourceUtil::getRootFolderPath(), listExtension, fileWatchDelegate );
        return _resourceWatchHandle.isValid();
    }

    void AssetHotReload::shutdown()
    {
        if ( _pFileWatchDispatcher == nullptr )
            return;

        if ( _resourceWatchHandle.isValid() )
            _pFileWatchDispatcher->unregisterWatch( _resourceWatchHandle );
        _resourceWatchHandle = {};

        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    void AssetHotReload::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
    }

    void AssetHotReload::onResourceFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action != FileWatcherAction::Modified )
            return;

        string relPath{};
        if ( FileUtil::makeRelativePath( ResourceUtil::getRootFolderPath(), FileUtil::joinPath( changeEvent._directory, changeEvent._filename ), relPath ) == false )
            return;

        if ( relPath.empty() )
            return;

        (void)reloadChangedAsset( relPath );
    }

    bool AssetHotReload::reloadChangedAsset( string_view relativePath )
    {
        const AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( relativePath );
        // 소스를 임포트하는 종류는 임포트하는 것이 리로드다 — 임포트된 결과의 쓰기가 다음 감시 이벤트로 온다(그 결과를 읽는 캐시가 없어도 임포트는 한다).
        if ( route._pfnImportSource != nullptr && route._pfnImportSource( relativePath ) )
            return true;
        if ( route._pCacheKindName == nullptr )
            return false;

        // 에디터는 `EngineServices.h` 를 볼 수 없다(모듈 경계). 호스트가 넘겨준 서비스를 쓴다.
        AssetManager* pResources = getService<AssetManager>();
        IAssetCache*  pCache     = ( pResources != nullptr ) ? pResources->findAssetCache( route._pCacheKindName ) : nullptr;
        if ( pCache == nullptr )
        {
            SW_LOG_WARNING( "Hot reload: no asset cache '%#' is registered for %#", route._pCacheKindName, relativePath );
            return false;
        }

        SW_LOG_INFO( "Hot-Reloading asset: %#", relativePath );
        // 디바이스는 **지금 것을** 넘긴다 — 캐시가 마지막으로 본 디바이스는 백엔드 교체 뒤 사라졌을 수 있다.
        EditorContext* pContext = EditorContext::get();
        pCache->reload( relativePath, ( pContext != nullptr ) ? pContext->getRhiDevice() : nullptr );

        Scene*             pScene   = getActiveScene();
        GameObjectManager* pObjects = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects != nullptr )
            (void)notifyAssetUsers( *pObjects, relativePath );
        return true;
    }

    uint32 AssetHotReload::notifyAssetUsers( GameObjectManager& objects, string_view relativePath )
    {
        const string reloaded = FileUtil::normalizeSeparators( relativePath );
        uint32       notified = 0;
        objects.forEachGameObject( [&reloaded, &notified]( GameObject* pObject )
        {
            for ( Component* pComponent : pObject->getComponents() )
            {
                if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                    continue;
                const TypeInfo* pType = pComponent->getTypeInfo();
                if ( pType == nullptr )
                    continue;
                pType->forEachProperty( [pComponent, &reloaded, &notified]( const PropertyInfo& prop )
                {
                    const string_view path = AssetHotReloadInternal::readAssetPath( prop, pComponent );
                    if ( path.empty() || StringUtil::equals( FileUtil::normalizeSeparators( path ), reloaded, true ) == false )
                        return;
                    pComponent->onPropertyChanged( prop._name );
                    ++notified;
                }, true );
            }
        } );
        return notified;
    }
} // namespace sw::editor
