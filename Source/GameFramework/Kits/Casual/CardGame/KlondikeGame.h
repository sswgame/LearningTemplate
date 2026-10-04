/**
 * @file KlondikeGame.h
 * @brief 솔리테어(클론다이크) — 7 열 · 파운데이션 4 · 스톡 · 웨이스트, 옮기기 규칙 검사, 1 장/3 장 뽑기, 자동 완료, 되돌리기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Casual/CardGame/CardDeck.h"

namespace sw
{
    /** @brief 규칙 설정입니다. */
    struct KlondikeSettings
    {
        int32 _drawCount{ 1 };   ///< 스톱에서 한 번에 뒤집는 장수(1 또는 3)
        int32 _maxRecycle{ -1 }; ///< 웨이스트를 스톡으로 되돌릴 수 있는 횟수(−1 = 제한 없음)
    };
} // namespace sw

namespace sw
{
    /** @brief 판 전체입니다. 되돌리기는 이것을 통째로 쌓습니다(52 장이라 작다). */
    struct KlondikeState
    {
        static constexpr int32 kColumnCount     = 7;
        static constexpr int32 kFoundationCount = 4;

        CardPile _arrTableau[kColumnCount]{};
        int32    _arrFaceDownCount[kColumnCount]{};  ///< 열마다 아래에서부터 뒤집힌 장수
        CardPile _arrFoundation[kFoundationCount]{}; ///< 자리 = 무늬(StandardSuit)
        CardPile _stock{};
        CardPile _waste{};
        int32    _recycleCount{ 0 };
        int32    _moveCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class KlondikeGame
     * @brief 클론다이크 한 판입니다. 옮기기는 모두 규칙을 검사하고, 어긋나면 false 이며 아무것도 바뀌지 않습니다.
     * @details 테이블로는 색이 번갈아 하나씩 내려가야 하고 빈 열에는 킹만, 파운데이션은 무늬별로 에이스부터 하나씩 올라갑니다.
     *          열에서 패를 옮겨 뒤집힌 패가 맨 위가 되면 저절로 뒤집습니다(되돌리기는 그것까지 되돌린다).
     */
    class SW_GF_API KlondikeGame
    {
    public:
        KlondikeGame();

        void initialize( const KlondikeSettings& settings, uint32 seed );
        /** @brief 주어진 덱(맨 위부터)을 한 줄씩 나눕니다 — 열 c 에 c + 1 장, 맨 위만 앞면, 나머지는 스톡입니다. */
        void initializeWithDeck( const KlondikeSettings& settings, const CardPile& deck );
        /** @brief 판 모양을 그대로 둡니다(시험 · 이어 하기). */
        void initializeFromState( const KlondikeSettings& settings, const KlondikeState& state );

        /** @brief 스톡에서 뒤집습니다. 스톡이 비었으면 웨이스트를 되돌립니다. 둘 다 비었거나 되돌리기 횟수를 다 썼으면 false 입니다. */
        [[nodiscard]] bool drawStock();
        [[nodiscard]] bool moveWasteToTableau( int32 column );
        [[nodiscard]] bool moveWasteToFoundation();
        [[nodiscard]] bool moveTableauToFoundation( int32 column );
        /** @brief 열 @p fromColumn 의 맨 위 @p count 장(앞면이어야 한다)을 @p toColumn 위로 옮깁니다. */
        [[nodiscard]] bool moveTableauToTableau( int32 fromColumn, int32 count, int32 toColumn );
        [[nodiscard]] bool moveFoundationToTableau( int32 suit, int32 column );
        /** @brief 마지막 옮기기를 되돌립니다. 되돌릴 것이 없으면 false 입니다. */
        [[nodiscard]] bool undo();

        /** @brief @p card 를 @p column 위에 놓을 수 있는가입니다(색 번갈이 · 하나 아래 · 빈 열은 킹). */
        static bool canStackOnTableau( const Card& card, const CardPile& column );
        bool        canPlaceOnFoundation( const Card& card ) const;
        bool        isWon() const;
        /** @brief 스톡 · 웨이스트가 비었고 뒤집힌 패가 없어 파운데이션으로만 올려도 끝나는가입니다. */
        bool canAutoComplete() const;
        /** @brief 자동 완료 — 올릴 수 있는 패를 파운데이션으로 올립니다(되돌리기 한 번에 묶인다). 올린 장수입니다. */
        int32 autoComplete();

        const KlondikeState&    getState() const { return _state; }
        const KlondikeSettings& getSettings() const { return _settings; }
        int32                   getUndoCount() const { return static_cast<int32>( _listHistory.size() ); }

    private:
        bool isColumnValid( int32 column ) const;
        void pushHistory();
        void revealColumnTop( int32 column );
        void moveToFoundation( const Card& card );

        vector<KlondikeState> _listHistory;
        KlondikeState         _state;
        KlondikeSettings      _settings;
    };
} // namespace sw
