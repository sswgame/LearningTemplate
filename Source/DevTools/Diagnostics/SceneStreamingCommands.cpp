#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "DevTools/Diagnostics/DevSceneStreamingAccess.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct SceneStreamingCommandsInternal
        {
            /**
             * @brief `world.streamqueue`: 로드 큐를 보입니다. 올리려는 셀 조각(줄 차례: 우선순위 · 거리 · 단계)과 HLOD 대리 수, 텍스처 밉 줄(우선순위 · 밉)과 읽는 중 · 업로드 중 수.
             * @details 셀과 텍스처가 같은 우선순위(`StreamingSourcePriority`)로 줄을 선다 — 둘을 한 화면에 놓아 차례가 어긋났는지 본다. 줄마다 앞 @p kMaxListedEntries 개만 적는다.
             */
            static bool runStreamingQueue( const vector<string>& listArgument, string& outReply )
            {
                constexpr uint32 kMaxListedEntries = 16;
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                const SceneStreamingSubsystem&                  streaming = pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
                vector<DevSceneStreamingAccess::LoadQueueEntry> listCell;
                DevSceneStreamingAccess::collectLoadQueue( streaming, listCell );
                StringBuilder<constant::buffer::kMax256> lineBuilder;
                lineBuilder.appendFormat( "cells: %# pending of %# (%# loaded), HLOD %# shown of %#", static_cast<uint32>( listCell.size() ), streaming.getCellCount(),
                                          streaming.getLoadedCellCount(), DevSceneStreamingAccess::getShownHLODCount( streaming ), DevSceneStreamingAccess::getHLODCount( streaming ) );
                outReply = string( lineBuilder.view() );
                for ( size_t index = 0; index < listCell.size() && index < kMaxListedEntries; ++index )
                {
                    const DevSceneStreamingAccess::LoadQueueEntry& entry = listCell[index];
                    string                                         layers;
                    for ( const string& layer : entry._listDataLayer )
                    {
                        layers.append( layers.empty() ? " [" : "," ).append( layer );
                    }
                    layers.append( layers.empty() ? "" : "]" );
                    lineBuilder.clear();
                    lineBuilder.appendFormat( "\n  (%#, %#)%# %# %# m %#", entry._cell._x, entry._cell._z, layers, getEnumName( entry._priority ), entry._distance, getEnumName( entry._state ) );
                    outReply.append( lineBuilder.view() );
                }
                AssetManager* pAssets = pScene->getObjectManager()->findAssetManager();
                if ( pAssets == nullptr )
                    return true; // 에셋 서비스가 없다(도구) — 텍스처 줄이 없다
                const TextureStreamingQueueSnapshot textures = pAssets->getAssetCache<TextureCache>().captureStreamingQueue();
                lineBuilder.clear();
                lineBuilder.appendFormat( "\ntextures: %# waiting, %# reading, %# uploading", static_cast<uint32>( textures._listWaiting.size() ), textures._readingCount,
                                          textures._uploadingCount );
                outReply.append( lineBuilder.view() );
                for ( size_t index = 0; index < textures._listWaiting.size() && index < kMaxListedEntries; ++index )
                {
                    const TextureMipRequest& request = textures._listWaiting[index];
                    lineBuilder.clear();
                    lineBuilder.appendFormat( "\n  %# %# mips %#+%#", request._key, getEnumName( request._priority ), request._firstMip, request._mipCount );
                    outReply.append( lineBuilder.view() );
                }
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( StreamingQueue, "world.streamqueue", "world.streamqueue",
                    "Show the streaming load queue: pending cells (priority, distance, stage), HLOD proxies shown, and texture mip requests (priority) with reads in flight",
                    &SceneStreamingCommandsInternal::runStreamingQueue );
} // namespace sw
