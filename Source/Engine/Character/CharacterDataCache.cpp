#include "pch.h"

#include "Engine/Character/CharacterDataCache.h"

#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/FitPartDef.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/Socket/SocketSet.h"

namespace sw
{
    namespace
    {
        struct CharacterDataCacheInternal
        {
            /**
             * @brief 소켓 세트 하나를 엔진 기본 소켓 종류 표로 검사하며 읽습니다. 종류 표를 읽지 못하면 내주지 않습니다(종류 검사를 건너뛴 반쪽 소켓을 내주지 않는다).
             * @details 종류 표는 읽을 때마다 짓는다 — 소켓 세트는 공유 캐시가 한 번 읽어 나누므로 드물고, 읽은 표를 함수 정적(감춘 싱글턴)에 두지 않는다.
             */
            [[nodiscard]] static bool loadSocketSet( const AssetID& assetID, SocketSet& outSocketSet )
            {
                SocketKindTable kinds;
                if ( kinds.loadDefault() == false )
                {
                    SW_LOG_ERROR_ONCE_ID( "CharacterDataCache.DefaultSocketKindsCould", "Default socket kinds could not be loaded - no socket asset is handed out" );
                    return false;
                }
                return outSocketSet.loadFromAsset( assetID, kinds );
            }
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
    /** 돌고 있는 세션이 옛 에셋의 역할 인덱스를 읽으므로 제자리에서 바꾸지 않고 떼어 낸다. */
    SW_SHARED_ASSET_CACHE( PairedAnimationDef, PairedAnimationDef,
                           PairedAnimationDef::kAssetType.withLoader( AssetTypeLoader::of<PairedAnimationDef>( MemoryTag::Animation, SharedAssetReloadMode::Detach ) ) );
    SW_SHARED_ASSET_CACHE( FitTables, FitTables, FitTables::kAssetType.withLoader( AssetTypeLoader::with<FitTables, &CharacterDataCacheInternal::loadFitTables>( MemoryTag::Animation ) ) );
    SW_SHARED_ASSET_CACHE( FitPartDef, FitPartDef, FitPartDef::kAssetType.withLoader( AssetTypeLoader::of<FitPartDef>( MemoryTag::Animation ) ) );
} // namespace sw
