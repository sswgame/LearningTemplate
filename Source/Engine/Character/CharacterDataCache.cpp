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
#include "Engine/Physics/Asset/PhysicsAsset.h"

namespace sw
{
    SW_LOG_CALLER( "CharacterDataCache" );

    namespace
    {
        struct CharacterDataCacheInternal
        {
            [[nodiscard]] static bool loadDefaultKinds( SocketKindTable& outKinds )
            {
                if ( outKinds.loadDefault() )
                    return true;
                SW_LOG_ERROR( "Default socket kinds could not be loaded - no socket asset is handed out" );
                return false;
            }

            /** @brief 엔진 기본 소켓 종류 표입니다. 읽지 못했으면 nullptr 입니다(오류는 한 번) — 종류 검사를 건너뛴 반쪽 소켓을 내주지 않습니다. */
            static const SocketKindTable* findDefaultKinds()
            {
                static SocketKindTable s_kinds;
                static const bool      s_bLoaded = loadDefaultKinds( s_kinds );
                return s_bLoaded ? &s_kinds : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<SocketSet>
    {
        static constexpr const utf8*           kKindName   = "SocketSet";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, SocketSet& outSocketSet )
        {
            const SocketKindTable* pKinds = CharacterDataCacheInternal::findDefaultKinds();
            return pKinds != nullptr && outSocketSet.loadFromAsset( assetID, *pKinds );
        }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<AnimationNotifyTable>
    {
        static constexpr const utf8*           kKindName   = "AnimationNotifyTable";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, AnimationNotifyTable& outTable ) { return outTable.loadFromAsset( assetID, AnimationNotifyHandlerRegistry::getDefault() ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<PairedAnimationDef>
    {
        static constexpr const utf8* kKindName  = "PairedAnimationDef";
        static constexpr MemoryTag   kMemoryTag = MemoryTag::Animation;
        /** @brief 돌고 있는 세션이 옛 에셋의 역할 인덱스를 읽으므로 제자리에서 바꾸지 않는다. */
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::Detach;

        [[nodiscard]] static bool load( const AssetID& assetID, PairedAnimationDef& outDef ) { return outDef.loadFromAsset( assetID ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<PhysicsAsset>
    {
        static constexpr const utf8*           kKindName   = "PhysicsAsset";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Physics;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, PhysicsAsset& outAsset ) { return outAsset.loadFromAsset( assetID ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<FitTables>
    {
        static constexpr const utf8*           kKindName   = "FitTables";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        /** @brief 엔진 기본 연산(`FitSolver` 가 등록하는 넷)으로 연산 이름 · 인자를 대조합니다. 사용자 연산을 쓰는 표는 자기 풀이기로 직접 읽습니다. */
        [[nodiscard]] static bool load( const AssetID& assetID, FitTables& outTables )
        {
            const FitSolver solver;
            return outTables.loadFromAsset( assetID, solver.getOperatorRegistry() );
        }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<FitPartDef>
    {
        static constexpr const utf8*           kKindName   = "FitPartDef";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, FitPartDef& outData ) { return outData.loadFromAsset( assetID ); }
    };
} // namespace sw

namespace sw
{
    template <>
    struct SharedAssetCacheTraits<BodyShapeSet>
    {
        static constexpr const utf8*           kKindName   = "BodyShapeSet";
        static constexpr MemoryTag             kMemoryTag  = MemoryTag::Animation;
        static constexpr SharedAssetReloadMode kReloadMode = SharedAssetReloadMode::InPlace;

        [[nodiscard]] static bool load( const AssetID& assetID, BodyShapeSet& outShapes ) { return outShapes.loadFromAsset( assetID ); }
    };
} // namespace sw

namespace sw
{
    SW_SHARED_ASSET_CACHE( SocketSet );
    SW_SHARED_ASSET_CACHE( BodyShapeSet );
    SW_SHARED_ASSET_CACHE( AnimationNotifyTable );
    SW_SHARED_ASSET_CACHE( PairedAnimationDef );
    SW_SHARED_ASSET_CACHE( PhysicsAsset );
    SW_SHARED_ASSET_CACHE( FitTables );
    SW_SHARED_ASSET_CACHE( FitPartDef );
} // namespace sw
