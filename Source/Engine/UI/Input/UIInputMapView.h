/**
 * @file UIInputMapView.h
 * @brief 게임 입력 맵을 읽되, 런타임 UI 가 먹은 입력과 게임 입력을 막는 화면(모달 · 로딩)을 걸러서 돌려주는 읽기 전용 창구입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class InputMap;
    class UISystem;
} // namespace sw

namespace sw
{
    /**
     * @class UIInputMapView
     * @brief 입력 맵의 읽기 질의(`wasActionTriggered` · `wasActionPressed` · `isActionDown` · `getAxis1D` · `getVector2D`)와 같은 이름 · 같은 뜻인데,
     *        UI 가 먹은 액션(`UISystem::isActionConsumed`)이거나 게임 입력이 막힌 상태(`UISystem::isGameInputBlocked`)이면 "누르지 않음"(false, 0)을 돌려줍니다.
     * @details 플레이어 조종자는 입력을 의도로 바꿀 때 같은 규칙을 쓴다. 명령 조종자(디렉터)와 카메라처럼 입력 맵을 직접 읽는 곳은 `const InputMap& inputMap = input.getInputMap();` 를
     *          `const UIInputMapView inputMap( input.getInputMap(), pUISystem );` 로 바꾸면 호출은 그대로이고 메뉴에서 누른 패드 A, 위젯이 받은 클릭이 게임으로 새지 않는다
     *          (언리얼의 입력 소비, 유니티의 `EventSystem.IsPointerOverGameObject` 에 해당). UI 시스템이 없으면(전용 서버, 헤드리스 테스트) 입력 맵을 그대로 읽는다.
     *          값 복사가 싸고 프레임을 넘겨 들지 않는다 — 입력 맵과 UI 시스템 포인터만 든다.
     */
    class SW_API UIInputMapView
    {
    public:
        /** @param pUISystem 먹은 입력을 알려 줄 UI 시스템입니다(nullptr 이면 거르지 않습니다). 초기화 전의 시스템도 거르지 않습니다. */
        UIInputMapView( const InputMap& inputMap, const UISystem* pUISystem );

        bool    wasActionTriggered( const hashed_string& action ) const;
        bool    wasActionPressed( const hashed_string& action ) const;
        bool    isActionDown( const hashed_string& action ) const;
        float32 getAxis1D( const hashed_string& action ) const;
        float2  getVector2D( const hashed_string& action ) const;

        /** @brief 이 액션을 UI 가 먹었거나 게임 입력이 막혀 있으면 true 입니다. */
        bool isActionBlocked( const hashed_string& action ) const;

    private:
        const InputMap& _inputMap;
        const UISystem* _pUISystem;
    };
} // namespace sw
