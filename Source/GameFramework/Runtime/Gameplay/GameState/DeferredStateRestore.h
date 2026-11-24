/**
 * @file DeferredStateRestore.h
 * @brief 열기 전에 받은 복원 바이트를 들고 있다가 열 때 적용하는 칸(`DeferredStateRestore`)과 열기 결과(`GameStateInitResult`)입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/GameState/StateSectionTableUtil.h"

namespace sw
{
    /** @brief `DeferredStateRestore::open` 을 부르는 `initialize`(월드 상태 · 플레이어 상태 컴포넌트)의 결과입니다. */
    enum class GameStateInitResult : uint8
    {
        Fresh = 0,          ///< 새 판이다. 여는 디렉터가 시작값(시작 돈, 첫 플래그)을 넣는다
        Restored,           ///< 보관하고 있던 세이브나 핫 리로드 바이트로 복원했다. 시작값은 넣지 않는다
        AlreadyInitialized, ///< 앞 디렉터가 이미 열었다. 설정은 무시하며, 새 판인지는 `isFreshGame` 으로 확인한다
        Refused             ///< 설정이 선언된 범위 밖이라 열지 않았다(오류를 남겼다). 여는 디렉터는 시작을 멈춘다
    };

    /**
     * @class DeferredStateRestore
     * @brief 열기(카탈로그 · 설정) 전에 받은 복원 바이트를 들고 있다가 열 때 적용하는 칸입니다. 월드 상태(`GameStateComponent`)와 플레이어 상태 컴포넌트가 멤버로 듭니다.
     * @details 세이브 복원(`ComponentStateCodec::restoreObject`)은 디렉터의 `startGame`(열기)보다 먼저 올 수 있고, 읽기에는 카탈로그가 있어야 한다(일지는 카탈로그에 없는
     *          퀘스트를 버린다). 열린 뒤에 받은 바이트는 바로 적용한다. 새 판인지(`isFreshGame`)는 디렉터가 시작값(시작 돈 · 첫 아이템)을 넣을지 볼 때 묻는다.
     *          상태는 구간 표(`StateSectionTableUtil`)로 싣는다. 안쪽 형식의 판 올림 사슬과 "전부 아니면 없음" 을 그 틀이 맡는다.
     */
    class DeferredStateRestore
    {
    public:
        DeferredStateRestore()
            : _heldBytes{}
            , _bOpen{ SW_FALSE }
            , _bFreshGame{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool isOpen() const { return _bOpen == SW_TRUE; }
        bool isFreshGame() const { return _bFreshGame == SW_TRUE; }

        /**
         * @brief 엽니다. 들고 있던 바이트가 있으면 @p component 의 `readState` 로 적용합니다. 부르는 쪽은 카탈로그 · 설정을 먼저 넣습니다.
         * @return 새 판, 복원, 이미 열림 중 하나입니다. 바이트를 읽지 못했으면 오류를 남기고 새 판(`Fresh`)입니다.
         */
        template <typename TComponent>
        [[nodiscard]] GameStateInitResult open( TComponent& component )
        {
            if ( _bOpen == SW_TRUE )
                return GameStateInitResult::AlreadyInitialized;
            _bOpen      = SW_TRUE;
            _bFreshGame = SW_TRUE;
            if ( _heldBytes.empty() || apply( component ) == false )
                return GameStateInitResult::Fresh;
            _bFreshGame = SW_FALSE;
            return GameStateInitResult::Restored;
        }

        /** @brief 복원 바이트를 받습니다. 열리기 전이면 들고 있다가 `open` 에서, 열린 뒤면 바로 적용합니다. 컴포넌트의 `restoreState` 가 부릅니다. */
        template <typename TComponent>
        void receive( TComponent& component, vector<uint8>&& bytes )
        {
            _heldBytes = std::move( bytes );
            if ( _bOpen == SW_TRUE && apply( component ) )
                _bFreshGame = SW_FALSE;
        }

        /**
         * @brief 열린 상태를 다시 읽힌 카탈로그에 다시 묶습니다 — 상태를 뜨고, @p bind 로 새 판에 열고, 같은 상태를 읽습니다(항목은 id 로 다시 찾는다). 게임 스레드 · 틱 밖에서 부릅니다.
         * @details 디렉터의 다시 묶기(`GameModeComponent::applyReloadedAssets`)와 같은 길이다. 디렉터가 다시 열어도 이 컴포넌트는 이미 열려 새 판을 받지 않으므로 스스로 따라간다.
         */
        template <typename TComponent, typename TBind>
        void rebind( TComponent& component, const TBind& bind )
        {
            if ( _bOpen == SW_FALSE )
                return;
            Archive state;
            StateIO::writeObject( component, state );
            state.setReadModeAndResetPosition( true );
            bind();
            if ( component.readState( state ) )
                SW_LOG_INFO_ID( "DeferredStateRestore.CatalogRebound", "%#: a reloaded catalog is applied to the open state", component.getTypeName().c_str() );
            else
                SW_LOG_WARNING_ID( "DeferredStateRestore.CatalogRefused", "%#: the open state does not fit the reloaded catalog (see the messages above) - it starts fresh", component.getTypeName().c_str() );
        }

        /**
         * @brief 머리(@p tag · @p version)와 구간 표(@p sectionCount 개 — @p forEachSection 이 `visit( state, pName )` 을 구간마다 부른다)를 쓰거나 읽습니다. 컴포넌트의 `serializeState` 가 부릅니다.
         * @details 열리기 전(복원한 직후의 두 번째 캡처)에 바이트를 들고 있으면 그 바이트가 곧 상태이다 — 빈 판을 쓰면 세이브를 잃는다.
         * @return 읽기면 모두 반영했을 때 true(@p pOwnerName 은 거절 오류에 적는다), 쓰기면 true 입니다.
         */
        template <typename TForEach>
        [[nodiscard]] bool serializeSections( StateIO& io, uint32 tag, uint32 version, uint32 sectionCount, const TForEach& forEachSection, const utf8* pOwnerName ) const
        {
            if ( io.isWriting() == false )
                return StateSectionTableUtil::read( io, tag, version, sectionCount, forEachSection, pOwnerName );
            Archive& archive = io.getArchive();
            if ( _bOpen == SW_FALSE && _heldBytes.empty() == false )
                archive.writeBytes( _heldBytes.data(), _heldBytes.size() );
            else
                StateSectionTableUtil::write( archive, tag, version, sectionCount, forEachSection );
            return true;
        }

        /** @brief 상태 하나(@p state)를 구간 하나로 쓰거나 읽습니다(`serializeSections`). */
        template <typename TState>
        void serialize( StateIO& io, uint32 tag, uint32 version, TState& state, const utf8* pName ) const
        {
            const auto forEachSection = [&state, pName]( auto&& visit )
            { visit( state, pName ); };
            (void)serializeSections( io, tag, version, 1, forEachSection, pName );
        }

    private:
        /** @brief 들고 있던 바이트를 적용하고 비웁니다. 구조가 깨졌으면 오류를 남기고 false 입니다. */
        template <typename TComponent>
        [[nodiscard]] bool apply( TComponent& component )
        {
            Archive    archive( _heldBytes.data(), _heldBytes.size() );
            const bool bRestored = component.readState( archive );
            _heldBytes.clear();
            if ( bRestored == false )
                SW_LOG_ERROR_ID( "DeferredStateRestore.SavedStateUnread", "%# on '%#': the saved state could not be read - it starts fresh", component.getTypeName().c_str(),
                                 component.getOwner() != nullptr ? component.getOwner()->getName().c_str() : "?" );
            return bRestored;
        }

    private:
        vector<uint8>          _heldBytes; ///< 열리기 전에 받은 복원 바이트
        uint8                  _bOpen      : 1;
        uint8                  _bFreshGame : 1;
        [[maybe_unused]] uint8 _reserved   : 6;
    };
} // namespace sw
