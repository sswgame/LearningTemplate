#include "pch.h"

#include "Engine/Scene/SceneCooker.h"

#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    namespace
    {
        struct SceneCookerInternal
        {
            /** @brief 오브젝트의 컴포넌트 타입 이름을 순서대로 이어 붙인 지문입니다. */
            static string makeComponentFingerprint( const GameObject* pGameObject )
            {
                string fingerprint;
                if ( pGameObject == nullptr )
                    return fingerprint;

                for ( const Component* pComp : pGameObject->getComponents() )
                {
                    const TypeInfo* pTypeInfo = ( pComp != nullptr ) ? pComp->getTypeInfo() : nullptr;
                    fingerprint += ( pTypeInfo != nullptr ) ? pTypeInfo->_name.c_str() : "<null>";
                    fingerprint += ';';
                }
                return fingerprint;
            }

            /**
             * @brief XML 상태 하나를 바이너리로 굽고, 되읽어 같은 구성이 나오는지 확인합니다.
             * @return 검증까지 통과했으면 true. 이때만 `outState` 가 채워집니다.
             */
            static bool cookOneEntityState( GameObjectManager& manager, string_view entityName,
                                            const string& embeddedXml, vector<uint8>& outState )
            {
                outState.clear();

                GameObject* pSource = manager.createGameObject( hashed_string( string( entityName ).c_str() ) );
                if ( pSource == nullptr )
                    return false;
                if ( ObjectStateSerializer::loadFromXmlString( pSource, embeddedXml ) == false )
                    return false;

                vector<uint8> stateBytes;
                if ( ObjectStateSerializer::saveToBinaryBuffer( pSource, stateBytes ) == false || stateBytes.empty() )
                    return false;

                // **구운 것을 그 자리에서 되읽어 본다.** 모르는 컴포넌트 타입이 섞이면 여기서 구성이
                // 어긋나고, 그 엔티티는 XML 로 남는다 — 쿠킹이 조용히 컴포넌트를 떨어뜨리지 않는다.
                GameObject* pVerify = manager.createGameObject( hashed_string( "SceneCooker.Verify" ) );
                if ( pVerify == nullptr )
                    return false;

                string parentName;
                if ( ObjectStateSerializer::loadFromBinaryBuffer( pVerify, stateBytes.data(), stateBytes.size(), parentName ) == 0 )
                    return false;

                if ( makeComponentFingerprint( pSource ) != makeComponentFingerprint( pVerify ) )
                    return false;

                outState = std::move( stateBytes );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SceneCooker" );

    uint32 SceneCooker::cookEntityState( SceneDocument& inoutDoc )
    {
        uint32 cookedCount{ 0 };

        // 쿠킹 전용 매니저다 — 씬 매니저의 것을 쓰면 굽는 동안 만든 임시 오브젝트가 실제 씬에 남는다.
        GameObjectManager manager;

        for ( SceneDocument::EntityNode& entity : inoutDoc._listEntityNode )
        {
            if ( entity._embeddedXml.empty() )
                continue;

            vector<uint8> stateBytes;
            if ( SceneCookerInternal::cookOneEntityState( manager, entity._name, entity._embeddedXml, stateBytes ) == false )
            {
                SW_LOG_WARNING( "Entity '%#' could not be cooked to binary state - keeping XML.", entity._name );
                continue;
            }

            entity._embeddedStateBytes = std::move( stateBytes );
            // 둘 다 실으면 파일만 커진다. 바이너리가 정본이 된 순간 XML 은 뺀다.
            entity._embeddedXml.clear();
            ++cookedCount;
        }

        manager.clear();
        return cookedCount;
    }

    uint32 SceneCooker::cookAllScenes( string_view cookedDir )
    {
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR( "Scene cook needs an output directory (--cooked-dir)." );
            return 0;
        }

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        if ( resourceRoot.empty() )
        {
            SW_LOG_ERROR( "Scene cook could not resolve the resource root." );
            return 0;
        }

        vector<string> listSceneFile;
        FileUtil::collectFiles( resourceRoot, ".xml", listSceneFile, true );

        const string normalizedRoot = FileUtil::normalizeSeparators( resourceRoot );
        const string normalizedOut  = FileUtil::normalizeSeparators( cookedDir );

        uint32 writtenCount{ 0 };
        for ( const string& scenePath : listSceneFile )
        {
            if ( scenePath.find( ".scene.xml" ) == string::npos )
                continue;

            SceneDocument doc{};
            if ( doc.loadXml( scenePath ) == false )
            {
                SW_LOG_ERROR( "Scene cook failed to read '%#'.", scenePath );
                continue;
            }

            // 굽기 전에 "상태가 있는 엔티티" 수를 세 둔다 — 굽고 나면 XML 이 비워져 셀 수 없다.
            uint32 statefulCount{ 0 };
            for ( const SceneDocument::EntityNode& entity : doc._listEntityNode )
            {
                if ( entity._embeddedXml.empty() == false )
                    ++statefulCount;
            }

            const uint32 cookedCount = cookEntityState( doc );
            // 이 비교는 **모든 빌드에서** 돌아야 한다. 아래 요약은 `SW_LOG_INFO` 라 Shipping 에서
            // 통째로 사라지는데, "구웠다고 했지만 실은 XML 그대로" 는 그때도 알아야 할 일이다.
            if ( cookedCount < statefulCount )
            {
                SW_LOG_WARNING( "Scene '%#': %# of %# entities could not be cooked to binary state - they keep XML.",
                                scenePath, statefulCount - cookedCount, statefulCount );
            }

            // 출력은 `<cookedDir>/<Resource 기준 상대 경로>` 에 같은 이름으로, 확장자만 .bin 이다.
            const string normalizedScene = FileUtil::normalizeSeparators( scenePath );
            string       relativePath    = normalizedScene;
            if ( normalizedScene.size() > normalizedRoot.size() && normalizedScene.compare( 0, normalizedRoot.size(), normalizedRoot ) == 0 )
            {
                relativePath = normalizedScene.substr( normalizedRoot.size() );
                while ( relativePath.empty() == false && relativePath.front() == '/' )
                    relativePath.erase( relativePath.begin() );
            }

            string outputPath = normalizedOut;
            if ( outputPath.empty() == false && outputPath.back() != '/' )
                outputPath += '/';
            outputPath += relativePath;
            outputPath.replace( outputPath.size() - 4, 4, ".bin" );

            if ( doc.saveBinary( outputPath ) == false )
            {
                SW_LOG_ERROR( "Scene cook failed to write '%#'.", outputPath );
                continue;
            }

            SW_LOG_INFO( "Cooked '%#' -> '%#' (%# entities, %# as binary state)", scenePath, outputPath,
                         static_cast<uint32>( doc._listEntityNode.size() ), cookedCount );
            ++writtenCount;
        }

        return writtenCount;
    }

} // namespace sw
