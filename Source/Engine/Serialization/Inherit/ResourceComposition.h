/**
 * @file ResourceComposition.h
 * @brief 합성의 로드 단계(로드 축 조건 · 덮어쓰기 층)가 남기는 기록과, 덮어쓰기 층의 목록입니다.
 * @details - **덮어쓰기 층(`ResourceLayers`)**: 순서 있는 층 뿌리 목록이다. 리소스 `X` 의 층 파일은 `<뿌리>/X` 이고 원본을 암묵적 부모로 가진 차이다(`_base` 를 적지 않는다).
 *            언리얼 팩 우선순위 · Composite Data Table, 유니티 Addressables 의 콘텐츠 갱신과 같은 자리이고(기억에 의존), 파일 통째 교체(팩 우선순위)와 달리 다른 칸만 적는다.
 *          - **기록(`ResourceComposition`)**: 리소스 id 로 읽은 합성마다 그 결과가 무엇에 기댔는지(로드 축 태그 · 층)를 적는다. 세 곳이 읽는다 —
 *            (1) 태그 · 층이 바뀌면 기댄 리소스를 다시 읽을 목록(`takePendingReload` — 에셋 매니저가 프레임마다 비운다), (2) 합성 해시 대조(`collectTagDependentChanges` · `ResourceDigestComparer` — 서버와 다르면 거절),
 *            (3) 테스트. 기대지 않은 합성도 해시를 든다 — 서버가 층 없이 읽은 표를 클라이언트가 층을 얹어 읽으면 서버 쪽은 기댄 것이 없어도 대조해야 한다.
 *          층 목록과 기록의 주인은 코어 범위의 리소스 루트(`ContentRootSubsystem`)다 — 엔진 코드는 `engine::getSubsystem<ResourceComposition>()` 로 닿는다(예전 함수 정적 상태는 감춘 싱글턴이었다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /**
     * @struct ResourceLayers
     * @brief 덮어쓰기 층의 이름 규칙입니다. 층 목록 자체는 기록(`ResourceComposition::setLayers`)이 든다.
     */
    struct SW_API ResourceLayers
    {
        /** @brief 층 @p root 에서 리소스 @p assetID 의 층 파일 id 입니다. */
        [[nodiscard]] static string makeLayerAssetID( string_view root, string_view assetID );
    };

    /** @brief 접속 대조의 한 줄입니다(리소스 id 와 합성 결과의 해시). */
    struct ResourceDigestEntry
    {
        string _assetID{};
        uint64 _hash{ 0 };
    };

    /**
     * @class ResourceComposition
     * @brief 로드 단계 합성의 기록과 덮어쓰기 층 목록입니다. 로드 워커와 게임 스레드가 함께 부르므로 잠금으로 지킵니다.
     */
    class SW_API ResourceComposition
    {
    public:
        ResourceComposition();
        ~ResourceComposition();
        ResourceComposition( const ResourceComposition& )            = delete;
        ResourceComposition& operator=( const ResourceComposition& ) = delete;

        /**
         * @brief 층 목록을 바꿉니다(아래 층이 먼저, 위 층이 나중에 얹는다). 뿌리는 리소스 id 폴더(`game/<팩>/layers/night`)입니다. 바뀐 층에 파일이 있던 · 있는 리소스는 다시 읽을 목록에 오릅니다.
         * @details 게임 팩 설정(`PackEngineSettings::_listResourceLayer`)이 기동 때 넣고, DLC · 모드 팩을 올리는 쪽이 늘린다.
         * @return 뿌리가 비었거나 겹치면 오류를 남기고 false 입니다(목록을 바꾸지 않는다).
         */
        [[nodiscard]] bool setLayers( const vector<string>& listRoot );
        /** @brief 지금 층 목록입니다(사본). */
        [[nodiscard]] vector<string> getLayers() const;
        /**
         * @brief 리소스 id 로 읽은 합성 하나를 적습니다(`ResourceComposer`). 로드 축 태그도 층도 없으면 지난 기록을 지웁니다.
         * @param listLoadTag 결과가 기댄 로드 축 태그(조건이 참이었든 거짓이었든 — 값이 바뀌면 결과가 바뀔 수 있다)
         * @param bLayered 덮어쓰기 층 파일을 얹었다
         */
        void record( string_view assetID, const vector<string>& listLoadTag, bool bLayered, string_view composedText );
        /** @brief 경로 교체(`ResourceRemap`)가 @p assetID 를 고를 때 로드 축 태그 @p listLoadTag 를 물었다고 적습니다. 그 태그가 바뀌면 다시 읽는다(합성 기록과 따로 든다 — 같은 id 의 합성이 지우지 않게). */
        void recordRemap( string_view assetID, const vector<string>& listLoadTag );
        /** @brief 태그가 바뀌었습니다. 그 태그에 기댄 리소스를 다시 읽을 목록에 올립니다(`ResourceTags`). */
        void notifyTagsChanged( const vector<string>& listChangedTag );
        /** @brief 층 목록이 바뀌었습니다. 층을 얹었던 리소스와 새 층에 파일이 있는 리소스를 다시 읽을 목록에 올립니다(`ResourceLayers`). */
        void notifyLayersChanged( const vector<string>& listRoot );
        /** @brief 다시 읽을 리소스 id 를 꺼냅니다(비우고 채웁니다. 이름 순). 에셋 매니저가 틱 밖에서 부릅니다. */
        void takePendingReload( vector<string>& outListAssetID );
        /** @brief 로드 축 · 층에 기댄 합성의 대조 목록입니다(비우고 채웁니다. id 순). */
        void collectDigest( vector<ResourceDigestEntry>& outListEntry ) const;
        /**
         * @brief @p inoutRevision 뒤에 결과가 새로 생기거나 바뀐, 로드 축 · 층에 기댄 합성의 대조 목록입니다(비우고 채웁니다. id 순). 클라이언트가 접속 뒤에 읽은 리소스를 서버에 알린다.
         * @param inoutRevision 지난번에 받은 번호(처음은 0 — 모두). 지금 번호로 고쳐 둡니다.
         */
        void collectTagDependentChanges( uint64& inoutRevision, vector<ResourceDigestEntry>& outListEntry ) const;
        /** @brief `collectTagDependentChanges` 와 같되 기대지 않은 합성도 넣습니다. 서버가 자기 합성을 받아 둔 상대의 줄과 맞댄다(`ResourceDigestComparer`). */
        void collectAllChanges( uint64& inoutRevision, vector<ResourceDigestEntry>& outListEntry ) const;
        /**
         * @brief 상대가 보낸 한 줄(@p entry)이 이 프로세스의 합성과 다르면 true 입니다. 이 프로세스가 그 리소스를 리소스 id 로 합성하지 않았으면 false 입니다(대조할 것이 없다).
         * @details 서버가 읽지 않는 리소스(전용 서버의 시각 효과)는 대조하지 않는다 — 서버가 든 게임플레이 데이터만 갈린다. 그래서 품질 축이 시각 값만 바꾸면 접속이 되고, 시뮬레이션 값을 바꾸면 거절된다.
         */
        [[nodiscard]] bool isDigestMismatch( const ResourceDigestEntry& entry ) const;
        /** @brief @p assetID 를 덮어쓰기 층을 얹어 합성했으면 true 입니다(저장이 층 값을 원본에 박지 않게 묻는다). */
        [[nodiscard]] bool isLayered( string_view assetID ) const;
        /** @brief 합성 결과의 해시입니다(대조 양쪽이 같은 함수를 쓴다). */
        [[nodiscard]] static uint64 hashText( string_view text );
        /** @brief 기록을 모두 지웁니다(테스트). */
        void clear();

        /** @brief 상태 전부(층 목록 · 기록 · 다시 읽을 목록)입니다. 이름만 공개다(.cpp 의 내부 도우미가 받는다) — 잠금 · 해시 맵이 헤더로 새지 않게. */
        struct State;

    private:
        unique_ptr<State> _pState;
    };

    SW_SERVICE_SLOT( ResourceComposition, Required );

    /** @brief 유예가 지나도 어긋난 한 줄입니다(`ResourceDigestComparer::update`). */
    struct ResourceDigestMismatch
    {
        int32  _peer{ 0 };
        string _assetID{};
    };

    /**
     * @class ResourceDigestComparer
     * @brief 서버가 상대(연결)마다 받은 합성 줄을 들고 자기 합성과 맞댑니다. 게임 스레드에서만 부릅니다.
     * @details 접속 때 한 번만 맞대면 붙은 뒤에 읽은 리소스 · 바뀐 태그는 대조되지 않는다. 그래서 어느 쪽이 나중에 읽든 만나게 줄을 들고 있는다 — 상대 줄이 오면 자기 합성과, 자기 합성이 바뀌면 받아 둔 줄과 맞댄다.
     *          어긋나도 바로 끊지 않고 유예를 둔다. 복제되는 태그(서버가 켠 데이터 레이어)는 양쪽이 다른 프레임에 바뀌어 잠깐 어긋나는 것이 정상이다. 언리얼은 패키지 지문을 접속 때만 대조한다(기억에 의존).
     */
    class SW_API ResourceDigestComparer
    {
    public:
        static constexpr uint32 kMaxLinesPerPeer = 4096; ///< 상대 하나가 들게 하는 줄 수 상한(신뢰 경계 — 상대가 보내는 대로 메모리를 쓰지 않는다)

        /** @brief @p composition 은 이 프로세스의 합성 기록이다(이것보다 오래 산다). */
        explicit ResourceDigestComparer( const ResourceComposition& composition );

        /**
         * @brief 상대가 보낸 줄을 받아 둡니다. 같은 id 는 새 값으로 바꿉니다.
         * @return 상대의 줄이 상한을 넘으면 받지 않고 false 입니다(부르는 쪽이 끊는다).
         */
        [[nodiscard]] bool addPeerLine( int32 peer, const ResourceDigestEntry& entry, int64 nowNanoseconds );
        /** @brief 연결이 닫혔습니다. 그 상대의 줄을 버립니다. */
        void removePeer( int32 peer );
        /**
         * @brief 자기 합성의 변화를 받아 맞대고, 어긋난 지 @p graceNanoseconds 가 지나도 어긋난 줄을 @p outListMismatch 에 더합니다.
         * @details 내보낸 줄은 지웁니다 — 부르는 쪽이 끊고 `removePeer` 합니다.
         */
        void update( int64 nowNanoseconds, int64 graceNanoseconds, vector<ResourceDigestMismatch>& outListMismatch );
        /** @brief 모두 비웁니다(세션 종료). */
        void clear();

    private:
        /** @brief 받아 둔 한 줄입니다. */
        struct PeerLine
        {
            int32               _peer{ 0 };
            ResourceDigestEntry _entry{};
            optional<int64>     _suspectStartTime; ///< 어긋남을 처음 본 때. 비어 있으면 맞다
        };

        const ResourceComposition& _composition;
        vector<PeerLine>           _listLine;
        uint64                     _revision; ///< 자기 합성을 어디까지 맞댔는가(`ResourceComposition::collectAllChanges`)
    };
} // namespace sw
