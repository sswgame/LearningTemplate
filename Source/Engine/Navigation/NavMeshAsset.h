/**
 * @file NavMeshAsset.h
 * @brief 쿠킹한 내비메시(`.navmesh`)입니다. 씬 하나의 에이전트 종류마다 타일 바이트를 싣습니다. 타일을 한꺼번에 베이크하는 도우미도 있습니다.
 * @details **형식**(리틀 엔디언)은 다음과 같다. 매직 `SWNV`, 형식 버전, 백엔드 이름, 백엔드 버전, 항목 수가 오고, 항목마다 [종류 이름, 베이크 값 해시, 입력 해시,
 *          경계 상자, 타일 수 → 타일마다 (x, z, 바이트)]가 온다. 타일 바이트는 백엔드의 것이고 그대로 `INavMesh::replaceTile` 에 넣는다.
 *          백엔드 이름이나 버전이 다르면 읽지 않는다(다른 라이브러리로 바꾼 뒤 낡은 쿠킹본을 넣지 않는다). 읽기는 현재 형식만 받는다.
 *
 *          **쿠킹과 런타임 베이크.** 쿠킹(`App --cook-scenes`)은 내비 표면(`NavMeshSurfaceComponent`)이 있는 씬마다 `<씬 이름>.navmesh` 를 쓴다.
 *          배포본은 그것을 팩에서 읽고, Dev 는 쿠킹 폴더를 마운트하지 않으므로 처음 쓸 때 같은 함수(`NavMeshBakeUtil::bakeAllTiles`)로 베이크한다.
 *          두 경로의 타일 바이트는 같다. 읽은 쪽은 입력 해시와 베이크 값 해시가 현재 씬과 테이블에 같을 때만 쓰고, 다르면 경고를 남기고 베이크한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/hashed_string.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Navigation/INavMesh.h"

namespace sw
{
    struct NavConvexVolume;

    class NavMeshGeometry;

    /** @brief 에이전트 종류 하나의 쿠킹한 타일들입니다. */
    struct NavMeshAssetEntry
    {
        hashed_string       _agentType{};
        uint64              _settingsHash{ 0 }; ///< `NavMeshSettings::computeAgentTypeHash`
        uint64              _inputHash{ 0 };    ///< `NavMeshGeometry::computeHash`
        AABB                _bounds{};
        vector<NavTileData> _listTile;
    };
} // namespace sw

namespace sw
{
    /** @class NavMeshAsset @brief 파일 머리말 참고. */
    class SW_API NavMeshAsset
    {
    public:
        /** @brief 쿠킹본의 확장자입니다. */
        static constexpr string_view kExtension = ".navmesh";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `NavMeshAsset.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "NavMeshAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        /** @brief 형식 버전입니다. 바꾸면 쿠킹본을 다시 만듭니다. */
        static constexpr uint32 kVersion = 1;

        NavMeshAsset();

        /** @brief 씬 경로(`.scene.xml`, `.scene.bin`, `.scene`)에 해당하는 쿠킹본 경로(`<씬 이름>.navmesh`)를 반환합니다. 씬 경로가 아니면 빈 문자열입니다. */
        static string makeCookedPath( string_view scenePath );

        /** @brief 바이트로 씁니다. */
        void writeBytes( vector<uint8>& outBytes ) const;
        /** @brief 바이트를 읽습니다. 매직, 형식 버전, 백엔드가 다르거나 잘렸으면 오류를 남기고 false 를 반환하며 내용을 비웁니다. */
        [[nodiscard]] bool readBytes( const uint8* pData, size_t size, string_view sourceLabel );
        /** @brief 리소스 경로의 쿠킹본을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 절대 경로에 씁니다(폴더가 없으면 만듭니다). */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;

        /** @brief 종류 이름의 항목입니다. 없으면 nullptr 입니다. */
        const NavMeshAssetEntry* findEntry( const hashed_string& agentType ) const;
        /** @brief 항목을 더하거나(같은 종류면) 바꿉니다. */
        void setEntry( NavMeshAssetEntry entry );

        const vector<NavMeshAssetEntry>& getEntries() const { return _listEntry; }

    private:
        vector<NavMeshAssetEntry> _listEntry;
    };
} // namespace sw

namespace sw
{
    /** @brief 베이크 한 번의 측정값입니다(진단과 측정용). */
    struct NavMeshBakeStats
    {
        float64 _milliseconds{ 0.0 };
        uint32  _tileCount{ 0 };       ///< 베이크한 타일 위치 수다.
        uint32  _filledTileCount{ 0 }; ///< 걸을 곳이 있는 타일 수다.
        uint32  _failedTileCount{ 0 };
        uint32  _polygonCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 내비메시 하나를 통째로 베이크거나 쿠킹본에서 채우는 도우미입니다. */
    struct SW_API NavMeshBakeUtil
    {
        /**
         * @brief 타일 격자의 모든 타일을 워커로 나눠 베이크하고(`engine::runParallel`) 끼웁니다. @p geometry 의 색인은 여기서 짓습니다.
         * @return 실패한 타일이 하나라도 있으면 false 입니다(나머지 타일은 끼운다).
         */
        static bool bakeAllTiles( INavMesh& navMesh, NavMeshGeometry& geometry, const vector<NavConvexVolume>* pExtraVolume = nullptr,
                                  NavMeshBakeStats* pOutStats = nullptr );
        /** @brief 쿠킹본의 항목을 끼웁니다(격자는 먼저 `initialize` 해 둔다). 하나라도 거절되면 false 입니다. */
        static bool installTiles( INavMesh& navMesh, const vector<NavTileData>& listTile );
        /** @brief 베이크 경계입니다. 입력 경계를 그대로 씁니다(비었으면 원점의 작은 상자입니다). */
        static AABB computeBakeBounds( const NavMeshGeometry& geometry );
    };
} // namespace sw
