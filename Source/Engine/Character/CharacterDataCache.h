/**
 * @file CharacterDataCache.h
 * @brief 캐릭터 데이터 에셋을 경로별로 나눠 주는 테이블 셋입니다. 소켓 에셋(`*.sockets.xml`), 알림 테이블(`*.notifies.xml`), 페어드 애니메이션(`*.pairedanim.xml`),
 *        물리 에셋(`*.physics.xml`)을 다룹니다.
 * @details 구조는 스켈레톤과 클립 캐시와 같다(`SharedAssetTable`은 약한 참조이고 같은 경로는 같은 객체다). 에셋 매니저의 캐시 레지스트리에 등록되어 있어
 *          파일을 고치면 제자리에서 다시 읽는다(`reload`). 다시 읽은 횟수(`getReloadCount`)가 오르면 쓰는 쪽(소켓 컴포넌트, 알림 디스패치, 래그돌)이
 *          자기가 만든 것을 다시 만든다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    struct PhysicsAsset;

    class AnimationNotifyTable;
    class PairedAnimationDef;
    class SocketKindTable;
    class SocketSet;

    /** @class SocketSetCache @brief 소켓 에셋을 엔진 기본 소켓 종류 테이블로 읽어 나눠 줍니다. */
    class SW_API SocketSetCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 소켓 에셋을 반환합니다. 읽을 수 없으면(모르는 필드나 종류) 오류를 남기고 nullptr 를 반환합니다. */
        static shared_ptr<const SocketSet> acquire( string_view path );
        /** @brief 이 테이블에서 다시 읽은 횟수를 반환합니다. */
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
    /** @class AnimationNotifyTableCache @brief 알림 테이블을 기본 처리기 레지스트리로 읽어 나눠 줍니다. */
    class SW_API AnimationNotifyTableCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 알림 테이블을 반환합니다. 읽을 수 없으면(모르는 처리기나 인자) 오류를 남기고 nullptr 를 반환합니다. */
        static shared_ptr<const AnimationNotifyTable> acquire( string_view path );
        /** @brief 이 테이블에서 다시 읽은 횟수를 반환합니다. */
        static uint32 getReloadCount();

        const utf8* getAssetKindName() const override { return "AnimationNotifyTable"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PairedAnimationDefCache
     * @brief 페어드 애니메이션 에셋(`*.pairedanim.xml`)을 나눠 줍니다.
     * @details 다시 읽기는 제자리에서 바꾸지 않고 표에서 떼어 낸다(`detachShared`) — 돌고 있는 세션은 역할 인덱스로 옛 에셋을 읽으므로 끝까지 옛 객체를 쥔다.
     *          쓰는 쪽(`PairedAnimationComponent`)은 다시 읽은 횟수가 바뀌면 다음 시작 전에 다시 `acquire` 한다.
     */
    class SW_API PairedAnimationDefCache final : public IAssetCache
    {
    public:
        /** @brief 경로의 에셋을 반환합니다. 읽을 수 없으면 오류(줄마다)를 남기고 nullptr 를 반환합니다. */
        static shared_ptr<const PairedAnimationDef> acquire( string_view path );
        /** @brief 이 테이블에서 다시 읽은 횟수를 반환합니다. */
        static uint32 getReloadCount();

        const utf8* getAssetKindName() const override { return "PairedAnimationDef"; }
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
        /** @brief 이 테이블에서 다시 읽은 횟수를 반환합니다. */
        static uint32 getReloadCount();

        const utf8* getAssetKindName() const override { return "PhysicsAsset"; }
        bool        isCached( string_view relativePath ) const override;
        void        reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t      getCachedCount() const override;
        void        clear() override;
    };
} // namespace sw
