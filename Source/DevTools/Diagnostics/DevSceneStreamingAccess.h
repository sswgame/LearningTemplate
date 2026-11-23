/**
 * @file DevSceneStreamingAccess.h
 * @brief 개발 도구가 씬 스트리밍의 로드 큐 · HLOD 대리 상태를 읽는 입구입니다(감시 값 · 개발 명령 `world.streamqueue`).
 * @details 런타임(`SceneStreamingSubsystem`)은 이 값을 공개 API 로 내지 않는다 — 갱신이 쓰는 줄을 그대로 읽으므로 따로 재지 않고, 배포 구성에는 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

namespace sw
{
    /** @struct DevSceneStreamingAccess @brief 파일 머리말 참고. */
    struct DevSceneStreamingAccess
    {
        /** @brief 로드 큐의 한 줄입니다(`collectLoadQueue`). */
        struct LoadQueueEntry
        {
            StreamingCellCoord      _cell{};
            vector<string>          _listDataLayer;
            StreamingSourcePriority _priority{ StreamingSourcePriority::Lowest };
            float32                 _distance{ 0.0f };                      ///< 가장 가까운 원점까지 조각 경계의 거리(m)
            StreamingCellState      _state{ StreamingCellState::Unloaded }; ///< Unloaded · Loading · Ready · Building 가운데 하나
        };

        /**
         * @brief 지난 갱신에서 원점 반경 안이라 올리려는 조각 가운데 아직 오르지 않은 것을 줄 차례(우선순위 → 거리)로 @p outListEntry 에 채웁니다(비우고 채운다).
         * @details 다 지었거나 실패한 조각 · 그 뒤 레이어가 꺼진 조각은 뺀다.
         */
        static void collectLoadQueue( const SceneStreamingSubsystem& streaming, vector<LoadQueueEntry>& outListEntry )
        {
            outListEntry.clear();
            if ( streaming._pScene == nullptr )
                return;
            for ( const SceneStreamingSubsystem::LoadOrderKey& key : streaming._listScratchLoadKey )
            {
                if ( key._slotIndex >= streaming._listCell.size() )
                    continue; // 그 뒤 월드를 바꿨다(복원이 조각을 다시 만들었다)
                const SceneStreamingSubsystem::CellSlot& slot     = streaming.getSlot( key._slotIndex );
                const bool                               bPending = slot._state != StreamingCellState::Loaded && slot._state != StreamingCellState::Failed && streaming.areSlotLayersActive( slot );
                if ( bPending )
                    outListEntry.push_back( LoadQueueEntry{ slot._cell, slot._listDataLayer, key._priority, MathUtil::sqrt( key._distanceSquared ), slot._state } );
            }
        }

        /** @brief `collectLoadQueue` 의 줄 수입니다. */
        static uint32 getPendingCellCount( const SceneStreamingSubsystem& streaming )
        {
            vector<LoadQueueEntry> listEntry;
            collectLoadQueue( streaming, listEntry );
            return static_cast<uint32>( listEntry.size() );
        }

        /** @brief HLOD 대리가 있는 조각 수입니다. */
        static uint32 getHLODCount( const SceneStreamingSubsystem& streaming )
        {
            uint32 count = 0;
            for ( const unique_ptr<SceneStreamingSubsystem::CellSlot>& pSlot : streaming._listCell )
            {
                count += pSlot->_hlodFileID != 0 ? 1u : 0u;
            }
            return count;
        }

        /** @brief 지금 켜진(조각이 올라 있지 않아 대신 그리는) HLOD 대리 수입니다. 레이어가 꺼져 대리가 없는 조각은 세지 않습니다. */
        static uint32 getShownHLODCount( const SceneStreamingSubsystem& streaming )
        {
            uint32 count = 0;
            for ( const unique_ptr<SceneStreamingSubsystem::CellSlot>& pSlot : streaming._listCell )
            {
                const GameObject* pProxy = pSlot->_hlodObjectID != 0 && streaming._pScene != nullptr ? streaming._pScene->getObjectManager()->findGameObjectByID( pSlot->_hlodObjectID ) : nullptr;
                count += pProxy != nullptr && pProxy->isActive() ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace sw
