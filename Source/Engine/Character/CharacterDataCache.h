/**
 * @file CharacterDataCache.h
 * @brief 캐릭터 데이터 에셋을 경로로 나눠 주는 표 셋 — 소켓 에셋(`*.sockets.xml`) · 알림 표(`*.notifies.xml`) · 물리 에셋(`*.physics.xml`)입니다.
 * @details 모양은 스켈레톤 · 클립 캐시와 같습니다(`SharedAssetTable` — 약한 참조, 같은 경로는 같은 객체). 에셋 매니저의 캐시 등록부에 올라 있어
 *          파일을 고치면 제자리로 다시 읽고(`reload`), 다시 읽은 횟수(`getReloadCount`)가 올라 쓰는 쪽(소켓 컴포넌트 · 알림 디스패치 · 래그돌)이
 *          자기가 지은 것을 다시 짓습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    struct PhysicsAsset;

    class AnimNotifyTable;
    class SocketKindTable;
    class SocketSet;

    /** @class SocketSetCache @brief 소켓 에셋을 엔진 기본 소켓 종류 표로 읽어 나눠 줍니다. */
    class SW_API SocketSetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 소켓 에셋입니다. 읽을 수 없으면(모르는 칸 · 종류) 오류를 남기고 nullptr 입니다. */
        static shared_ptr<const SocketSet> acquire( string_view path );
        /** @brief 이 표에서 다시 읽은 횟수입니다. */
        static uint32 getReloadCount();
        /** @brief 엔진 기본 소켓 종류 표(`engine/character/default.socketkinds.xml`)입니다. 처음 부를 때 한 번 읽습니다. */
        static const SocketKindTable& getDefaultKinds();

        const utf8* getAssetKindName() const override { return "SocketSet"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw

namespace sw
{
    /** @class AnimNotifyTableCache @brief 알림 표를 기본 처리기 등록부로 읽어 나눠 줍니다. */
    class SW_API AnimNotifyTableCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 알림 표입니다. 읽을 수 없으면(모르는 처리기 · 인자) 오류를 남기고 nullptr 입니다. */
        static shared_ptr<const AnimNotifyTable> acquire( string_view path );
        /** @brief 이 표에서 다시 읽은 횟수입니다. */
        static uint32 getReloadCount();

        const utf8* getAssetKindName() const override { return "AnimNotifyTable"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw

namespace sw
{
    /** @class PhysicsAssetCache @brief 물리 에셋을 나눠 줍니다. */
    class SW_API PhysicsAssetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 물리 에셋입니다. 읽을 수 없으면 오류를 남기고 nullptr 입니다. */
        static shared_ptr<const PhysicsAsset> acquire( string_view path );
        /** @brief 이 표에서 다시 읽은 횟수입니다. */
        static uint32 getReloadCount();

        const utf8* getAssetKindName() const override { return "PhysicsAsset"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw
