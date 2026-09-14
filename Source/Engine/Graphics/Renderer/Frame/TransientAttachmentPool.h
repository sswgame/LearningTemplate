/**
 * @file TransientAttachmentPool.h
 * @brief 이름으로 찾는 프레임 첨부(트랜지언트 렌더타깃) 풀 — 파이프라인 XML 이 선언한 첨부를 창 크기로 들고 있는다.
 * @details 트랜지언트는 **구성이 바뀔 때만** 다시 만들어진다(창 크기 · 파이프라인). 프레임 안에서는 이름 → (텍스처, SRV)
 *          조회와 "이 첨부를 이번 프레임에 처음 여는가" 판정만 일어나고, 후자는 같은 웨이브의 패스들이 동시에 부른다.
 *          언리얼 RDG 는 생명주기가 겹치지 않는 트랜지언트끼리 메모리를 공유하지만 여기는 각 첨부를 프레임 내내 든다 —
 *          정확성 문제는 아니고 메모리 차이다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class TransientAttachmentPool
     * @brief 첨부 텍스처와 그 bindless SRV 의 소유자. 한 크기로 만들고, 이름으로 찾고, 한 번에 놓는다.
     */
    class SW_API TransientAttachmentPool
    {
    public:
        /**
         * @brief 첨부 하나 — 텍스처와 그 bindless SRV.
         * @details 예전엔 이름이 같은 두 맵(`_mapTransient` / `_mapTransientSrv`)에 나뉘어 있었다.
         *          이름 하나로 둘 다 필요한 자리가 패스마다 여러 번 도는데 그때마다 같은 문자열을
         *          두 번 해시했고, 한쪽에만 넣고 다른 쪽을 빠뜨리면 조용히 어긋났다.
         */
        struct Attachment
        {
            RHITextureHandle   _texture{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
        };

        TransientAttachmentPool();
        ~TransientAttachmentPool() = default;

        TransientAttachmentPool( const TransientAttachmentPool& )            = delete;
        TransientAttachmentPool& operator=( const TransientAttachmentPool& ) = delete;

        /** @brief 앞으로 만들 첨부의 크기. 이미 든 첨부는 바꾸지 않는다 — 크기를 바꾸려면 먼저 놓는다. */
        void   setSize( uint32 width, uint32 height );
        uint32 getWidth() const { return _width; }
        uint32 getHeight() const { return _height; }
        /** @brief 첨부가 하나도 없는가. */
        bool isEmpty() const { return _mapAttachment.empty(); }

        /** @brief 이름으로 첨부를 만듭니다 (현재 크기). 이미 있으면 그대로 true. 못 만들면 경고를 남기고 false. */
        bool allocate( IRHIDevice* pDevice, string_view name, RHIFormat format, bool bDepth, const float4& clearColor );
        /** @brief 이름의 첨부 (텍스처+SRV). 없으면 빈 값. */
        Attachment find( string_view name ) const;
        /** @brief 이름의 텍스처 핸들. 없으면 0. */
        RHITextureHandle findTexture( string_view name ) const;
        /** @brief 그 이름의 첨부가 있는가. */
        bool contains( string_view name ) const { return _mapAttachment.find( name ) != _mapAttachment.end(); }
        /** @brief 전체 — 레지스트리 공개·Present 소스 선택처럼 목록을 훑는 자리용. */
        const unordered_map<string, Attachment>& getAll() const { return _mapAttachment; }

        /** @brief 전부 놓고 비웁니다 — 디바이스가 **살아 있을 때**. 텍스처 SRV 는 텍스처용 해제로 돌려준다(버퍼용과 인덱스 공간이 다르다). */
        void release( IRHIDevice* pDevice );
        /** @brief 디바이스가 이미 사라졌다 — 핸들만 잊습니다. */
        void forget();

        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 돌려줍니다.
         * @details 반환값이 곧 "Clear 로 열어도 되는가" 다. 같은 웨이브의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계구역이어야 한다 — 나눠 놓으면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지운다.
         */
        bool markCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다 (프레임 시작). */
        void resetCleared();

    private:
        unordered_map<string, Attachment> _mapAttachment;
        /// @brief 이번 프레임에 이미 클리어한 첨부들. 병렬 패스가 동시에 갱신하므로 _clearedMutex 로 보호한다.
        vector<hashed_string> _listClearedThisFrame;
        mutable mutex         _clearedMutex;
        uint32                _width;
        uint32                _height;
    };
} // namespace sw
