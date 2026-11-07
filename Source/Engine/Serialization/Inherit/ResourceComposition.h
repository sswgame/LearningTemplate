/**
 * @file ResourceComposition.h
 * @brief 합성의 로드 단계(로드 축 조건 · 덮어쓰기 층)가 남기는 기록과, 덮어쓰기 층의 목록입니다.
 * @details - **덮어쓰기 층(`ResourceLayers`)**: 순서 있는 층 뿌리 목록이다. 리소스 `X` 의 층 파일은 `<뿌리>/X` 이고 원본을 암묵적 부모로 가진 차이다(`_base` 를 적지 않는다).
 *            언리얼 팩 우선순위 · Composite Data Table, 유니티 Addressables 의 콘텐츠 갱신과 같은 자리이고(기억에 의존), 파일 통째 교체(팩 우선순위)와 달리 다른 칸만 적는다.
 *          - **기록(`ResourceComposition`)**: 리소스 id 로 읽은 합성마다 그 결과가 무엇에 기댔는지(로드 축 태그 · 층)를 적는다. 세 곳이 읽는다 —
 *            (1) 태그 · 층이 바뀌면 기댄 리소스를 다시 읽을 목록(`takePendingReload` — 에셋 매니저가 프레임마다 비운다), (2) 접속 때의 합성 해시 대조(`collectDigest` — 서버와 다르면 거절),
 *            (3) 테스트. 쿠킹 축만 쓴 합성은 적지 않는다(쿠킹본이 같으면 결과가 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct ResourceLayers
     * @brief 프로세스의 덮어쓰기 층 목록입니다(아래 층이 먼저, 위 층이 나중에 얹는다). 게임 팩 설정(`PackEngineSettings::_listResourceLayer`)이 기동 때 넣고, DLC · 모드 팩을 올리는 쪽이 늘린다.
     */
    struct SW_API ResourceLayers
    {
        /**
         * @brief 층 목록을 바꿉니다. 뿌리는 리소스 id 폴더(`game/<팩>/layers/night`)입니다. 바뀐 층에 파일이 있던 · 있는 리소스는 다시 읽을 목록에 오릅니다.
         * @return 뿌리가 비었거나 겹치면 오류를 남기고 false 입니다(목록을 바꾸지 않는다).
         */
        [[nodiscard]] static bool setLayers( const vector<string>& listRoot );
        /** @brief 지금 층 목록입니다(사본). */
        [[nodiscard]] static vector<string> getLayers();
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
     * @struct ResourceComposition
     * @brief 로드 단계 합성의 기록입니다. 로드 워커와 게임 스레드가 함께 부르므로 잠금으로 지킵니다.
     */
    struct SW_API ResourceComposition
    {
        /**
         * @brief 리소스 id 로 읽은 합성 하나를 적습니다(`ResourceComposer`). 로드 축 태그도 층도 없으면 지난 기록을 지웁니다.
         * @param listLoadTag 결과가 기댄 로드 축 태그(조건이 참이었든 거짓이었든 — 값이 바뀌면 결과가 바뀔 수 있다)
         * @param bLayered 덮어쓰기 층 파일을 얹었다
         */
        static void record( string_view assetID, const vector<string>& listLoadTag, bool bLayered, string_view composedText );
        /** @brief 리소스 id 로 합성한 리소스라고 적습니다(층 목록이 바뀔 때 새 층 파일을 찾을 대상). */
        static void recordComposed( string_view assetID );
        /** @brief 경로 교체(`ResourceRemap`)가 @p assetID 를 고를 때 로드 축 태그 @p listLoadTag 를 물었다고 적습니다. 그 태그가 바뀌면 다시 읽는다(합성 기록과 따로 든다 — 같은 id 의 합성이 지우지 않게). */
        static void recordRemap( string_view assetID, const vector<string>& listLoadTag );
        /** @brief 태그가 바뀌었습니다. 그 태그에 기댄 리소스를 다시 읽을 목록에 올립니다(`ResourceTags`). */
        static void notifyTagsChanged( const vector<string>& listChangedTag );
        /** @brief 층 목록이 바뀌었습니다. 층을 얹었던 리소스와 새 층에 파일이 있는 리소스를 다시 읽을 목록에 올립니다(`ResourceLayers`). */
        static void notifyLayersChanged( const vector<string>& listRoot );
        /** @brief 다시 읽을 리소스 id 를 꺼냅니다(비우고 채웁니다. 이름 순). 에셋 매니저가 틱 밖에서 부릅니다. */
        static void takePendingReload( vector<string>& outListAssetID );
        /** @brief 접속 대조 목록입니다(비우고 채웁니다. id 순). */
        static void collectDigest( vector<ResourceDigestEntry>& outListEntry );
        /**
         * @brief 상대가 보낸 한 줄(@p entry)이 이 프로세스의 합성과 다르면 true 입니다. 이 프로세스가 그 리소스를 로드 축 · 층에 기대 합성하지 않았으면 false 입니다(대조할 것이 없다).
         * @details 서버가 읽지 않는 리소스(전용 서버의 시각 효과)는 대조하지 않는다 — 서버가 든 게임플레이 데이터만 갈린다. 그래서 품질 축이 시각 값만 바꾸면 접속이 되고, 시뮬레이션 값을 바꾸면 거절된다.
         */
        [[nodiscard]] static bool isDigestMismatch( const ResourceDigestEntry& entry );
        /** @brief 합성 결과의 해시입니다(대조 양쪽이 같은 함수를 쓴다). */
        [[nodiscard]] static uint64 hashText( string_view text );
        /** @brief 기록을 모두 지웁니다(엔진 종료 · 테스트). */
        static void clear();
    };
} // namespace sw
