/**
 * @file SocketPartPoolWatches.cpp
 * @brief 소켓 부품 풀의 자동화 감시 값(`SocketPartPool.TakeHits` · `TakeMisses` · `Stored`)입니다.
 * @details 시나리오만 읽는 감시 값이라 풀 TU 가 아니라 이 파일에 둔다. 풀의 공개 진단 읽기(`getTakeHitCount` …)만 쓴다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/Socket/SocketPartPool.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct SocketPartPoolWatchesInternal
        {
            /** @brief 첫 씬이 아니라 지금 활성 씬의 풀을 읽는다 — 시나리오 감시 값이 부릅니다. */
            [[nodiscard]] static bool readTakeHitCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SocketPartPool>().getTakeHitCount() );
                return true;
            }
            [[nodiscard]] static bool readTakeMissCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SocketPartPool>().getTakeMissCount() );
                return true;
            }
            [[nodiscard]] static bool readStoredCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SocketPartPool>().getStoredCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( socketPartPoolTakeHits, "SocketPartPool.TakeHits", "Appearance parts the socket part pool handed out from rest (cumulative)",
                         &SocketPartPoolWatchesInternal::readTakeHitCount );
    SW_AUTOMATION_WATCH( socketPartPoolTakeMisses, "SocketPartPool.TakeMisses", "Appearance part requests the socket part pool could not serve (the part was spawned new, cumulative)",
                         &SocketPartPoolWatchesInternal::readTakeMissCount );
    SW_AUTOMATION_WATCH( socketPartPoolStored, "SocketPartPool.Stored", "Appearance parts the socket part pool took in to rest (cumulative)", &SocketPartPoolWatchesInternal::readStoredCount );
} // namespace sw
