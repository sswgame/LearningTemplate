#include "pch.h"

#include "GameFramework/Client/Shop/ShopScreen.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ShopScreenInternal
        {
            /** @brief 결과 상태의 보일 글 키입니다(원문은 engine.strings.xml 의 `Shop.*`). */
            static const utf8* getStatusKey( ShopPurchaseStatus status )
            {
                switch ( status )
                {
                    case ShopPurchaseStatus::Succeeded:
                    {
                        return SW_LOCKEYREF( "Shop", "Bought" );
                    }
                    case ShopPurchaseStatus::NotEnoughMoney:
                    {
                        return SW_LOCKEYREF( "Shop", "NotEnoughMoney" );
                    }
                    case ShopPurchaseStatus::OutOfStock:
                    {
                        return SW_LOCKEYREF( "Shop", "OutOfStock" );
                    }
                    case ShopPurchaseStatus::LimitReached:
                    {
                        return SW_LOCKEYREF( "Shop", "LimitReached" );
                    }
                    case ShopPurchaseStatus::NoRoom:
                    {
                        return SW_LOCKEYREF( "Shop", "NoRoom" );
                    }
                    case ShopPurchaseStatus::Rejected:
                    {
                        return SW_LOCKEYREF( "Shop", "Rejected" );
                    }
                    case ShopPurchaseStatus::ServerError:
                    {
                        return SW_LOCKEYREF( "Shop", "ServerError" );
                    }
                    case ShopPurchaseStatus::Unverified:
                    {
                        return SW_LOCKEYREF( "Shop", "Unverified" );
                    }
                    case ShopPurchaseStatus::Busy:
                    {
                        return SW_LOCKEYREF( "Shop", "Busy" );
                    }
                }
                return SW_LOCKEYREF( "Shop", "Rejected" );
            }

            /** @brief 맨 위의 상점 화면입니다(닫는 중 제외). 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const ShopScreen* findTopShop( AutomationWatchValue& outValue )
            {
                const UISystem* pUI = game::findSubsystem<UISystem>();
                if ( pUI != nullptr )
                {
                    const vector<unique_ptr<UIScreen>>& listScreen = pUI->getScreens();
                    for ( auto iter = listScreen.rbegin(); iter != listScreen.rend(); ++iter )
                    {
                        const ShopScreen* pShop = castTo<ShopScreen>( iter->get() );
                        if ( pShop != nullptr && pShop->isClosing() == false )
                            return pShop;
                    }
                }
                outValue.setText( "no shop screen is open" );
                return nullptr;
            }

            /** @brief 감시 값 `Shop.PurchaseCount` 입니다. 열린 상점 화면에서 끝난 구매 수(성공 · 실패 모두)입니다. */
            [[nodiscard]] static bool readPurchaseCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ShopScreen* pShop = findTopShop( outValue );
                if ( pShop == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pShop->getFinishedCount() ) );
                return true;
            }

            /** @brief 감시 값 `Shop.LastStatus` 입니다. 마지막 구매 결과 이름(`ShopPurchaseStatus`)이고, 아직 없으면 `none` 입니다. */
            [[nodiscard]] static bool readLastStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ShopScreen* pShop = findTopShop( outValue );
                if ( pShop == nullptr )
                    return false;
                outValue.setText( pShop->getFinishedCount() == 0 ? "none" : getEnumName( pShop->getLastReceipt()._status ) );
                return true;
            }

            /** @brief 감시 값 `Shop.Busy` 입니다. 구매 확인 · 답을 기다리는 동안 1 입니다. */
            [[nodiscard]] static bool readBusy( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ShopScreen* pShop = findTopShop( outValue );
                if ( pShop == nullptr )
                    return false;
                outValue.setNumber( pShop->isBusy() ? 1.0 : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( shopPurchaseCount, "Shop.PurchaseCount", "Finished purchases (succeeded or failed) of the open shop screen", &ShopScreenInternal::readPurchaseCount, false, false );
    SW_AUTOMATION_QUERY_WATCH( shopLastStatus, "Shop.LastStatus", "Status name of the last purchase of the open shop screen (ShopPurchaseStatus); none before the first", &ShopScreenInternal::readLastStatus,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( shopBusy, "Shop.Busy", "1 while the open shop screen waits for a purchase confirmation or answer", &ShopScreenInternal::readBusy, false, false );

    ShopScreen::ShopScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _listOffer{}
        , _lastReceipt{}
        , _pProvider{ nullptr }
        , _shownRevision{ 0 }
        , _finishedCount{ 0 }
        , _bBuilt{ SW_FALSE }
        , _bBusy{ SW_FALSE }
    {
    }

    ShopScreen::~ShopScreen()
    {
        if ( _pProvider != nullptr )
            _pProvider->unbindScreen();
    }

    void ShopScreen::onProviderReleased()
    {
        _pProvider = nullptr;
        close();
    }

    UIScreenHandle ShopScreen::open( UISystem& ui, string_view documentPath, IShopPurchaseProvider& provider )
    {
        const UIScreenHandle handle  = ui.openScreen<ShopScreen>( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle;
        ShopScreen& screen = static_cast<ShopScreen&>( *pScreen );
        screen._pProvider  = &provider;
        provider.bindScreen( SW_DELEGATE_LAMBDA( Delegate<void()>, [pScreen = &screen]()
        { pScreen->onProviderReleased(); } ) );
        screen.rebuild();
        return handle;
    }

    string ShopScreen::makeRowText( const ShopOffer& offer )
    {
        const string row = SW_LOCALIZED_MESSAGE( "Shop", "OfferRow" ).addKey( "item", offer._displayName ).addInteger( "price", offer._unitPrice ).addKey( "currency", offer._currency.c_str() ).str();
        if ( offer._stock == ShopOffer::kUnlimitedStock )
            return row;
        return SW_LOCALIZED_MESSAGE( "Shop", "OfferRowStock" ).addKey( "offer", row ).addInteger( "stock", offer._stock ).str();
    }

    bool ShopScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( kCloseCommand ) )
        {
            close();
            return true;
        }
        if ( command != hashed_string( kPickCommand ) )
            return UIScreen::onCommand( command, source );
        UISystem* pUI = getUISystem();
        if ( pUI == nullptr || _pProvider == nullptr )
            return false;
        if ( _bBusy == SW_TRUE || _pProvider->isPurchasePending() )
            return true; // 확인 · 답을 기다리는 중의 누름은 버린다(중복 누름)
        const ListViewWidget*  pOffers = getTree().findWidget<ListViewWidget>( hashed_string( kOffersName ) );
        const optional<uint32> index   = pOffers != nullptr ? pOffers->findItemIndex( source ) : nullopt;
        if ( index.has_value() == false || *index >= _listOffer.size() )
            return false;
        const ShopOffer& offer = _listOffer[*index];
        int64            total = 0;
        if ( offer.computeTotal( 1, total ) == false )
        {
            setStatus( ShopScreenInternal::getStatusKey( ShopPurchaseStatus::Rejected ) );
            return true;
        }
        // 잔액은 공급자의 것(온라인이면 서버 원장의 사본)이다 — 모자라면 확인 창 없이 알린다. 정본 판정은 그래도 공급자가 한다.
        if ( _pProvider->getBalance( offer._currency ) < total )
        {
            setStatus( ShopScreenInternal::getStatusKey( ShopPurchaseStatus::NotEnoughMoney ) );
            SW_LOG_INFO_ID( "ShopScreen.NotEnoughMoney", "Shop: not enough %# for '%#' (price %#)", offer._currency.c_str(), offer._offerID.c_str(), total );
            return true;
        }
        ConfirmDialogDesc desc{};
        desc._title                     = SW_LOCKEYREF( "Shop", "ConfirmTitle" );
        desc._message                   = SW_LOCALIZED_MESSAGE( "Shop", "ConfirmMessage" ).addKey( "item", offer._displayName ).addInteger( "price", total ).addKey( "currency", offer._currency.c_str() ).str();
        desc._acceptText                = SW_LOCKEYREF( "Shop", "ConfirmAccept" );
        desc._cancelText                = SW_LOCKEYREF( "Shop", "ConfirmCancel" );
        const UIScreenHandle shop       = getHandle();
        const uint32         offerIndex = *index;
        const UIScreenHandle dialog     = ConfirmDialogScreen::open( *pUI, desc, SW_DELEGATE_LAMBDA( ConfirmDialogScreen::ResultDelegate, [pUI, shop, offerIndex]( ConfirmDialogResult result )
            { onConfirmed( *pUI, shop, offerIndex, result ); } ) );
        if ( dialog != kInvalidUIScreenHandle )
        {
            _bBusy = SW_TRUE;
        }
        return true;
    }

    void ShopScreen::onConfirmed( UISystem& ui, UIScreenHandle shop, uint32 offerIndex, ConfirmDialogResult result )
    {
        UIScreen* pScreen = ui.findScreen( shop );
        if ( pScreen == nullptr || pScreen->isClosing() )
            return;
        ShopScreen& screen = static_cast<ShopScreen&>( *pScreen );
        if ( result != ConfirmDialogResult::Accepted || screen._pProvider == nullptr || offerIndex >= screen._listOffer.size() )
        {
            screen._bBusy = SW_FALSE;
            screen.rebuild();
            return;
        }
        screen.setStatus( SW_LOCKEYREF( "Shop", "Purchasing" ) );
        const ShopOffer offer = screen._listOffer[offerIndex]; // 공급자가 바로 답해(오프라인) 목록을 다시 만들어도 안전하게 복사한다
        UISystem*       pUI   = &ui;
        screen._pProvider->beginPurchase( offer, 1, SW_DELEGATE_LAMBDA( ShopPurchaseDelegate, [pUI, shop]( const ShopPurchaseReceipt& receipt )
        { onPurchased( *pUI, shop, receipt ); } ) );
    }

    void ShopScreen::onPurchased( UISystem& ui, UIScreenHandle shop, const ShopPurchaseReceipt& receipt )
    {
        SW_LOG_INFO_ID( "ShopScreen.PurchaseFinished", "Shop: purchase of '%#' x%# finished: %# (paid %#, receipt %#)", receipt._offerID.c_str(), receipt._count,
                        getEnumName( receipt._status ), receipt._paid, receipt._receiptID.c_str() );
        UIScreen* pScreen = ui.findScreen( shop );
        if ( pScreen == nullptr || pScreen->isClosing() )
            return; // 답이 오기 전에 화면을 닫았다 — 정본은 공급자다
        static_cast<ShopScreen&>( *pScreen ).applyReceipt( receipt );
    }

    void ShopScreen::applyReceipt( const ShopPurchaseReceipt& receipt )
    {
        _lastReceipt = receipt;
        ++_finishedCount;
        _bBusy = SW_FALSE;
        setStatus( ShopScreenInternal::getStatusKey( receipt._status ) );
        rebuild();
    }

    void ShopScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        if ( _pProvider != nullptr && ( _bBuilt == SW_FALSE || _pProvider->getRevision() != _shownRevision ) )
            rebuild();
    }

    void ShopScreen::setStatus( const string& text )
    {
        TextWidget* pStatus = getTree().findWidget<TextWidget>( hashed_string( kStatusName ) );
        if ( pStatus != nullptr )
            pStatus->setText( text );
    }

    void ShopScreen::rebuild()
    {
        if ( _pProvider == nullptr )
            return;
        WidgetTree&     tree     = getTree();
        ListViewWidget* pOffers  = tree.findWidget<ListViewWidget>( hashed_string( kOffersName ) );
        TextWidget*     pBalance = tree.findWidget<TextWidget>( hashed_string( kBalanceName ) );
        if ( pOffers == nullptr || pBalance == nullptr || tree.findWidget<TextWidget>( hashed_string( kStatusName ) ) == nullptr )
        {
            SW_LOG_ERROR_ID( "ShopScreen.ShopDocumentNeeds", "Shop document %# needs a '%#' list view and '%#' and '%#' texts", getDocumentPath().c_str(), kOffersName, kBalanceName, kStatusName );
            _bBuilt = SW_TRUE; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        _pProvider->collectOffers( _listOffer );
        pOffers->setRowFactory( SW_DELEGATE_LAMBDA( ListViewWidget::RowFactoryDelegate, []()
        {
            unique_ptr<ButtonWidget> button = make_unique<ButtonWidget>();
            button->setCommand( hashed_string( kPickCommand ) );
            (void)button->addChild( make_unique<TextWidget>() );
            return unique_ptr<Widget>( std::move( button ) );
        } ) );
        pOffers->setRowBinder( SW_DELEGATE_LAMBDA( ListViewWidget::RowBinderDelegate, [this]( Widget& row, uint32 itemIndex )
        {
            ButtonWidget&    button = static_cast<ButtonWidget&>( row );
            const ShopOffer& offer  = _listOffer[itemIndex];
            // 구매 중에도 줄은 켜 둔다 — 고르기는 `_bBusy` 가 버리고, 끄면 확인 창이 닫힐 때 덮였던 줄로 포커스가 돌아오지 못한다(패드 사용자가 자리를 잃는다).
            button.setEnabled( offer._stock != 0 );
            // 줄 이름은 상품 순번이다(`Offer0` …) — 가상 목록이 줄을 다시 써도 포커스 · 시나리오가 상품으로 가리킨다.
            StringBuilder<constant::buffer::kMax32> nameBuilder;
            nameBuilder.appendFormat( "Offer%#", itemIndex );
            button.setName( hashed_string( nameBuilder.view() ) );
            static_cast<TextWidget*>( button.getChild( 0 ) )->setText( makeRowText( offer ) );
        } ) );
        pOffers->setItemCount( static_cast<uint32>( _listOffer.size() ) );
        // 잔액은 상품이 쓰는 재화마다 한 번(진열 순서).
        vector<string>        listBalance;
        vector<hashed_string> listShown;
        for ( const ShopOffer& offer : _listOffer )
        {
            if ( std::find( listShown.begin(), listShown.end(), offer._currency ) != listShown.end() )
                continue;
            listShown.push_back( offer._currency );
            listBalance.push_back( SW_LOCALIZED_MESSAGE( "Shop", "BalanceLine" ).addInteger( "amount", _pProvider->getBalance( offer._currency ) ).addKey( "currency", offer._currency.c_str() ).str() );
        }
        pBalance->setText( LocalizedMessage::join( "  ", listBalance ) );
        _shownRevision = _pProvider->getRevision();
        _bBuilt        = SW_TRUE;
    }
} // namespace sw
