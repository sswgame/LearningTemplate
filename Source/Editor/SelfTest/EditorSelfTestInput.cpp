#include "pch.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Hash/HashUtil.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestInputInternal
        {
            enum class EventKind : uint8
            {
                MousePos,
                MouseButton,
                Text,
                Key,
                MouseWheel, ///< 세로 휠 눈금(`_position._y`)
                FrameBreak, ///< 여기까지를 이번 프레임에 넣고 나머지는 다음 프레임에 넣는다
            };

            struct PendingEvent
            {
                string    _text;
                float2    _position{};
                int32     _code{ 0 }; ///< 버튼 번호 · ImGuiKey, 마우스 위치면 뷰포트 id(0 = 알리지 않음)
                EventKind _kind{ EventKind::MousePos };
                uint8     _bDown{ SW_FALSE };
            };

            struct NamedMark
            {
                string             _key;
                EditorSelfTestMark _mark;
            };

            /** @brief 보이는 창 하나의 자리 해시입니다(위치 · 크기 · 도크 노드 · 뷰포트 · 접힘). */
            struct WindowPlace
            {
                uint32 _windowID{ 0 };
                uint64 _hash{ 0 };
            };

            /** @brief 키 · 글 입력을 받을 곳입니다(`EditorSelfTestInput::isKeyboardTargetSettled`). */
            struct KeyboardTarget
            {
                float2 _pos{};
                float2 _size{};
                uint32 _windowID{ 0 };
                uint32 _activeID{ 0 };
                uint32 _navID{ 0 };
                uint32 _popupCount{ 0 };

                bool operator==( const KeyboardTarget& other ) const
                {
                    return _pos == other._pos && _size == other._size && _windowID == other._windowID && _activeID == other._activeID && _navID == other._navID &&
                           _popupCount == other._popupCount;
                }
            };

            /** @brief 한 프레임의 포인터 자리와 그 점의 최상위 창입니다(`evaluateMarkHit` 의 재료). 그 프레임이 끝난 뒤, 다음 입력을 넣기 전에 뜬다. */
            struct HitSample
            {
                float2 _mousePos{};
                uint32 _hoveredWindowID{ 0 };   ///< 0 이면 그 점에서 입력을 받는 창이 없다(모달이 막는다)
                uint32 _frame{ 0 };             ///< 표본이 그린 ImGui 프레임
                uint8  _bMouseDown{ SW_FALSE }; ///< 버튼이 눌린 채였다 — ImGui 는 누른 동안 호버 창을 누른 곳에 묶으므로 판정하지 않는다
                uint8  _bValid{ SW_FALSE };
            };

            /** @brief 테스트가 돈다(레이블을 적는다)는 표시와 큐 · 레이블입니다. */
            struct State
            {
                vector<PendingEvent> _listEvent;
                vector<NamedMark>    _listMark;
                KeyboardTarget       _keyboardTarget{};
                HitSample            _hitSample{};
                uint32               _keyboardTargetFrame{ 0 };       ///< `_keyboardTarget` 을 뜬 ImGui 프레임
                uint32               _keyboardTargetStableStart{ 0 }; ///< 지금 표본과 같은 값이 이어진 첫 프레임
                uint64               _layoutSignature{ 0 };           ///< 바로 앞 프레임이 끝난 뒤의 창 배치 서명(`sampleLayoutSignature`)
                uint32               _layoutFrame{ 0 };               ///< `_layoutSignature` 를 뜬 ImGui 프레임(그 서명은 이 바로 앞 프레임의 것이다)
                uint32               _layoutTag{ 0 };                 ///< `_layoutSignature` 가 그린 프레임이 열릴 때 찍힌 기다림 번호
                uint32               _stablePairTag{ 0 };             ///< 지금 서명이 바로 앞 표본과 같으면 그 앞 표본의 번호, 다르면 0(`isLayoutSettledAfter`)
                uint32               _settleWaitSerial{ 0 };          ///< 마지막 `beginSettleWait` 의 번호
                uint32               _openFrameTag{ 0 };              ///< 지금 열린 프레임에 찍은 기다림 번호
                vector<WindowPlace>  _listWindowPlace;                ///< 바로 앞 표본의 창마다 자리 해시(`_layoutSignature` 의 재료) — 바뀐 창을 이름으로 알린다
                string               _lastLayoutChange;               ///< 서명이 마지막으로 바뀐 까닭(창 이름 · 메인 뷰포트 · 팝업, `describeLastLayoutChange`)
                string               _scrollIntoViewKey;              ///< 다음에 기록할 때 잘려 있으면 그린 창을 스크롤해 보이게 할 레이블(`requestScrollIntoView`)
                float2               _heldMousePosition{};
                uint32               _heldMouseViewportID{ 0 };
                float2               _aimPoint{}; ///< 마지막으로 `Reachable` 로 판정한 점(`moveByOffsetFromAim` 의 기준)
                uint32               _aimViewportID{ 0 };
                bool                 _bAimed{ false };      ///< `_aimPoint` 가 있다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                bool                 _bHoldMouse{ false };  ///< 누른 채 두는 동안 프레임마다 커서를 이 위치로 다시 넣는다(플랫폼이 실제 커서를 넣어 호버가 풀리지 않게) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                bool                 _bEnabled{ SW_FALSE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

                /** @brief 닿는다고 판정한 점을 끌기 기준(`EditorSelfTestInput::moveByOffsetFromAim`)으로 남깁니다. */
                void rememberAim( const float2& point, uint32 viewportID )
                {
                    _aimPoint      = point;
                    _aimViewportID = viewportID;
                    _bAimed        = true;
                }
            };

            /**
             * @brief 지금 에디터 범위의 입력 상태입니다. 에디터 컨텍스트가 없으면 nullptr 입니다.
             * @details 상태의 주인은 `EditorStateStore` 다 — 컨텍스트가 `initialize` 에서 맨 먼저 만드는 내장 서브시스템이라 컨텍스트가 있는 순간부터 있다.
             *          시나리오는 첫 에디터 프레임 앞(런타임 `prepare`)에 `setEnabled( true )` 를 부른다. `SW_EDITOR_SUBSYSTEM` 서브시스템은 첫 프레임 앞의
             *          `syncExtensionRegistrations` 에서야 만들어져 그때 켜는 일이 조용히 사라졌고, 위젯 레이블이 한 번도 기록되지 않았다.
             */
            static State* findState()
            {
                EditorStateStore* pStore = editor::findSubsystem<EditorStateStore>();
                return pStore != nullptr ? &pStore->get<State>() : nullptr;
            }

            static void push( PendingEvent&& event )
            {
                State* pState = findState();
                if ( pState != nullptr )
                    pState->_listEvent.push_back( std::move( event ) );
            }

            /** @brief 레이블을 기록하는 중이면 상태를, 아니면(꺼짐 · 컨텍스트 없음 · 키 없음) nullptr 를 반환합니다. */
            static State* findRecordingState( const utf8* pKey )
            {
                State* pState = findState();
                return pState != nullptr && pState->_bEnabled && pKey != nullptr ? pState : nullptr;
            }

            /**
             * @brief 클릭이 닿는 점(사각형 가운데)이 그린 창의 보이는 영역 밖이면 true 입니다.
             * @details `IsItemVisible` 은 한 픽셀만 보여도 참이라, 아래 가장자리에 걸친 단추의 가운데가 잘린 곳에 놓여 클릭이 아무 데도 닿지 않았다.
             */
            static bool isClickPointClipped( const EditorSelfTestMark& mark )
            {
                const ImGuiWindow* const pWindow = ImGui::GetCurrentWindow();
                const ImVec2             center{ ( mark._min._x + mark._max._x ) * 0.5f, ( mark._min._y + mark._max._y ) * 0.5f };
                // 메뉴 막대 층(주 메뉴 막대 · 상태줄 항목)은 창 안쪽 영역(`InnerClipRect` — 메뉴 막대 아래) 밖에 그린다 — 그 층은 메뉴 막대 사각형으로 본다.
                // 예전에는 상태줄의 Cancel 이 늘 '잘림' 으로 보여 lightprobebakecancel 이 졌다.
                if ( pWindow->DC.NavLayerCurrent == ImGuiNavLayer_Menu )
                    return pWindow->MenuBarRect().Contains( center ) == false;
                if ( pWindow->InnerClipRect.Contains( center ) ) // 표 칸의 ClipRect 가 아니라 창 안쪽 영역 — 모든 칸에 걸친 줄도 맞게 본다
                    return false;
                // 막 열린 하위 메뉴(`BeginMenu` 가 true)는 현재 창을 하위 메뉴로 바꾸지만 마지막 항목은 부모 창의 메뉴 머리다 — 부모 창 안이면 보인다.
                // 예전에는 열린 메뉴 머리가 늘 '잘림' 으로 보여 meshmodifiers 의 범주 클릭이 졌다.
                const bool bOpenedMenuHeader = ( pWindow->Flags & ImGuiWindowFlags_ChildMenu ) != 0 && pWindow->ParentWindow != nullptr;
                return bOpenedMenuHeader == false || pWindow->ParentWindow->InnerClipRect.Contains( center ) == false;
            }

            /** @brief 바로 앞에 그린 위젯(ImGui "last item")의 레이블입니다. */
            static EditorSelfTestMark makeLastItemMark()
            {
                EditorSelfTestMark mark;
                mark._itemID     = ImGui::GetItemID();
                mark._windowID   = ImGui::GetCurrentWindow()->ID;
                mark._viewportID = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
                mark._min        = float2{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y };
                mark._max        = float2{ ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y };
                mark._frame      = static_cast<uint32>( ImGui::GetFrameCount() );
                mark._bClipped   = isClickPointClipped( mark );
                return mark;
            }

            static bool isSamePlace( const EditorSelfTestMark& lhs, const EditorSelfTestMark& rhs )
            {
                return lhs._min == rhs._min && lhs._max == rhs._max && lhs._viewportID == rhs._viewportID && lhs._bClipped == rhs._bClipped;
            }

            /**
             * @brief @p mark 를 @p pKey 로 남깁니다. 앞 기록이 바로 앞(또는 같은) 프레임의 같은 자리면 자리 잡은 첫 프레임을 이어받고, 아니면 이 프레임부터 다시 센다.
             * @details 같은 프레임에 같은 키를 다시 남기면(목록이 같은 레이블을 둘 그린다) 마지막 것이 이긴다 — 자리가 다르면 자리 잡지 않은 것으로 센다.
             */
            static void record( State& state, const utf8* pKey, EditorSelfTestMark mark )
            {
                // 목록이 항목마다 같은 이름을 적으면 그 프레임의 첫 항목이 남는다. 뒤 항목이 덮으면 표식이 프레임 안에서 자리를 옮겨
                // 자리 잡기가 영영 시작되지 않고(isSamePlace 가 늘 거짓) 스크롤도 마지막 항목을 쫓았다(editor.catalogcurvegraph 의 곡선 미리보기).
                for ( const NamedMark& named : state._listMark )
                {
                    if ( named._key == pKey && named._mark._frame == mark._frame )
                        return;
                }
                // 입력 단계가 기다리는 레이블이 잘려 있으면 그린 창(지금 창)을 스크롤한다 — 사람처럼 보이게 한 뒤 누른다. 다음 프레임에 자리가 바뀌어 자리 잡기를 다시 센다.
                if ( mark._bClipped && state._scrollIntoViewKey == pKey )
                {
                    ImGui::ScrollToBringRectIntoView( ImGui::GetCurrentWindow(), ImRect( mark._min._x, mark._min._y, mark._max._x, mark._max._y ) );
                    state._scrollIntoViewKey.clear();
                }
                mark._stableStartFrame = mark._frame;
                for ( NamedMark& named : state._listMark )
                {
                    if ( named._key != pKey )
                        continue;
                    const bool bContinued = named._mark._frame + 1 >= mark._frame && isSamePlace( named._mark, mark );
                    if ( bContinued )
                        mark._stableStartFrame = named._mark._stableStartFrame;
                    named._mark = mark;
                    return;
                }
                state._listMark.push_back( NamedMark{ string( pKey ), mark } );
            }

            /** @brief 키 · 글 입력을 받을 곳의 표본입니다. 둘이 같으면 그 사이 초점이 움직이지 않았다. */
            static KeyboardTarget sampleKeyboardTarget()
            {
                const ImGuiContext& context = *ImGui::GetCurrentContext();
                KeyboardTarget      target;
                if ( context.NavWindow != nullptr )
                {
                    target._windowID = context.NavWindow->ID;
                    target._pos      = float2{ context.NavWindow->Pos.x, context.NavWindow->Pos.y };
                    target._size     = float2{ context.NavWindow->Size.x, context.NavWindow->Size.y };
                }
                target._activeID   = context.ActiveId;
                target._navID      = context.NavId;
                target._popupCount = static_cast<uint32>( context.OpenPopupStack.Size );
                return target;
            }

            /**
             * @brief 보이는 창의 배치 서명입니다. 둘이 같으면 그 사이 어느 창도 움직이거나 크기가 바뀌거나 도킹이 바뀌지 않았다.
             * @details 창마다의 자리 해시를 @p outListPlace 에 남긴다 — 서명이 바뀌면 앞 표본과 견주어 어느 창이 움직였는지 알린다(자리 잡기 시한 초과의 까닭).
             */
            static uint64 sampleLayoutSignature( vector<WindowPlace>& outListPlace )
            {
                const ImGuiContext& context   = *ImGui::GetCurrentContext();
                const ImVec2        mainSize  = ImGui::GetMainViewport()->Size;
                uint64              signature = HashUtil::makeHash( mainSize.x, mainSize.y, context.OpenPopupStack.Size );
                outListPlace.clear();
                for ( const ImGuiWindow* pWindow : context.Windows )
                {
                    if ( pWindow->WasActive == false || pWindow->Hidden )
                        continue;
                    const uint64 place = HashUtil::makeHash( pWindow->ID, pWindow->Pos.x, pWindow->Pos.y, pWindow->Size.x, pWindow->Size.y, pWindow->DockId, pWindow->ViewportId,
                                                             pWindow->Collapsed );
                    outListPlace.push_back( WindowPlace{ pWindow->ID, place } );
                    signature = HashUtil::combine( signature, place );
                }
                return signature;
            }

            /** @brief 앞 표본(@p listBefore)에서 지금(@p listAfter)으로 바뀐 첫 창의 이름입니다. 창이 그대로면 메인 뷰포트 크기 · 팝업 수가 바뀐 것이다. */
            static string describeLayoutChange( const vector<WindowPlace>& listBefore, const vector<WindowPlace>& listAfter )
            {
                for ( const WindowPlace& after : listAfter )
                {
                    const WindowPlace* pBefore = VectorUtil::findBy<&WindowPlace::_windowID>( listBefore, after._windowID );
                    if ( pBefore != nullptr && pBefore->_hash == after._hash )
                        continue;
                    const ImGuiWindow* pWindow = ImGui::FindWindowByID( after._windowID );
                    return string{ pWindow != nullptr ? pWindow->Name : "?" } + ( pBefore == nullptr ? " appeared" : " moved or resized" );
                }
                for ( const WindowPlace& before : listBefore )
                {
                    if ( VectorUtil::findBy<&WindowPlace::_windowID>( listAfter, before._windowID ) != nullptr )
                        continue;
                    const ImGuiWindow* pWindow = ImGui::FindWindowByID( before._windowID );
                    return string{ pWindow != nullptr ? pWindow->Name : "?" } + " disappeared";
                }
                return "the main viewport size or the open popup count changed";
            }

            /**
             * @brief @p pHit(그 점의 최상위 창)가 @p mark 의 주인이면 true 입니다 — 레이블을 그린 창, 그 조상(제목 표시줄 · 도크 탭 · 열린 메뉴 머리), 레이블 사각형 안에 든 자식 창.
             * @details 자식 창 · 팝업 · 도킹된 창은 `ParentWindow` 로 부모를 잇는다. 레이블을 그린 창의 자손인데 레이블 안에 들지 않은 창(같은 창 위의 오버레이 툴바, 그 창이 연 팝업)은 덮은 것이다.
             */
            static bool isMarkOwner( const ImGuiWindow& hit, const ImGuiWindow& markWindow, const EditorSelfTestMark& mark )
            {
                for ( const ImGuiWindow* pAncestor = &markWindow; pAncestor != nullptr; pAncestor = pAncestor->ParentWindow )
                {
                    if ( pAncestor == &hit )
                        return true;
                }
                constexpr float32 kTolerance = 1.0f; ///< 픽셀. 자식 창 사각형과 그 항목 사각형이 반올림으로 어긋난다
                const ImRect      markRect( mark._min._x - kTolerance, mark._min._y - kTolerance, mark._max._x + kTolerance, mark._max._y + kTolerance );
                bool              bInsideMark = false;
                for ( const ImGuiWindow* pWindow = &hit; pWindow != nullptr; pWindow = pWindow->ParentWindow )
                {
                    if ( pWindow == &markWindow )
                        return bInsideMark;
                    bInsideMark = bInsideMark || markRect.Contains( pWindow->Rect() );
                }
                return false;
            }

            /** @brief 테스트가 끝난 포인터를 두는 화면 밖 자리입니다(`parkMouse`). */
            static constexpr float32 kParkedMouseCoordinate = -10000.0f;

            /** @brief 레이블의 @p anchor 위치(0..1)입니다. */
            static float2 computeAimPoint( const EditorSelfTestMark& mark, const float2& anchor )
            {
                return float2{ mark._min._x + ( mark._max._x - mark._min._x ) * anchor._x, mark._min._y + ( mark._max._y - mark._min._y ) * anchor._y };
            }

            /** @brief 두 점이 같은 픽셀이면 true 입니다. ImGui 는 마우스 좌표를 정수로 내린다. */
            static bool isSamePixel( const float2& lhs, const float2& rhs )
            {
                constexpr float32 kAimTolerance = 1.0f;
                return fabsf( lhs._x - rhs._x ) <= kAimTolerance && fabsf( lhs._y - rhs._y ) <= kAimTolerance;
            }

            /**
             * @brief 포인터가 @p mark 의 @p anchor 자리에 있던 프레임에 그 점의 최상위 창이 레이블 주인인지 판정합니다(`EditorSelfTestInput::aimAtMark` 의 판정).
             * @details 지금 포인터(이번 프레임)와 표본(바로 앞 프레임이 끝난 뒤) 둘 다 그 점이어야 판정한다 — 표본의 창은 그 포인터로 정한 것이다.
             */
            static EditorMarkHit evaluateMarkHit( const State& state, const EditorSelfTestMark& mark, const float2& anchor, string& outCoverName )
            {
                const HitSample& sample = state._hitSample;
                const float2     target = computeAimPoint( mark, anchor );
                const ImVec2     mouse  = ImGui::GetIO().MousePos;
                const bool       bFresh = sample._bValid != SW_FALSE && sample._frame + 1 >= static_cast<uint32>( ImGui::GetFrameCount() );
                const bool       bAimed = isSamePixel( float2{ mouse.x, mouse.y }, target ) && isSamePixel( sample._mousePos, target );
                if ( bFresh == false || bAimed == false )
                    return EditorMarkHit::Pending;
                if ( sample._bMouseDown != SW_FALSE )
                    return EditorMarkHit::Reachable; // 누른 동안은 ImGui 가 호버 창을 누른 곳에 묶는다 — 이 점의 창을 말하지 않는다(누른 채 다른 버튼을 누르는 단계)
                const ImGuiWindow* pMarkWindow = ImGui::FindWindowByID( mark._windowID );
                const ImGuiWindow* pHit        = sample._hoveredWindowID != 0 ? ImGui::FindWindowByID( sample._hoveredWindowID ) : nullptr;
                if ( pMarkWindow == nullptr )
                    return EditorMarkHit::Pending; // 레이블을 그린 창이 없어졌다 — 다음 기록을 기다린다
                if ( pHit == nullptr )
                {
                    outCoverName = "(no window takes input there - a modal popup blocks it)";
                    return EditorMarkHit::Covered;
                }
                if ( isMarkOwner( *pHit, *pMarkWindow, mark ) )
                    return EditorMarkHit::Reachable;
                outCoverName = pHit->Name != nullptr ? pHit->Name : "?";
                return EditorMarkHit::Covered;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorSelfTestMarks::record( const utf8* pKey )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findRecordingState( pKey );
        if ( pState != nullptr )
            EditorSelfTestInputInternal::record( *pState, pKey, EditorSelfTestInputInternal::makeLastItemMark() );
    }

    void EditorSelfTestMarks::recordInputFrame( const utf8* pKey )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findRecordingState( pKey );
        if ( pState == nullptr )
            return;
        // 레이블 글까지 한 위젯으로 잡힌 사각형에서 입력 칸만 남긴다. 레이블이 창 밖으로 길게 나가도 가운데가 칸 위에 있게 한다.
        EditorSelfTestMark mark       = EditorSelfTestInputInternal::makeLastItemMark();
        const float32      frameRight = mark._min._x + ImGui::CalcItemWidth();
        if ( frameRight < mark._max._x )
            mark._max._x = frameRight;
        EditorSelfTestInputInternal::record( *pState, pKey, mark );
    }

    void EditorSelfTestMarks::recordRect( const utf8* pKey, const float2& min, const float2& max )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findRecordingState( pKey );
        if ( pState == nullptr )
            return;
        EditorSelfTestMark mark;
        mark._windowID   = ImGui::GetCurrentWindow()->ID;
        mark._viewportID = ImGui::GetWindowViewport() != nullptr ? ImGui::GetWindowViewport()->ID : 0u;
        mark._min        = min;
        mark._max        = max;
        mark._frame      = static_cast<uint32>( ImGui::GetFrameCount() );
        EditorSelfTestInputInternal::record( *pState, pKey, mark );
    }

    void EditorSelfTestMarks::requestScrollIntoView( string_view key )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState != nullptr )
            pState->_scrollIntoViewKey = string( key );
    }

    bool EditorSelfTestMarks::find( string_view key, EditorSelfTestMark& outMark )
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false;
        const uint32 frame = static_cast<uint32>( ImGui::GetFrameCount() );
        for ( const EditorSelfTestInputInternal::NamedMark& named : pState->_listMark )
        {
            if ( named._key == key && named._mark._frame + 1 >= frame )
            {
                outMark = named._mark;
                return true;
            }
        }
        return false;
    }

    bool EditorSelfTestMarks::setEnabled( bool bEnabled )
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false; // 에디터 컨텍스트가 없다 — 기록할 곳이 없으니 켠 척하지 않고 알린다
        pState->_bEnabled = bEnabled;
        if ( bEnabled == false )
        {
            pState->_listMark.clear();
            pState->_listEvent.clear();
            pState->_bHoldMouse = false;
            pState->_bAimed     = false;
        }
        return true;
    }

    bool EditorSelfTestMarks::isEnabled()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        return pState != nullptr && pState->_bEnabled;
    }

    void EditorSelfTestInput::moveMouse( const float2& position, uint32 viewportID )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind     = EditorSelfTestInputInternal::EventKind::MousePos;
        event._position = position;
        event._code     = static_cast<int32>( viewportID );
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::setMouseButton( int32 button, bool bDown )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind  = EditorSelfTestInputInternal::EventKind::MouseButton;
        event._code  = button;
        event._bDown = bDown ? SW_TRUE : SW_FALSE;
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::typeText( string_view utf8Text )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind = EditorSelfTestInputInternal::EventKind::Text;
        event._text = string( utf8Text );
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::scrollMouseWheel( float32 notches )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind     = EditorSelfTestInputInternal::EventKind::MouseWheel;
        event._position = float2{ 0.0f, notches };
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::waitNextFrame()
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind = EditorSelfTestInputInternal::EventKind::FrameBreak;
        EditorSelfTestInputInternal::push( std::move( event ) );
    }

    void EditorSelfTestInput::setKey( int32 imguiKey, bool bDown )
    {
        EditorSelfTestInputInternal::PendingEvent event;
        event._kind  = EditorSelfTestInputInternal::EventKind::Key;
        event._code  = imguiKey;
        event._bDown = bDown ? SW_TRUE : SW_FALSE;
        EditorSelfTestInputInternal::push( std::move( event ) );
        // 실제 플랫폼 백엔드와 같이 수정 키는 두 이벤트다 — 왼쪽 키(에디터 입력 맵이 읽는다)와 ImGui 의 수정자 비트(`io.KeyCtrl`, 서드파티 노드 에디터가 읽는다).
        int32 modifierBit = 0;
        if ( imguiKey == ImGuiKey_LeftCtrl )
            modifierBit = ImGuiMod_Ctrl;
        else if ( imguiKey == ImGuiKey_LeftShift )
            modifierBit = ImGuiMod_Shift;
        else if ( imguiKey == ImGuiKey_LeftAlt )
            modifierBit = ImGuiMod_Alt;
        if ( modifierBit != 0 )
        {
            EditorSelfTestInputInternal::PendingEvent modifierEvent;
            modifierEvent._kind  = EditorSelfTestInputInternal::EventKind::Key;
            modifierEvent._code  = modifierBit;
            modifierEvent._bDown = bDown ? SW_TRUE : SW_FALSE;
            EditorSelfTestInputInternal::push( std::move( modifierEvent ) );
        }
    }

    bool EditorSelfTestInput::findKeyByName( string_view name, int32& outImguiKey )
    {
        struct ModifierName
        {
            const utf8* _pName;
            int32       _key;
        };
        constexpr ModifierName kArrModifier[] = {
            { "Ctrl",  ImGuiKey_LeftCtrl},
            {"Shift", ImGuiKey_LeftShift},
            {  "Alt",   ImGuiKey_LeftAlt},
        };
        for ( const ModifierName& modifier : kArrModifier )
        {
            if ( name == modifier._pName )
            {
                outImguiKey = modifier._key;
                return true;
            }
        }
        for ( int32 key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key )
        {
            if ( name == ImGui::GetKeyName( static_cast<ImGuiKey>( key ) ) )
            {
                outImguiKey = key;
                return true;
            }
        }
        return false;
    }

    EditorMarkHit EditorSelfTestInput::aimAtMark( string_view key, const float2& anchor, string& outCoverName )
    {
        using Internal = EditorSelfTestInputInternal;
        EditorSelfTestMark mark;
        Internal::State*   pState = Internal::findState();
        if ( pState == nullptr || EditorSelfTestMarks::find( key, mark ) == false )
            return EditorMarkHit::Missing;
        pState->_heldMousePosition   = Internal::computeAimPoint( mark, anchor );
        pState->_heldMouseViewportID = mark._viewportID;
        pState->_bHoldMouse          = true;

        const EditorMarkHit hit = Internal::evaluateMarkHit( *pState, mark, anchor, outCoverName );
        if ( hit == EditorMarkHit::Reachable )
            pState->rememberAim( Internal::computeAimPoint( mark, anchor ), mark._viewportID );
        return hit;
    }

    EditorMarkHit EditorSelfTestInput::pressAtMark( string_view key, const float2& anchor, int32 button, string& outCoverName )
    {
        using Internal = EditorSelfTestInputInternal;
        EditorSelfTestMark mark;
        Internal::State*   pState = Internal::findState();
        if ( pState == nullptr || EditorSelfTestMarks::find( key, mark ) == false )
            return EditorMarkHit::Missing;
        const EditorMarkHit hit = Internal::evaluateMarkHit( *pState, mark, anchor, outCoverName );
        if ( hit != EditorMarkHit::Reachable )
            return hit;
        pState->rememberAim( Internal::computeAimPoint( mark, anchor ), mark._viewportID );
        moveMouse( pState->_aimPoint, pState->_aimViewportID );
        setMouseButton( button, true );
        return hit;
    }

    bool EditorSelfTestInput::releaseAtMark( string_view key, const float2& anchor, int32 button )
    {
        EditorSelfTestMark mark;
        const bool         bDrawn = EditorSelfTestMarks::find( key, mark );
        if ( bDrawn )
            moveMouse( EditorSelfTestInputInternal::computeAimPoint( mark, anchor ), mark._viewportID );
        setMouseButton( button, false );
        return bDrawn;
    }

    bool EditorSelfTestInput::moveByOffsetFromAim( const float2& offset )
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr || pState->_bAimed == false )
            return false;
        moveMouse( float2{ pState->_aimPoint._x + offset._x, pState->_aimPoint._y + offset._y }, pState->_aimViewportID );
        return true;
    }

    void EditorSelfTestInput::parkMouse()
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState != nullptr )
            pState->_bAimed = false;
        releaseMouseHold();
        moveMouse( float2{ EditorSelfTestInputInternal::kParkedMouseCoordinate, EditorSelfTestInputInternal::kParkedMouseCoordinate } );
    }

    bool EditorSelfTestInput::hasPendingEvents()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        return pState != nullptr && pState->_listEvent.empty() == false;
    }

    bool EditorSelfTestInput::isKeyboardTargetSettled()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return false;
        // 표본이 지난 프레임 것이어야 한다 — 오래된 표본(기록을 다시 켰다)은 지금 초점을 말하지 않는다.
        const bool bFresh = pState->_keyboardTargetFrame + 1 >= static_cast<uint32>( ImGui::GetFrameCount() );
        return bFresh && pState->_keyboardTargetFrame > pState->_keyboardTargetStableStart;
    }

    uint32 EditorSelfTestInput::beginSettleWait()
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return 0;
        return ++pState->_settleWaitSerial;
    }

    uint32 EditorSelfTestInput::getSettleWaitSerial()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        return pState != nullptr ? pState->_settleWaitSerial : 0;
    }

    bool EditorSelfTestInput::isLayoutSettledAfter( uint32 waitSerial )
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        // 같은 두 표본 중 앞 것의 번호가 기다림 번호 이상이면 뒤 것도 그렇다(번호는 줄지 않는다).
        return pState != nullptr && waitSerial != 0 && pState->_stablePairTag >= waitSerial;
    }

    string EditorSelfTestInput::describeLastLayoutChange()
    {
        const EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        return pState != nullptr ? pState->_lastLayoutChange : string{};
    }

    void EditorSelfTestInput::releaseMouseHold()
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState != nullptr )
            pState->_bHoldMouse = false;
    }

    void EditorSelfTestInput::flushIntoImGui()
    {
        EditorSelfTestInputInternal::State* pState = EditorSelfTestInputInternal::findState();
        if ( pState == nullptr )
            return;
        EditorSelfTestInputInternal::State&                state     = *pState;
        vector<EditorSelfTestInputInternal::PendingEvent>& listEvent = state._listEvent;
        if ( state._bEnabled )
        {
            // 앞 프레임이 끝난 뒤의 초점이다(이번 프레임의 입력을 넣기 전). 바로 앞 프레임과 같으면 자리 잡은 첫 프레임을 이어받는다.
            const EditorSelfTestInputInternal::KeyboardTarget target     = EditorSelfTestInputInternal::sampleKeyboardTarget();
            const uint32                                      frame      = static_cast<uint32>( ImGui::GetFrameCount() );
            const bool                                        bContinued = state._keyboardTargetFrame + 1 >= frame && state._keyboardTarget == target;
            if ( bContinued == false )
                state._keyboardTargetStableStart = frame;
            state._keyboardTarget      = target;
            state._keyboardTargetFrame = frame;

            // 바로 앞 프레임의 포인터 자리와 그 점의 최상위 창이다(그 프레임의 NewFrame 이 같은 포인터로 정했다) — 이번 입력을 넣기 전에 뜬다.
            const ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
            const ImGuiIO&      imguiIO      = ImGui::GetIO();
            bool                bMouseDown   = false;
            for ( const bool bDown : imguiIO.MouseDown )
            {
                bMouseDown = bMouseDown || bDown;
            }
            state._hitSample = EditorSelfTestInputInternal::HitSample{
                float2{ imguiIO.MousePos.x, imguiIO.MousePos.y },
                imguiContext.HoveredWindow != nullptr ? imguiContext.HoveredWindow->ID : 0u, frame,
                bMouseDown ? SW_TRUE : SW_FALSE, ImGui::IsMousePosValid() ? SW_TRUE : SW_FALSE
            };

            // 바로 앞 프레임의 배치다 — 그 프레임이 열릴 때 찍은 번호를 붙인다. 그다음 이번에 여는 프레임에 지금 번호를 찍는다.
            vector<EditorSelfTestInputInternal::WindowPlace> listPlace;
            const uint64                                     signature     = EditorSelfTestInputInternal::sampleLayoutSignature( listPlace );
            const bool                                       bLayoutStayed = state._layoutFrame + 1 >= frame && state._layoutSignature == signature;
            if ( state._layoutSignature != signature )
                state._lastLayoutChange = EditorSelfTestInputInternal::describeLayoutChange( state._listWindowPlace, listPlace );
            state._listWindowPlace = std::move( listPlace );
            state._stablePairTag   = bLayoutStayed ? state._layoutTag : 0;
            state._layoutSignature = signature;
            state._layoutFrame     = frame;
            state._layoutTag       = state._openFrameTag;
            state._openFrameTag    = state._settleWaitSerial;
        }
        if ( state._bHoldMouse )
        {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent( state._heldMousePosition._x, state._heldMousePosition._y );
            if ( state._heldMouseViewportID != 0 )
                io.AddMouseViewportEvent( static_cast<ImGuiID>( state._heldMouseViewportID ) );
        }
        if ( listEvent.empty() )
            return;
        ImGuiIO& io            = ImGui::GetIO();
        size_t   consumedCount = 0;
        for ( const EditorSelfTestInputInternal::PendingEvent& event : listEvent )
        {
            ++consumedCount;
            if ( event._kind == EditorSelfTestInputInternal::EventKind::FrameBreak )
                break;
            switch ( event._kind )
            {
                case EditorSelfTestInputInternal::EventKind::MousePos:
                {
                    io.AddMousePosEvent( event._position._x, event._position._y );
                    // 떠 있는 창(자기 플랫폼 창)은 플랫폼이 실제 커서로 고른 뷰포트로 호버가 갈린다 — 위젯의 뷰포트를 뒤에 넣어 이긴다.
                    if ( event._code != 0 )
                        io.AddMouseViewportEvent( static_cast<ImGuiID>( event._code ) );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::MouseButton:
                {
                    io.AddMouseButtonEvent( event._code, event._bDown != SW_FALSE );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::Text:
                {
                    io.AddInputCharactersUTF8( event._text.c_str() );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::Key:
                {
                    io.AddKeyEvent( static_cast<ImGuiKey>( event._code ), event._bDown != SW_FALSE );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::MouseWheel:
                {
                    io.AddMouseWheelEvent( 0.0f, event._position._y );
                    break;
                }
                case EditorSelfTestInputInternal::EventKind::FrameBreak:
                {
                    break;
                }
            }
        }
        listEvent.erase( listEvent.begin(), listEvent.begin() + static_cast<ptrdiff_t>( consumedCount ) );
    }
} // namespace sw::editor
