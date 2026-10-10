#include "pch.h"

#include "Engine/UI/Base/UiNavigationSolver.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct UiNavigationSolverInternal
        {
            /** @brief 공간 점수에서 "그 방향으로 나아갔다" 고 보는 최소 변 이동입니다(UI 단위 — 같은 열의 1 px 어긋남은 옆으로 보지 않는다). */
            static constexpr float32 kMinimumEdgeAdvance = 1.0f;

            /** @brief @p widget 아래(자기 포함)의 받을 수 있는 위젯을 문서 순서로 모읍니다. 안 보이거나 꺼진 갈래는 내려가지 않습니다. */
            static void collectFocusable( const Widget& widget, vector<const Widget*>& inoutListWidget )
            {
                if ( widget.isVisible() == false || widget.isEnabled() == false )
                    return;
                if ( UiNavigationSolver::canReceiveFocus( widget ) )
                    inoutListWidget.push_back( &widget );
                const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
                if ( pPanel == nullptr )
                    return;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                {
                    collectFocusable( *pPanel->getChild( index ), inoutListWidget );
                }
            }

            /**
             * @brief 자르는 조상 밖으로 완전히 나간 위젯이면 true 입니다.
             * @details 스크롤 패널(`canScrollIntoView`)이 자른 것은 빼지 않는다 — 포커스가 가면 그 패널이 보이게 옮긴다(목록 아래로 내려가기).
             */
            static bool isClippedOut( const Widget& widget )
            {
                const UiRect bounds = widget.getGeometry().computeScreenBounds();
                for ( const PanelWidget* pParent = widget.getParent(); pParent != nullptr; pParent = pParent->getParent() )
                {
                    if ( pParent->clipsChildren() && pParent->canScrollIntoView() == false &&
                         bounds.intersects( pParent->getGeometry().computeScreenBounds() ) == false )
                        return true;
                }
                return false;
            }

            static UiNavigationDirection getOpposite( UiNavigationDirection direction )
            {
                switch ( direction )
                {
                    case UiNavigationDirection::Up:
                        return UiNavigationDirection::Down;
                    case UiNavigationDirection::Down:
                        return UiNavigationDirection::Up;
                    case UiNavigationDirection::Left:
                        return UiNavigationDirection::Right;
                    case UiNavigationDirection::Right:
                        return UiNavigationDirection::Left;
                    case UiNavigationDirection::Next:
                        return UiNavigationDirection::Previous;
                    case UiNavigationDirection::Previous:
                        return UiNavigationDirection::Next;
                }
                return direction;
            }

            /** @brief 방향의 수직축 틈입니다(좌우면 세로 구간, 상하면 가로 구간). */
            static float32 computePerpendicularGap( const UiRect& from, const UiRect& candidate, UiNavigationDirection direction )
            {
                const bool bHorizontal = direction == UiNavigationDirection::Left || direction == UiNavigationDirection::Right;
                return bHorizontal ? UiRect::computeRangeGap( from.getTop(), from.getBottom(), candidate.getTop(), candidate.getBottom() )
                                   : UiRect::computeRangeGap( from.getLeft(), from.getRight(), candidate.getLeft(), candidate.getRight() );
            }

            static WidgetID findTabNeighbor( const vector<const Widget*>& listCandidate, const Widget& from, bool bForward )
            {
                const uint32 count = static_cast<uint32>( listCandidate.size() );
                if ( count == 0 )
                    return kInvalidWidgetID;
                uint32 current = count;
                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( listCandidate[index] == &from )
                    {
                        current = index;
                        break;
                    }
                }
                if ( current == count ) // 범위 밖에서 왔다 — 처음(앞으로) · 끝(뒤로)
                    return bForward ? listCandidate[0]->getID() : listCandidate[count - 1]->getID();
                const uint32 next = bForward ? ( current + 1 ) % count : ( current + count - 1 ) % count;
                return listCandidate[next]->getID();
            }

            static WidgetID findSpatialNeighbor( const vector<const Widget*>& listCandidate, const Widget& from, UiNavigationDirection direction )
            {
                const UiRect  fromRect     = from.getGeometry().computeScreenBounds();
                const float2  fromCenter   = fromRect.getCenter();
                const Widget* pBest        = nullptr;
                float32       bestScore    = 0.0f;
                float32       bestDistance = 0.0f;
                for ( const Widget* pCandidate : listCandidate )
                {
                    if ( pCandidate == &from || isClippedOut( *pCandidate ) )
                        continue;
                    const UiRect  rect  = pCandidate->getGeometry().computeScreenBounds();
                    const float32 score = UiNavigationSolver::computeSpatialScore( fromRect, rect, direction );
                    if ( score < 0.0f )
                        continue;
                    const float32 distance = float2::getDistanceSquared( fromCenter, rect.getCenter() );
                    // 문서 순서로 돌며 "더 작을 때만" 바꾼다 — 같으면 앞의 것이 남는다(결정적).
                    const bool bBetter = pBest == nullptr || score < bestScore || ( score == bestScore && distance < bestDistance );
                    if ( bBetter )
                    {
                        pBest        = pCandidate;
                        bestScore    = score;
                        bestDistance = distance;
                    }
                }
                return pBest != nullptr ? pBest->getID() : kInvalidWidgetID;
            }

            /** @brief Wrap: 반대 방향으로 가장 먼 후보입니다 — 수직 틈이 0 인 후보가 있으면 그 안에서만. */
            static WidgetID findWrapNeighbor( const vector<const Widget*>& listCandidate, const Widget& from, UiNavigationDirection direction )
            {
                const UiNavigationDirection opposite     = getOpposite( direction );
                const UiRect                fromRect     = from.getGeometry().computeScreenBounds();
                const Widget*               pBest        = nullptr;
                float32                     bestPrimary  = 0.0f;
                bool                        bBestAligned = false;
                for ( const Widget* pCandidate : listCandidate )
                {
                    if ( pCandidate == &from || isClippedOut( *pCandidate ) )
                        continue;
                    const UiRect  rect  = pCandidate->getGeometry().computeScreenBounds();
                    const float32 score = UiNavigationSolver::computeSpatialScore( fromRect, rect, opposite );
                    if ( score < 0.0f )
                        continue;
                    const float32 gap      = computePerpendicularGap( fromRect, rect, opposite );
                    const bool    bAligned = gap == 0.0f;
                    const float32 primary  = score - 2.0f * gap;
                    const bool    bBetter  = pBest == nullptr || ( bAligned && bBestAligned == false ) || ( bAligned == bBestAligned && primary > bestPrimary );
                    if ( bBetter )
                    {
                        pBest        = pCandidate;
                        bestPrimary  = primary;
                        bBestAligned = bAligned;
                    }
                }
                return pBest != nullptr ? pBest->getID() : kInvalidWidgetID;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const WidgetNavigationEntry& WidgetNavigation::getEntry( UiNavigationDirection direction ) const
    {
        switch ( direction )
        {
            case UiNavigationDirection::Up:
                return _up;
            case UiNavigationDirection::Down:
                return _down;
            case UiNavigationDirection::Left:
                return _left;
            case UiNavigationDirection::Right:
                return _right;
            case UiNavigationDirection::Next:
                return _next;
            case UiNavigationDirection::Previous:
                return _previous;
        }
        return _up;
    }
} // namespace sw

namespace sw
{
    bool UiNavigationSolver::canReceiveFocus( const Widget& widget )
    {
        if ( widget.supportsFocus() == false )
            return false;
        for ( const Widget* pWidget = &widget; pWidget != nullptr; pWidget = pWidget->getParent() )
        {
            if ( pWidget->isEnabled() == false || pWidget->isVisible() == false )
                return false;
        }
        return true;
    }

    float32 UiNavigationSolver::computeSpatialScore( const UiRect& from, const UiRect& candidate, UiNavigationDirection direction )
    {
        const float2 fromCenter       = from.getCenter();
        const float2 candidateCenter  = candidate.getCenter();
        float32      primary          = 0.0f;
        float32      perpendicularGap = 0.0f;
        switch ( direction )
        {
            case UiNavigationDirection::Right:
            {
                if ( candidateCenter._x <= fromCenter._x || candidate.getLeft() < from.getLeft() + UiNavigationSolverInternal::kMinimumEdgeAdvance )
                    return -1.0f;
                primary          = MathUtil::max( 0.0f, candidate.getLeft() - from.getRight() );
                perpendicularGap = UiRect::computeRangeGap( from.getTop(), from.getBottom(), candidate.getTop(), candidate.getBottom() );
                break;
            }
            case UiNavigationDirection::Left:
            {
                if ( candidateCenter._x >= fromCenter._x || candidate.getRight() > from.getRight() - UiNavigationSolverInternal::kMinimumEdgeAdvance )
                    return -1.0f;
                primary          = MathUtil::max( 0.0f, from.getLeft() - candidate.getRight() );
                perpendicularGap = UiRect::computeRangeGap( from.getTop(), from.getBottom(), candidate.getTop(), candidate.getBottom() );
                break;
            }
            case UiNavigationDirection::Down:
            {
                if ( candidateCenter._y <= fromCenter._y || candidate.getTop() < from.getTop() + UiNavigationSolverInternal::kMinimumEdgeAdvance )
                    return -1.0f;
                primary          = MathUtil::max( 0.0f, candidate.getTop() - from.getBottom() );
                perpendicularGap = UiRect::computeRangeGap( from.getLeft(), from.getRight(), candidate.getLeft(), candidate.getRight() );
                break;
            }
            case UiNavigationDirection::Up:
            {
                if ( candidateCenter._y >= fromCenter._y || candidate.getBottom() > from.getBottom() - UiNavigationSolverInternal::kMinimumEdgeAdvance )
                    return -1.0f;
                primary          = MathUtil::max( 0.0f, from.getTop() - candidate.getBottom() );
                perpendicularGap = UiRect::computeRangeGap( from.getLeft(), from.getRight(), candidate.getLeft(), candidate.getRight() );
                break;
            }
            case UiNavigationDirection::Next:
            case UiNavigationDirection::Previous:
            {
                return -1.0f; // 탭 순서는 공간 점수를 쓰지 않는다
            }
        }
        return primary + 2.0f * perpendicularGap;
    }

    WidgetID UiNavigationSolver::findNextWidget( const WidgetTree& tree, WidgetID from, UiNavigationDirection direction )
    {
        const Widget* pFrom = tree.findWidgetByID( from );
        if ( pFrom == nullptr || tree.getRoot() == nullptr )
            return kInvalidWidgetID;

        // (1) 조상으로 올라가며 규칙을 본다 — 첫 Explicit(받을 수 있으면) · 첫 Stop/Wrap 이 범위.
        const Widget*    pScope = tree.getRoot();
        UiNavigationRule rule   = UiNavigationRule::Escape;
        for ( const Widget* pWidget = pFrom; pWidget != nullptr; pWidget = pWidget->getParent() )
        {
            const WidgetNavigationEntry& entry = pWidget->getNavigation().getEntry( direction );
            if ( entry._rule == UiNavigationRule::Explicit )
            {
                const Widget* pTarget = tree.findWidgetByName( entry._target );
                if ( pTarget != nullptr && pTarget != pFrom && canReceiveFocus( *pTarget ) )
                    return pTarget->getID();
                continue;
            }
            if ( entry._rule == UiNavigationRule::Stop || entry._rule == UiNavigationRule::Wrap )
            {
                pScope = pWidget;
                rule   = entry._rule;
                break;
            }
        }

        vector<const Widget*> listCandidate;
        UiNavigationSolverInternal::collectFocusable( *pScope, listCandidate );
        if ( direction == UiNavigationDirection::Next || direction == UiNavigationDirection::Previous )
            return UiNavigationSolverInternal::findTabNeighbor( listCandidate, *pFrom, direction == UiNavigationDirection::Next );

        // (2) 공간 탐색 → (3) 없고 Wrap 이면 반대쪽 끝.
        const WidgetID spatial = UiNavigationSolverInternal::findSpatialNeighbor( listCandidate, *pFrom, direction );
        if ( spatial != kInvalidWidgetID || rule != UiNavigationRule::Wrap )
            return spatial;
        return UiNavigationSolverInternal::findWrapNeighbor( listCandidate, *pFrom, direction );
    }

    WidgetID UiNavigationSolver::findFirstFocusable( const Widget& scope )
    {
        vector<const Widget*> listCandidate;
        UiNavigationSolverInternal::collectFocusable( scope, listCandidate );
        return listCandidate.empty() ? kInvalidWidgetID : listCandidate[0]->getID();
    }
} // namespace sw
