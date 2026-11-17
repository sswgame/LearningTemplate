/**
 * @file ThemeParkTestRules.h
 * @brief 테마파크 테스트가 쓰는 공원 규칙 줄(`<Rules>`)과 그 값의 설정입니다. 게임 데이터(`game/themepark/data/rides.xml`)와 같은 값이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"

namespace sw
{
    /** @brief 공원 규칙 한 줄입니다(모든 칸이 필수). */
    inline constexpr const utf8* kParkRules = R"(<Rules guestArrivalPerMinute="12.0" walkSpeed="2.5" wanderRadius="6.0" queuePatience="90.0" energyDrainPerSecond="0.0020833333333333333" nauseaRecoveryPerSecond="0.008333333333333333" queueSpacing="0.6" leaveEnergy="0.1" leaveHappiness="0.15" nauseaPerRating="0.06" queueUnhappinessPerSecond="0.002" maxGuests="150" randomSeed="12345" guestCashMin="20" guestCashMax="80" guestMinIntensityMax="3.0" guestMaxIntensityMin="3.0" guestMaxIntensityMax="9.0" guestNauseaToleranceMin="0.3" guestNauseaToleranceMax="0.9" arrivalBaseFactor="0.4" arrivalRatingScale="1000.0" noRideArrivalFactor="0.2" noArrivalEntryFee="100.0" rideFunBase="0.04" rideFunPerExcitement="0.025" greatRideExcitement="6.0" tameRideFunScale="0.5" sickFunPenalty="0.1" rideEnergyCost="0.04" queueGiveUpUnhappiness="0.1" nothingToRideUnhappiness="0.05" leaveNausea="0.95" fairPriceValueScale="2.0" fairPriceSlack="0.5" rideChoiceJitter="1.5" tameRideChoicePenalty="2.0" repeatRideChoicePenalty="1.5" minWalkSeconds="0.5" ratingPerOpenRide="25.0" ratedOpenRideCount="8" emptyParkRating="400.0" ratingBase="200.0" ratingPerHappiness="600.0" ratingPerNausea="300.0" ratingPerQueueComplaint="100.0" fixedStep="0.25" maxFrameTime="5.0" currency="Cash"/>)";

    /** @brief 규칙 줄을 읽은 설정입니다. 테스트가 C++ 기본값 대신 데이터 길로 공원을 연다. */
    struct ThemeParkTestRules
    {
        static ThemeParkSettings make()
        {
            ParkLayout           layout;
            CoasterLayoutCatalog layouts;
            const string         text = string( "<ParkLayout gateX=\"0\" gateZ=\"0\" startingCash=\"1000\" entryFee=\"0\">" ) + kParkRules +
                                "<FlatRide id=\"cups\" name=\"Cups\" excitement=\"2\" intensity=\"1\" nausea=\"1\" cycleTime=\"40\" capacity=\"16\" price=\"3\" runningCost=\"3\" cost=\"2000\" "
                                "x=\"45\" z=\"0\" entranceX=\"45\" entranceZ=\"-10\" shape=\"Cylinder\" sizeX=\"6\" sizeY=\"2\" sizeZ=\"6\" color=\"1,1,1,1\"/></ParkLayout>";
            const bool bLoaded = layout.loadFromXMLText( text, layouts, "ThemeParkTestRules" );
            SW_ASSERT( bLoaded );
            (void)bLoaded;
            return layout.getRules();
        }
    };
} // namespace sw
