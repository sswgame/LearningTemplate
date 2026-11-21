/**
 * @file StateSectionTableUtil.h
 * @brief 상태 여럿을 구간 표(`StateIO::writeSection` 구간 하나씩)로 싣고 읽는 틀입니다. 월드 상태(`GameStateComponent`)와 플레이어 진행(`PlayerProgressComponent`)이 같은 틀을 쓴다.
 * @details 알 수 없는 구간은 건너뛰고, 빠진 구간은 새 판으로 둔다. 구간 하나라도 읽지 못하면 로드 전체가 실패하고 상태는 읽기 전 그대로다(전부 아니면 없음).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    /**
     * @struct StateSectionTableUtil
     * @brief 구간 표 쓰기 · 읽기입니다. @p forEachSection 은 `visit( state, pName )` 을 구간마다 부르는 함수 하나 — 구간 표가 부르는 쪽 한 곳에 있다.
     */
    struct StateSectionTableUtil
    {
        static constexpr uint32 kMinSectionBytes = 12; ///< 구간마다 태그, 버전, 길이(uint32 셋) 이상

        /** @brief 읽은 구간 하나입니다. 본문은 읽은 바이트를 가리키는 뷰입니다. */
        struct Section
        {
            Archive _body{};
            uint32  _tag{ 0 };
            uint32  _version{ 0 };
        };

        /** @brief 상태 하나를 그 타입의 태그와 버전으로 구간 하나에 씁니다. */
        template <typename TState>
        static void writeStateSection( Archive& outArchive, const TState& state )
        {
            Archive body;
            state.writeState( body );
            StateIO::writeSection( outArchive, TState::kStateTag, TState::kStateVersion, body );
        }

        /**
         * @brief 구간 하나를 사본에 읽고 끝까지 맞으면 반영합니다. 옛 판은 판 올림 사슬(`SW_STATE_FORMAT_UPGRADE`)로 올려 읽습니다(감사 A-6).
         *        올릴 단계가 없거나 깨졌으면 경고를 남기고 그대로 둔 채 false 입니다(부르는 쪽이 로드 전체를 실패시킨다).
         */
        template <typename TState>
        [[nodiscard]] static bool applyStateSection( const Section& section, TState& inoutState, const utf8* pName )
        {
            Archive body = section._body;
            // 머리로 판을 올리는 형식(`kStateHeaderlessVersion` — 세계 시계)은 안쪽 머리가 올린다 — 구간 판으로 또 올리면 두 번 오른다.
            constexpr bool bVersionedByHeader = StateIOInternal::HasHeaderlessVersion<TState>::value;
            if ( bVersionedByHeader == false && StateIO::upgradeSectionBody( TState::kStateTag, section._version, TState::kStateVersion, body ) == false )
            {
                SW_LOG_WARNING_ID( "GameState.SectionVersionNot", "GameState: %# section version %# is not %# and has no upgrade chain to it", pName, section._version,
                                   TState::kStateVersion );
                return false;
            }
            TState     state = inoutState;
            const bool bRead = state.readState( body ) && body.getRemainingBytes() == 0;
            if ( bRead == false )
            {
                SW_LOG_WARNING_ID( "GameState.SectionBrokenStarts", "GameState: %# section is broken", pName );
                return false;
            }
            inoutState = std::move( state );
            return true;
        }

        /** @brief 머리(@p tag · @p version), 구간 수 @p sectionCount, 구간들을 씁니다. */
        template <typename TForEach>
        static void write( Archive& archive, uint32 tag, uint32 version, uint32 sectionCount, const TForEach& forEachSection )
        {
            StateIO::writeHeader( archive, tag, version );
            archive << sectionCount;
            forEachSection( [&archive]( const auto& state, const utf8* )
            { writeStateSection( archive, state ); } );
        }

        /**
         * @brief 머리 뒤 구간 표를 읽어 상태마다 반영합니다. 구조가 깨졌거나 구간 하나라도 읽지 못하면 읽기 전 상태로 되돌리고 `io.fail()` 이며 false 입니다.
         * @param pOwnerName 거절 오류에 적을 이름(예 "the session state")
         */
        template <typename TForEach>
        [[nodiscard]] static bool read( StateIO& io, uint32 tag, uint32 version, uint32 sectionCount, const TForEach& forEachSection, const utf8* pOwnerName )
        {
            io.header( tag, version );
            Archive& archive    = io.getArchive(); // 머리 뒤에 받는다 — 옛 판이면 올린 바이트다
            uint32   savedCount = 0;
            io.count( kMinSectionBytes, savedCount );
            if ( io.isOk() == false )
                return false;
            // 구조를 끝까지 먼저 읽는다. 구조가 깨졌으면 아무것도 바꾸지 않는다.
            vector<Section> listSection;
            listSection.resize( savedCount );
            for ( Section& section : listSection )
            {
                if ( io.check( StateIO::readSection( archive, section._tag, section._version, section._body ) ) == false )
                    return false;
            }
            if ( io.check( archive.getRemainingBytes() == 0 ) == false )
                return false;
            // 구간 하나라도 읽지 못하면 로드 전체가 실패다(감사 P0-5) — 예전에는 그 구간만 경고하고 새 판으로 두어, 가방 슬롯 수 같은 설정을 바꾼 패치 뒤
            // 인벤토리 · 땅 · 달력이 통째로 사라졌고 이어진 저장이 그것을 굳혔다. 읽기 전 상태를 같은 구간 형식으로 떠 두었다가 실패하면 되돌린다(전부 아니면 없음).
            Archive backup;
            forEachSection( [&backup]( const auto& state, const utf8* )
            { writeStateSection( backup, state ); } );
            bool bAllApplied = true;
            for ( const Section& section : listSection )
            {
                bool bKnown = false;
                forEachSection( [&section, &bKnown, &bAllApplied]( auto& state, const utf8* pName )
                {
                    if ( section._tag == std::decay_t<decltype( state )>::kStateTag )
                    {
                        if ( applyStateSection( section, state, pName ) == false )
                            bAllApplied = false;
                        bKnown = true;
                    }
                } );
                if ( bKnown == false )
                    SW_LOG_WARNING_ID( "GameState.UnknownSectionSkipped", "GameState: unknown section %# skipped", section._tag );
            }
            if ( bAllApplied )
                return true;
            // 되돌린다 — 방금 떠 둔 구간이라 반드시 읽힌다.
            for ( uint32 sectionIndex = 0; sectionIndex < sectionCount && backup.getRemainingBytes() > 0; ++sectionIndex )
            {
                Section section;
                if ( StateIO::readSection( backup, section._tag, section._version, section._body ) == false )
                    break;
                forEachSection( [&section]( auto& state, const utf8* pName )
                {
                    if ( section._tag == std::decay_t<decltype( state )>::kStateTag )
                        (void)applyStateSection( section, state, pName ); // 같은 판으로 떠 둔 것이다
                } );
            }
            SW_LOG_ERROR_ID( "GameState.LoadRejected", "GameState: a section of the save could not be read - the save is not loaded (%# is unchanged)", pOwnerName );
            io.fail();
            return false;
        }
    };
} // namespace sw
