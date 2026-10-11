/**
 * @file AnimationAssetCache.h
 * @brief 스켈레톤(`.skeleton.json`), 애니메이션 클립(`.animclip`), 본 LOD(`.bonelod.json`), 후처리 리그(`.rig.json`)를 경로별로 나눠 주는 테이블과,
 *        그 테이블을 에셋 캐시 레지스트리에 노출하는 함수들입니다.
 * @details `SpriteClipCache` 와 `MeshCache` 와 같은 구조다. 테이블은 프로세스에 하나(Engine.dll 안)이고 약한 참조라 마지막 사용자가 놓으면 사라진다.
 *          Animation(티어 2)이 아니라 Resource(티어 4)에 있는 것은 `IAssetCache` 를 구현하기 때문이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    class AnimClip;
    class RigAsset;
    class Skeleton;
    class SkeletonBoneLOD;

    /**
     * @class SkeletonCache
     * @brief 스켈레톤을 경로별로 나눠 줍니다. 같은 경로는 같은 객체라서 클립에서 본으로 가는 테이블을 스켈레톤 포인터로 캐시할 수 있습니다.
     */
    class SW_API SkeletonCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 스켈레톤을 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다(잠급니다). */
        static shared_ptr<const Skeleton> acquire( string_view path );
        /** @brief 사용 중이면 제자리에서 다시 읽습니다(틱 밖, 게임 스레드). 읽지 못하면 이전 내용 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] static bool reloadShared( string_view path );

        const utf8* getAssetKindName() const override { return "Skeleton"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimClipCache
     * @brief 애니메이션 클립을 경로로 나눠 줍니다. 같은 클립을 쓰는 캐릭터 백 명이 파일을 백 번 읽지 않습니다.
     */
    class SW_API AnimClipCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 클립을 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다(잠급니다). */
        static shared_ptr<const AnimClip> acquire( string_view path );
        /** @brief 사용 중이면 제자리에서 다시 읽습니다(틱 밖, 게임 스레드). 읽지 못하면 이전 내용 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] static bool reloadShared( string_view path );

        const utf8* getAssetKindName() const override { return "AnimClip"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletonBoneLODCache
     * @brief 스켈레톤 옆의 본 LOD 테이블(`.bonelod.json`)을 경로별로 나눠 줍니다. 핫 리로드는 제자리에서 다시 읽고, 유닛은 `SkeletonBoneLOD::getRevision` 으로 변경을 알아챕니다.
     */
    class SW_API SkeletonBoneLODCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 본 LOD 를 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. */
        static shared_ptr<const SkeletonBoneLOD> acquire( string_view path );
        /** @brief 사용 중이면 제자리에서 다시 읽습니다. 읽지 못하면 이전 내용 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] static bool reloadShared( string_view path );

        const utf8* getAssetKindName() const override { return "SkeletonBoneLOD"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigAssetCache
     * @brief 후처리 리그(`.rig.json`)를 경로별로 나눠 줍니다. 다시 읽으면 제자리에서 바꾸고 내용 번호(`RigAsset::getContentID`)가 바뀌므로, 쓰는
     *        `PoseModifierComponent` 가 다음 프레임에 다시 바인딩합니다.
     */
    class SW_API RigAssetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 리그를 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. */
        static shared_ptr<const RigAsset> acquire( string_view path );
        /** @brief 사용 중이면 제자리에서 다시 읽습니다(틱 밖, 게임 스레드). 읽지 못하면 이전 내용 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] static bool reloadShared( string_view path );

        const utf8* getAssetKindName() const override { return "Rig"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw
