/**
 * @file ContentPreparationTestToken.h
 * @brief 테스트가 게임 인스턴스 없이 준비 단계 증표(`ContentPreparationToken`)를 받는 입구입니다. `Tests/` 아래라 배포 구성은 이 선언도 정의도 보지 않는다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Flow/IContentConfiguration.h"

namespace sw
{
    /**
     * @brief 정의는 `ContentPreparationTestToken.cpp` 하나뿐이다.
     * @details 내보낸(`SW_GF_API`) 선언이라 다른 모듈(키트 · 게임)은 이 함수를 정의할 수 없고(가져온 함수의 정의는 컴파일 오류),
     *          GameFramework 안에서 다시 정의하면 개발 구성 링크가 겹친 심볼로 멈춘다. 증표의 friend 선언과 같아야 한다(-Winconsistent-dllimport).
     */
    SW_GF_API const ContentPreparationToken& getContentPreparationTokenForTest();
} // namespace sw
