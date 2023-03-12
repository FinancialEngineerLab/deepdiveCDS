#include <ql/quantlib.hpp>

#if !defined(BOOST_ALL_NO_LIB) && defined(BOOST_MSVC)
#  include <ql/auto_link.hpp>
#endif

#include <boost/timer.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>

#include <iostream>
#include <iomanip>

using namespace QuantLib;
using namespace std;

#ifdef BOOST_MSVC
#  ifdef QL_ENABLE_THREAD_SAFE_OBSERVER_PATTERN
#    include <ql/auto_link.hpp>
#    define BOOST_LIB_NAME boost_system
#    include <boost/config/auto_link.hpp>
#    undef BOOST_LIB_NAME
#    define BOOST_LIB_NAME boost_thread
#    include <boost/config/auto_link.hpp>
#    undef BOOST_LIB_NAME
#  endif
#endif

#if defined(QL_ENABLE_SESSIONS)
namespace QuantLib {
    Integer sessionId() { return 0; }
}

#endif


namespace {

    class UpdateCounter : public Observer {
      public:
        UpdateCounter() = default;
        void update() override { ++counter_; }
        Size counter() const { return counter_; }

      private:
        Size counter_ = 0;
    };

    class RestoreUpdates {
      public:
        ~RestoreUpdates() {
            ObservableSettings::instance().enableUpdates();
        }
    };

}


#ifdef QL_ENABLE_THREAD_SAFE_OBSERVER_PATTERN
#include <atomic>
#include <mutex>
#include <thread>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <list>

namespace {

    class MTUpdateCounter : public Observer {
      public:
        MTUpdateCounter() : counter_(0) {
            ++instanceCounter_;
        }
        ~MTUpdateCounter() {
            --instanceCounter_;
        }
        void update() {
            ++counter_;
        }
        int counter() { return counter_; }
        static int instanceCounter() { return instanceCounter_; }

      private:
        std::atomic<int> counter_;
        static std::atomic<int> instanceCounter_;
    };

    std::atomic<int> MTUpdateCounter::instanceCounter_(0);

    class GarbageCollector {
      public:
        GarbageCollector() : terminate_(false) { }

        void addObj(const ext::shared_ptr<MTUpdateCounter>& updateCounter) {
            std::lock_guard<std::mutex> lock(mutex_);
            objList.push_back(updateCounter);
        }

        void run() {
            while(!terminate_) {
                Size objListSize;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    objListSize = objList.size();
                }

                if (objListSize > 20) {
                    // trigger gc
                    while (objListSize > 0) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        objList.pop_front();
                        objListSize = objList.size();
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            objList.clear();
        }

        void terminate() {
            terminate_ = true;
        }
      private:
        std::mutex mutex_;
        std::atomic<bool> terminate_;

        std::list<ext::shared_ptr<MTUpdateCounter> > objList;
    };
}
#endif


int main(int, char* [])
{
    //try
    //{
        
        Date tradeDate(10,Mar,2023);
        Date CDS_settle =  WeekendsOnly().advance(tradeDate, 1 * Days);
        
        Settings::instance().evaluationDate() =tradeDate;
        Date curveDate = Settings::instance().evaluationDate();
        
        SavedSettings backup;
        
        std::cout << Settings::instance().evaluationDate()  << std::endl;
        std::cout << tradeDate << std::endl;
        std::cout << curveDate << std::endl;
        
        ext::shared_ptr<Estr> estser(new Estr());
        std::vector<ext::shared_ptr<RateHelper>> esterInstruments;
        std::vector<std::pair<int, double>> SWAPDATA;
        SWAPDATA.emplace_back(std::make_pair(3, 0.01));
        SWAPDATA.emplace_back(std::make_pair(6, 0.02));
        SWAPDATA.emplace_back(std::make_pair(9, 0.03));
        SWAPDATA.emplace_back(std::make_pair(12, 0.04));
        SWAPDATA.emplace_back(std::make_pair(24, 0.05));
        SWAPDATA.emplace_back(std::make_pair(36, 0.07));
        SWAPDATA.emplace_back(std::make_pair(48, 0.08));
        
        for (int i = 0; i < SWAPDATA.size(); i++)
        {
            ext::shared_ptr<Quote> sw(new SimpleQuote(SWAPDATA[i].second));
            auto helper = ext::make_shared<OISRateHelper>(2, SWAPDATA[i].first * Months, Handle<Quote>(sw), estser);
            esterInstruments.push_back(helper);
        }
        
        
        Settings::instance().evaluationDate() =tradeDate;
        ext::shared_ptr<YieldTermStructure> esterTermStructure(new PiecewiseYieldCurve<Discount, LogLinear>(curveDate,
                                                                                                            esterInstruments,                           Actual365Fixed()));
        
        esterTermStructure->registerWith(Settings::instance().evaluationDate()= tradeDate);
        esterTermStructure->enableExtrapolation(true);
        esterTermStructure->deepUpdate();
        
        RelinkableHandle<YieldTermStructure> discountingTermStructure;
        discountingTermStructure.linkTo(esterTermStructure);
        
        std::cout << discountingTermStructure->referenceDate() << std::endl;
        std::cout << Settings::instance().evaluationDate()  << std::endl;
        std::cout <<discountingTermStructure->discount(0.25, true) << std::endl;
        std::cout <<discountingTermStructure->discount(0.5, true) << std::endl;
        std::cout <<discountingTermStructure->discount(0.75, true) << std::endl;
        std::cout <<discountingTermStructure->discount(1.0, true) << std::endl;
        
        
        CreditDefaultSwap::PricingModel model = CreditDefaultSwap::ISDA;
        ext::shared_ptr<CdsHelper> cds6m(new SpreadCdsHelper(
                                                             0.00300, 6 * Months, 1, WeekendsOnly(), Quarterly, Following,
                                                             DateGeneration::CDS, Actual360(), 0.4, discountingTermStructure, true, true, Date(),
                                                             Actual360(true), true, model));
        ext::shared_ptr<CdsHelper> cds12m(new SpreadCdsHelper(
                                                              0.008489, 12 * Months, 1, WeekendsOnly(), Quarterly, Following,
                                                              DateGeneration::CDS,  Actual360(), 0.4, discountingTermStructure, true, true, Date(),
                                                              Actual360(true), true, model));
        ext::shared_ptr<CdsHelper> cds24m(new SpreadCdsHelper(
                                                              0.006002, 24 * Months, 1,WeekendsOnly(), Quarterly, Following,
                                                              DateGeneration::CDS,  Actual360(), 0.4, discountingTermStructure, true, true, Date(),
                                                              Actual360(true), true, model));
        ext::shared_ptr<CdsHelper> cds36m(new SpreadCdsHelper(
                                                              0.005002, 36 * Months, 1, WeekendsOnly(), Quarterly, Following,
                                                              DateGeneration::CDS,  Actual360(), 0.4, discountingTermStructure, true, true, Date(),
                                                              Actual360(true), true, model));
        
        std::vector<ext::shared_ptr<DefaultProbabilityHelper> > isdaCdsHelpers;
        
        isdaCdsHelpers.push_back(cds6m);
        isdaCdsHelpers.push_back(cds12m);
        isdaCdsHelpers.push_back(cds24m);
        isdaCdsHelpers.push_back(cds36m);
        
        Settings::instance().evaluationDate() =tradeDate;
        Handle<DefaultProbabilityTermStructure> isdaCts =
        Handle<DefaultProbabilityTermStructure>(ext::make_shared<
                                                PiecewiseDefaultCurve<SurvivalProbability, LogLinear> >(curveDate
                                                                                                        ,isdaCdsHelpers,                            Actual365Fixed()));
        isdaCts->registerWith(Settings::instance().evaluationDate()= tradeDate);
        isdaCts->enableExtrapolation(true);
        isdaCts->deepUpdate();
        std::cout << tradeDate<<std::endl;
        std::cout << isdaCts->referenceDate() << std::endl;
        std::cout << discountingTermStructure->referenceDate() << std::endl;
        std::cout << Settings::instance().evaluationDate()  << std::endl;
        
        
        
        std::cout << "ISDA credit curve:" << std::endl;
        std::cout << "date;time;survivalprob" << std::endl;
        
        std::cout <<  ";" << isdaCts->survivalProbability(0.25,true) << std::endl;
        std::cout <<  ";" << isdaCts->survivalProbability(0.5,true) << std::endl;
        std::cout << ";" << isdaCts->survivalProbability(1.0,true) << std::endl;
        std::cout << ";" << isdaCts->survivalProbability(2.0,true) << std::endl;
        std::cout << ";" << isdaCts->survivalProbability(3.0, true) << std::endl;
        
        
        std::cout << isdaCts->defaultProbability(0.25, true) << std::endl;
        std::cout << isdaCts->hazardRate(0.25, true) << std::endl;
        
        ext::shared_ptr<IsdaCdsEngine> isdaPricer =
        ext::make_shared<IsdaCdsEngine>(isdaCts, 0.4, discountingTermStructure);
        return 0;
        
    //}
    //catch (exception &e)
    //{
    //   cerr << e.what() << endl;
    //    return 1;
    //}
    //catch (...)
    //{
    //   cerr << "unknown error" << endl;
    //    return 1;
    //}


}
