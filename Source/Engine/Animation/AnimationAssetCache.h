/**
 * @file AnimationAssetCache.h
 * @brief 스켈레톤(`.skeleton.json`) · 애니메이션 클립(`.animclip`) · 본 LOD(`.bonelod.json`) · 후처리 리그(`.rig.json`)를 경로로 나눠 주는 표와,
 *        그 표를 에셋 캐시 등록부에 보이는 창구들입니다.
 * @details `SpriteClipCache` · `MeshCache` 와 같은 모양입니다 — 표는 프로세스에 하나(Engine.dll 안)이고 약한 참조라 마지막 사용자가 놓으면 사라집니다.
 *          Animation(티어 2)이 아니라 Resource(티어 4)에 있는 것은 `IAssetCache` 를 구현하기 때문입니다.
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
     * @brief 스켈레톤을 경로로 나눠 줍니다. 같은 경로는 같은 객체라 클립 → 본 표를 스켈레톤 포인터로 캐시할 수 있습니다.
     */
    class SW_API SkeletonCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 스켈레톤을 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다(잠급니다). */
        static shared_ptr<const Skeleton> acquire( string_view path );
        /** @brief 사용 중이면 제자리로 다시 읽습니다(틱 밖, 게임 스레드). 읽지 못하면 옛 내용 그대로이고 false 입니다. */
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
        /** @brief 사용 중이면 제자리로 다시 읽습니다(틱 밖, 게임 스레드). 읽지 못하면 옛 내용 그대로이고 false 입니다. */
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
     * @brief 스켈레톤 곁 본 LOD 표(`.bonelod.json`)를 경로로 나눠 줍니다. 핫 리로드는 제자리로 다시 읽고, 유닛은 `SkeletonBoneLOD::getRevision` 으로 알아챕니다.
     */
    class SW_API SkeletonBoneLODCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 본 LOD 를 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. */
        static shared_ptr<const SkeletonBoneLOD> acquire( string_view path );
        /** @brief 사용 중이면 제자리로 다시 읽습니다. 읽지 못하면 옛 내용 그대로이고 false 입니다. */
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
     * @brief 후처리 리그(`.rig.json`)를 경로로 나눠 줍니다. 다시 읽으면 제자리로 바꾸고 내용 번호(`RigAsset::getContentId`)가 바뀌어, 쓰는
     *        `PoseModifierComponent` 가 다음 프레임에 다시 묶습니다.
     */
    class SW_API RigAssetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 리그를 나눠 받습니다. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다. */
        static shared_ptr<const RigAsset> acquire( string_view path );
        /** @brief 사용 중이면 제자리로 다시 읽습니다(틱 밖, 게임 스레드). 읽지 못하면 옛 내용 그대로이고 false 입니다. */
        [[nodiscard]] static bool reloadShared( string_view path );

        const utf8* getAssetKindName() const override { return "Rig"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw
