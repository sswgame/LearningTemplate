/**
 * @file SequenceDebugSections.cpp
 * @brief 런타임 컷씬 디버거(디버그 섹션 `sequence`)입니다 — 대상 오브젝트의 시퀀스 플레이어(없으면 활성 씬의 첫 플레이어)의 플레이헤드 · 걸린 항목 · 바인딩 · 키트 해석을 읽는다.
 * @details 에디터 Cutscene Debugger 창(`SequenceDebuggerPanel`)과 같은 값을 디버그 섹션 표 한 곳에 낸다 — 게임 HUD · 씬 뷰 HUD · 에디터 Gameplay Debugger 창 · `hud.print sequence` 가 같은 본문을 부른다
 *          (언리얼 Gameplay Debugger 카테고리 자리). 읽기만 하고, 꺼져 있으면 불리지 않는다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceDiagnostics.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

namespace sw
{
    namespace
    {
        struct SequenceDebugSectionsInternal
        {
            /** @brief HUD 섹션 순서입니다(게임플레이 섹션 200..285 뒤). */
            static constexpr int32 kSectionOrder = 290;

            static void writeSequence( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const       pManager = GameplayDebugTarget::findActiveObjectManager();
                const SequencePlayerComponent* const pPlayer  = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<SequencePlayerComponent>( writer.getSceneWalkKey(), *pManager ) : nullptr;
                if ( pPlayer == nullptr || pPlayer->getOwner() == nullptr )
                {
                    writer.addLine( "sequence", "none (no sequence player in the active scene)" );
                    return;
                }
                const SequenceAsset& asset = pPlayer->getAsset();
                writer.addLine( "target", string( pPlayer->getOwner()->getName().c_str() ) );
                writer.addLine( "state", pPlayer->isPlaying() ? "playing" : "stopped" );
                fixed_string<constant::buffer::kMax64> frameText;
                formatstring( frameText.data(), frameText.capacity(), "%# (%#..%#)", pPlayer->getCurrentFrame(), asset._frameMin, asset._frameMax );
                writer.addLine( "frame", frameText.c_str() );

                uint32                           lineCount = 3;
                vector<const SequenceTrackItem*> listActive;
                asset.collectActiveItems( pPlayer->getCurrentFrame(), listActive );
                for ( const SequenceTrackItem* pItem : listActive )
                {
                    if ( ++lineCount > GameplayDebugTarget::kMaxLines )
                        break;
                    writer.addLine( "item", string( pItem->_name.c_str() ) );
                }
                uint32 unboundCount = 0;
                for ( const SequenceBinding& binding : asset._listBinding )
                {
                    if ( pPlayer->findBoundObject( binding._id ) == nullptr && asset.findSpawnItem( binding._id ) == nullptr )
                        ++unboundCount;
                }
                writer.addLine( "bindings", to_string( static_cast<uint64>( asset._listBinding.size() ) ) + ( unboundCount > 0 ? ", " + to_string( unboundCount ) + " unbound" : string{} ),
                                unboundCount > 0 ? DebugHUDLineStyle::Warning : DebugHUDLineStyle::Neutral );

                // 키트의 해석 — 그 키트 모듈이 올라와 있을 때만 제공자가 목록에 있다(창과 같은 등록표).
                vector<const SequenceDiagnosticRegistration*> listProvider;
                SequenceDiagnosticRegistry::collectProviders( listProvider );
                string text;
                for ( const SequenceDiagnosticRegistration* pProvider : listProvider )
                {
                    text.clear();
                    if ( pProvider->_pDescribe( *pPlayer, text ) )
                        writer.addLine( pProvider->_pName, text );
                }
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( sequence, "sequence", "Cutscene (playhead, items, bindings)", SequenceDebugSectionsInternal::kSectionOrder, DebugHUDSectionFlag::kNone, &SequenceDebugSectionsInternal::writeSequence );
} // namespace sw
