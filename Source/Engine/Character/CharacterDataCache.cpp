#include "pch.h"

#include "Engine/Character/CharacterDataCache.h"

#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"

namespace sw
{
    namespace
    {
        struct CharacterDataCacheInternal
        {
            [[nodiscard]] static bool loadDefaultKinds( SocketKindTable& outKinds )
            {
                if ( outKinds.loadDefault() )
                    return true;
                SW_LOG_ERROR_ID( "CharacterDataCache.DefaultSocketKindsCould", "Default socket kinds could not be loaded - no socket asset is handed out" );
                return false;
            }

            /** @brief 엔진 기본 소켓 종류 표입니다. 읽지 못했으면 nullptr 입니다(오류는 한 번) — 종류 검사를 건너뛴 반쪽 소켓을 내주지 않습니다. */
            static const SocketKindTable* findDefaultKinds()
            {
                static SocketKindTable s_kinds;
                static const bool      s_bLoaded = loadDefaultKinds( s_kinds );
                return s_bLoaded ? &s_kinds : nullptr;
            }

            [[nodiscard]] static bool loadSocketSet( const AssetID& assetID, SocketSet& outSocketSet )
            {
                const SocketKindTable* pKinds = findDefaultKinds();
                return pKinds != nullptr && outSocketSet.loadFromAsset( assetID, *pKinds );
            }
            [[nodiscard]] static bool loadNotifyTable( const AssetID& assetID, AnimationNotifyTable& outTable ) { return outTable.loadFromAsset( assetID, sw::engine::getSubsystem<AnimationNotifyHandlerRegistry>() ); }
            /** @brief 엔진 기본 연산(`FitSolver` 가 등록하는 넷)으로 연산 이름 · 인자를 대조합니다. 사용자 연산을 쓰는 표는 자기 풀이기로 직접 읽습니다. */
            [[nodiscard]] static bool loadFitTables( const AssetID& assetID, FitTables& outTables )
            {
                const FitSolver solver;
                return outTables.loadFromAsset( assetID, solver.getOperatorRegistry() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( SocketSet, SocketSet, SocketSet::kAssetType.withLoader( AssetTypeLoader::with<SocketSet, &CharacterDataCacheInternal::loadSocketSet>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( BodyShapeSet, BodyShapeSet,
                           AssetTypeDef::makeDataFile( "bodyshape", AssetPathRule{ {}, {}, ".bodyshape.xml" }, AssetTypeLoader::of<BodyShapeSet>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( AnimationNotifyTable, AnimationNotifyTable,
                           AssetTypeDef::makeCacheOnly( "AnimationNotifyTable", AssetTypeLoader::with<AnimationNotifyTable, &CharacterDataCacheInternal::loadNotifyTable>( MemoryTag::Animation ) ) );
    /** 돌고 있는 세션이 옛 에셋의 역할 인덱스를 읽으므로 제자리에서 바꾸지 않고 떼어 낸다. */
    SW_SHARED_ASSET_CACHE( PairedAnimationDef, PairedAnimationDef,
                           PairedAnimationDef::kAssetType.withLoader( AssetTypeLoader::of<PairedAnimationDef>( MemoryTag::Animation, SharedAssetReloadMode::Detach ) ) );
    SW_SHARED_ASSET_CACHE( PhysicsAsset, PhysicsAsset, PhysicsAsset::kAssetType.withLoader( AssetTypeLoader::of<PhysicsAsset>( MemoryTag::Physics ) ) );
    SW_SHARED_ASSET_CACHE( FitTables, FitTables, AssetTypeDef::makeCacheOnly( "FitTables", AssetTypeLoader::with<FitTables, &CharacterDataCacheInternal::loadFitTables>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( FitPartDef, FitPartDef, AssetTypeDef::makeCacheOnly( "FitPartDef", AssetTypeLoader::of<FitPartDef>( MemoryTag::Animation ) ) );
} // namespace sw
