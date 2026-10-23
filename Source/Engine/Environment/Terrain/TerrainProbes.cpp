/**
 * @file TerrainProbes.cpp
 * @brief 지형의 자동화 탐침입니다. 탐침 레지스트리(`Engine/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다.
 */
#include "pch.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Probe/AutomationProbe.h"

namespace sw
{
    namespace
    {
        /** @brief 탐침 `Terrain.WrittenEditCount` — 디스크에서 다시 읽은 뒤 높이나 스플랫을 쓴 지형 수입니다(저장하지 않은 편집이 교체 · 리로드를 넘었는지 본다). */
        [[nodiscard]] bool readTerrainWrittenEditCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            uint32 count{ 0 };
            for ( const TerrainComponent* pTerrain : pManager->getComponentRegistry().getAll<TerrainComponent>() )
            {
                if ( pTerrain->hasWrittenEdits() )
                    ++count;
            }
            outValue = static_cast<float64>( count );
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( terrainWrittenEditCount, "Terrain.WrittenEditCount", "Terrains whose heights or splat were written since they were last read from disk",
                         &readTerrainWrittenEditCount );
} // namespace sw
