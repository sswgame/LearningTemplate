/**
 * @file TransientAttachmentPool.h
 * @brief 이름으로 찾는 프레임 첨부(트랜지언트 렌더 타깃) 풀입니다. 파이프라인 XML 이 선언한 첨부를 창 크기로 보관합니다.
 * @details 트랜지언트는 **구성이 바뀔 때만** 다시 만들어집니다(창 크기, 파이프라인). 프레임 안에서는 이름 → (텍스처, SRV)
 *          조회와 "이 첨부를 이번 프레임에 처음 여는가" 판정만 일어나고, 후자는 같은 레벨의 패스들이 동시에 부릅니다.
 *          언리얼 RDG 는 생명주기가 겹치지 않는 트랜지언트끼리 메모리를 공유하지만 여기는 각 첨부를 프레임 내내 보관합니다.
 *          정확성 문제는 아니고 메모리 차이입니다.
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
     * @brief 첨부 텍스처와 그 bindless SRV 의 소유자입니다. 한 크기로 만들고, 이름으로 찾고, 한 번에 놓습니다.
     */
    class SW_API TransientAttachmentPool
    {
    public:
        /**
         * @brief 첨부 하나입니다(텍스처와 그 bindless SRV).
         * @details 텍스처와 SRV 를 한 맵의 한 원소로 둡니다. 이름 하나로 둘 다 필요한 자리가 패스마다 여러 번 돌고,
         *          맵을 둘로 나누면 해시가 두 번이고 한쪽에만 넣는 실수가 조용히 어긋납니다.
         */
        struct Attachment
        {
            RHITextureHandle   _texture{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
            uint32             _width{ 0 }; ///< 이 첨부의 실제 크기(풀 크기를 나눗수로 나눈 값, 올림)
            uint32             _height{ 0 };
            RHIFormat          _format{ RHIFormat::Unknown }; ///< 만들 때의 포맷(파이프라인 XML 의 선언)
            float4             _clearColor{};                 ///< 클리어 색 · 깊이(`_x`) — 파이프라인 XML 의 선언
            uint32             _idleFrames{ 0 };              ///< 쓰는 패스가 이어서 돌지 않은 프레임 수(`releaseIfIdle`)
        };

        /**
         * @brief 쓰는 패스가 이만큼 이어서 돌지 않은 첨부(꺼진 효과의 타깃)는 놓습니다. 다시 켜지면 셋업이 다시 만듭니다.
         * @details 모션 블러는 카메라가 서고 움직일 때마다 켜지고 꺼진다. 바로 놓으면 그때마다 만들고 놓기를 되풀이하므로 60 Hz 에서 약 2 초를 둔다.
         *          놓은 GPU 자원은 백엔드의 해제 큐가 앞 프레임들이 끝난 뒤에 지운다(`destroyTexture` → `RHIReleaseQueue`, `kGPUReleaseFrameLatency`).
         */
        static constexpr uint32 kIdleFramesBeforeRelease = 120;

        /**
         * @brief 첨부 하나의 쉰 프레임 수를 한 프레임 나아가게 합니다. 놓을 때가 되면 true 입니다(그 프레임에 한 번).
         * @param ioIdleFrames 쉰 프레임 수입니다. 이번 프레임에 쓰였으면 0 으로 돌아간다.
         */
        static constexpr bool advanceIdleFrames( uint32& ioIdleFrames, bool bUsedThisFrame )
        {
            ioIdleFrames = bUsedThisFrame ? 0u : ioIdleFrames + 1u;
            return ioIdleFrames == kIdleFramesBeforeRelease;
        }

        /** @brief 프레임 크기 하나를 나눗수로 줄인 크기입니다(올림, 최소 1). 나눗수 0 은 1 로 봅니다. */
        static constexpr uint32 computeScaledExtent( uint32 extent, uint32 divisor )
        {
            const uint32 safeDivisor = divisor > 0u ? divisor : 1u;
            const uint32 scaled      = ( extent + safeDivisor - 1u ) / safeDivisor;
            return scaled > 0u ? scaled : 1u;
        }

        TransientAttachmentPool();
        ~TransientAttachmentPool() = default;

        TransientAttachmentPool( const TransientAttachmentPool& )            = delete;
        TransientAttachmentPool& operator=( const TransientAttachmentPool& ) = delete;

        /** @brief 앞으로 만들 첨부의 크기입니다. 이미 있는 첨부는 바꾸지 않습니다. 크기를 바꾸려면 먼저 놓습니다. */
        void   setSize( uint32 width, uint32 height );
        uint32 getWidth() const { return _width; }
        uint32 getHeight() const { return _height; }
        /** @brief 첨부가 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _mapAttachment.empty(); }

        /**
         * @brief 이름으로 첨부를 만듭니다. 크기는 현재 크기를 `resolutionDivisor` 로 나눈 값(올림)입니다.
         * @return 이미 있으면 그대로 true 이고, 못 만들면 경고를 남기고 false 입니다.
         */
        bool allocate( IRHIDevice* pDevice, string_view name, RHIFormat format, bool bDepth, const float4& clearColor, uint32 resolutionDivisor );
        /**
         * @brief 프레임 크기와 상관없는 크기(width × height)로 첨부를 만듭니다. 그림자 맵처럼 화면이 아니라 빛의 볼륨을 담는 타깃용입니다.
         * @return 이미 있으면 그대로 true 이고, 못 만들면 경고를 남기고 false 입니다.
         */
        bool allocateSized( IRHIDevice* pDevice, string_view name, RHIFormat format, bool bDepth, const float4& clearColor, uint32 width, uint32 height );
        /** @brief 이름의 첨부(텍스처 + SRV)입니다. 원본을 가리키는 이름(`forward`)은 원본의 첨부입니다. 없으면 빈 값입니다. */
        Attachment find( string_view name ) const;
        /** @brief 이름의 텍스처 핸들입니다. 원본을 가리키는 이름은 원본의 핸들입니다. 없으면 0 입니다. */
        RHITextureHandle findTexture( string_view name ) const;
        /** @brief 이름이 가리키는 원본을 따라가지 않고 그 이름으로 만든 텍스처 핸들입니다(그래프 배리어용 — 원본의 상태는 원본 이름의 배리어가 맡는다). 없으면 0 입니다. */
        RHITextureHandle findOwnTexture( string_view name ) const;
        /** @brief 그 이름으로 만든 첨부가 있는지 반환합니다(원본을 가리키는지와 상관없다). */
        bool contains( string_view name ) const { return _mapAttachment.find( name ) != _mapAttachment.end(); }

        /**
         * @brief 이번 프레임에 @p name 이 @p source 의 첨부를 가리키게 합니다(꺼진 효과의 출력 — `RenderPassTraitFlag::kBypassesToSource`).
         * @details @p source 가 다시 다른 이름을 가리키면 끝까지 따라간다(꺼진 효과가 잇달아 있을 때). 고리는 로드 검증이 막는다(출력은 그 패스만 쓴다).
         * @return 가리키는 곳이 바뀌었으면 true 입니다.
         */
        bool forward( string_view name, string_view source );
        /**
         * @brief 이번 프레임에 @p name 이 아무 첨부도 가리키지 않게 합니다(꺼진 SSAO처럼 원본이 없는 효과, 읽는 패스가 모두 꺼져 기록하지 않는 패스의 출력).
         * @details `find` 는 빈 값이고 `resolveName` 은 빈 이름이다. 읽는 쪽은 그 역할을 바인딩하지 않아 셰이더의 기본값(AO = 1)을 쓴다.
         * @return 가리키는 곳이 바뀌었으면 true 입니다.
         */
        bool markAbsent( string_view name ) { return forward( name, string_view{} ); }
        /** @brief @p name 이 다시 자기 첨부를 쓰게 합니다. @return 바뀌었으면 true 입니다. */
        bool stopForward( string_view name );
        /** @brief @p name 이 지금 원본을 가리키거나 비어 있으면 true 입니다(그 이름을 쓰는 패스는 기록하지 않는다). */
        bool isForwarded( string_view name ) const { return _mapForward.find( name ) != _mapForward.end(); }
        /** @brief 가리키는 곳을 끝까지 따라간 이름입니다. 가리키지 않으면 @p name 그대로, 비어 있으면(`markAbsent`) 빈 이름입니다. */
        string_view resolveName( string_view name ) const;
        /** @brief 전체 첨부입니다. 레지스트리 등록이나 Present 소스 선택처럼 목록을 훑는 곳에서 씁니다. */
        const unordered_map<string, Attachment>& getAll() const { return _mapAttachment; }

        /** @brief 모두 놓고 비웁니다. 디바이스가 **살아 있을 때** 부릅니다. 텍스처 SRV 는 텍스처용 해제로 돌려줍니다(버퍼용과 인덱스 공간이 다릅니다). */
        void release( IRHIDevice* pDevice );
        /**
         * @brief 그 이름으로 만든 첨부의 쉰 프레임 수를 나아가게 하고, `kIdleFramesBeforeRelease` 프레임 이어서 쓰이지 않았으면 놓습니다. 셋업(기록 전)에서 프레임마다 부릅니다.
         * @return 이번에 놓았으면 true 입니다. 첨부가 없으면 false 입니다.
         */
        bool releaseIfIdle( IRHIDevice* pDevice, string_view name, bool bUsedThisFrame );
        /** @brief 이 풀이 지금까지 만든 첨부 수입니다(놓았다 다시 만든 것도 센다 — 진단과 테스트용). */
        uint32 getAllocationCount() const { return _allocationCount; }
        /** @brief 디바이스가 이미 사라졌을 때 부릅니다. 핸들만 잊습니다. */
        void forget();

        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 반환합니다.
         * @details 반환값이 곧 "Clear 로 열어도 되는가" 입니다. 같은 레벨의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계 구역이어야 합니다. 나눠 놓으면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지웁니다.
         */
        bool markCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다(프레임 시작). */
        void resetCleared();

    private:
        unordered_map<string, Attachment> _mapAttachment;
        /// @brief 꺼진 효과의 출력 이름 → 그 원본 이름입니다(`forward`). 뷰가 프레임마다 정하고 패스 기록 중에는 읽기만 한다.
        unordered_map<string, string> _mapForward;
        /// @brief 이번 프레임에 이미 클리어한 첨부들입니다. 병렬 패스가 동시에 갱신하므로 _clearedMutex 로 보호합니다.
        vector<hashed_string> _listClearedThisFrame;
        mutable mutex         _clearedMutex;
        uint32                _width;
        uint32                _height;
        uint32                _allocationCount;
    };
} // namespace sw
