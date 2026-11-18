/**
 * @file IContentConfiguration.h
 * @brief 이 실행의 콘텐츠 구성(켠 모드 등 기본 팩 위에 얹은 것)을 게임 인스턴스 · 넷 세션 · 세이브 봉투가 보는 창구입니다.
 * @details 구성을 정하는 쪽(모드 지원 키트의 `ModSession`)은 프레임워크보다 위 층이라, 세션 · 세이브는 이 창구로만 본다. 게임이 `onPrepareContent` 에서 구성을 정하고
 *          게임 범위에 이 타입으로 건다(`bind<IContentConfiguration>`). 걸린 것이 없으면 구성 없음이다(해시 0 — 와이어 버전 · 세이브는 그대로).
 *          언리얼이 콘텐츠 해시를 넷 버전과 세이브 머리에 섞는 자리와 같다(기억에 의존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameInstance;
    class ContentPreparationToken;

    /**
     * @brief 테스트가 게임 인스턴스 없이 준비 단계 증표를 받는 유일한 길입니다. 정의는 `Flow/Tests/ContentPreparationTestToken.cpp` 하나뿐이다.
     * @details 형식으로 닫는다 — 내보낸(`SW_GF_API`) 선언이라 다른 모듈(키트 · 게임)은 이 함수를 정의할 수 없고(가져온 함수의 정의는 컴파일 오류),
     *          GameFramework 안에서 다시 정의하면 개발 구성 링크가 겹친 심볼로 멈춘다. 배포 구성은 `Tests/` 를 짓지 않으므로 부르는 코드가 링크되지 않는다.
     */
    SW_GF_API const ContentPreparationToken& getContentPreparationTokenForTest();

    /**
     * @class ContentPreparationToken
     * @brief 콘텐츠 구성을 정하는 준비 단계(`GameInstance::onPrepareContent`)에 있다는 증표입니다. 게임 인스턴스만 만든다 — 구성을 바꾸는 입구(`ModSession::applyForGame`)가 이것을 받아
     *        준비 훅 밖(예: `onInitialize`)에서 부르는 코드는 컴파일되지 않는다(감사 K-9 — 넷 세션 · 첫 씬 · 선언한 데이터를 읽은 뒤에 구성이 바뀌던 길).
     * @details 복사 · 이동이 막혀 훅이 끝난 뒤로 들고 나가지 못한다. 테스트는 `getContentPreparationTokenForTest`(테스트 빌드에만 정의가 있다)로 받는다.
     */
    class ContentPreparationToken
    {
        friend class GameInstance;
        friend SW_GF_API const ContentPreparationToken& getContentPreparationTokenForTest(); // 내보낸 선언과 같아야 한다(-Winconsistent-dllimport)

    public:
        ContentPreparationToken( const ContentPreparationToken& )            = delete;
        ContentPreparationToken& operator=( const ContentPreparationToken& ) = delete;

    private:
        ContentPreparationToken() = default;
    };

    /** @class IContentConfiguration @brief 콘텐츠 구성 하나입니다. 게임 스레드에서 읽습니다. */
    class IContentConfiguration
    {
    public:
        virtual ~IContentConfiguration() = default;

        /** @brief 구성 해시입니다. 구성이 비었으면 0 입니다 — 넷 와이어 버전에 섞인다(`GameNetSession::combineWireContent`). */
        virtual uint64 getContentConfigHash() const = 0;
        /** @brief 세이브 봉투 머리에 적는 글입니다(빈 구성은 빈 글). `checkSaveCompatible` 이 이 글을 받는다. */
        virtual string makeContentConfigText() const = 0;
        /**
         * @brief 세이브 머리에 적힌 구성 @p savedText 의 세이브를 지금 구성으로 읽어도 되면 true 입니다. 아니면 까닭을 오류로 남기고 false 입니다.
         * @details 세이브의 모드가 지금 꺼졌거나 판이 다르면 거절한다(없는 리소스를 가리키는 세이브를 조용히 비우지 않는다). 새로 켠 모드는 받는다.
         */
        [[nodiscard]] virtual bool checkSaveCompatible( string_view savedText ) const = 0;
    };
} // namespace sw
