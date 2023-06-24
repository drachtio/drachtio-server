#include <assert.h>
#include <unordered_map>

#include "stats-collector.hpp"
#include "drachtio.h"
#include "controller.hpp"

#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/detail/ckms_quantiles.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>

using namespace prometheus;

namespace drachtio {
  
  //using BucketBoundaries = std::vector<double>;
  //typedef std::vector<double> BucketBoundaries ;

  class StatsCollector::PromImpl {
  public:
    using Quantiles = std::vector<prometheus::detail::CKMSQuantiles::Quantile>;

    class HistogramSpec_t {
    public:
      HistogramSpec_t(const BucketBoundaries& b, std::shared_ptr<Family<Histogram> > ptr) :
        buckets(b), p(ptr) {}
      BucketBoundaries buckets;
      std::shared_ptr<Family<Histogram> > p;
    }  ;
    typedef std::unordered_map<string, std::shared_ptr<Family<Counter> > > mapCounter_t;
    typedef std::unordered_map<string, std::shared_ptr<Family<Gauge> > > mapGauge_t;
    typedef std::unordered_map<string, HistogramSpec_t > mapHistogram_t;

    PromImpl() = delete;
    PromImpl(const char* szHostport) : m_exposer(szHostport) {
      m_registry = std::make_shared<Registry>();
      m_exposer.RegisterCollectable(m_registry);
    }
    ~PromImpl() {}

    void buildCounter(const string& name, const char* desc) {
      auto& m = BuildCounter()
        .Name(name)
        .Help(desc)
        .Register(*m_registry);

      std::shared_ptr<Family<Counter> > p(&m);
      m_mapCounter.insert(mapCounter_t::value_type(name, p));
    }

    void counterIncrement(const string& name, mapLabels_t& labels) {
      mapCounter_t::const_iterator it = m_mapCounter.find(name) ;
      if (m_mapCounter.end() != it) {
        it->second->Add(labels).Increment() ;
      }
    }
    void counterIncrement(const string& name, const double val, mapLabels_t& labels) {
      mapCounter_t::const_iterator it = m_mapCounter.find(name) ;
      if (m_mapCounter.end() != it) {
        it->second->Add(labels).Increment(val) ;
      }
    }

    void buildGauge(const string& name, const char* desc) {
      auto& m = BuildGauge()
        .Name(name)
        .Help(desc)
        .Register(*m_registry);

      std::shared_ptr<Family<Gauge> > p(&m);
      m_mapGauge.insert(mapGauge_t::value_type(name, p));
    }

    void gaugeIncrement(const string& name, mapLabels_t& labels) {
      mapGauge_t::const_iterator it = m_mapGauge.find(name) ;
      if (m_mapGauge.end() != it) {
        it->second->Add(labels).Increment() ;
      }
    }

    void gaugeIncrement(const string& name, const double val, mapLabels_t& labels) {
      mapGauge_t::const_iterator it = m_mapGauge.find(name) ;
      if (m_mapGauge.end() != it) {
        it->second->Add(labels).Increment(val) ;
      }
    }

    void gaugeDecrement(const string& name, mapLabels_t& labels) {
       mapGauge_t::const_iterator it = m_mapGauge.find(name) ;
      if (m_mapGauge.end() != it) {
        it->second->Add(labels).Decrement() ;
      }
    }

    void gaugeDecrement(const string& name, const double val, mapLabels_t& labels) {
       mapGauge_t::const_iterator it = m_mapGauge.find(name) ;
      if (m_mapGauge.end() != it) {
        it->second->Add(labels).Decrement(val) ;
      }
    }

    void gaugeSet(const string& name, const double val, mapLabels_t& labels) {
       mapGauge_t::const_iterator it = m_mapGauge.find(name) ;
      if (m_mapGauge.end() != it) {
        it->second->Add(labels).Set(val) ;
      }
    }

    void gaugeSetToCurrentTime(const string& name, mapLabels_t& labels) {
       mapGauge_t::const_iterator it = m_mapGauge.find(name) ;
      if (m_mapGauge.end() != it) {
        it->second->Add(labels).SetToCurrentTime() ;
      }
    }

    void buildHistogram(const string& name, const char* desc, const BucketBoundaries& buckets) {
      auto& m = BuildHistogram()
        .Name(name)
        .Help(desc)
        .Register(*m_registry);

      std::shared_ptr<Family<Histogram> > p(&m);
      m_mapHistogram.insert(mapHistogram_t::value_type(name, HistogramSpec_t(buckets, p)));
    }

    void histogramObserve(const string& name, const double val, mapLabels_t& labels) {
       mapHistogram_t::const_iterator it = m_mapHistogram.find(name) ;
      if (m_mapHistogram.end() != it) {
        const HistogramSpec_t& spec = it->second;
        std::shared_ptr<Family<Histogram> > pFamily = spec.p;
        pFamily->Add(labels, spec.buckets).Observe(val);
      }
    }

  
  private:

    Exposer m_exposer;
    std::shared_ptr<Registry> m_registry;

    mapCounter_t  m_mapCounter;
    mapGauge_t  m_mapGauge;
    mapHistogram_t  m_mapHistogram;
  };

  StatsCollector::StatsCollector() : m_pimpl(nullptr) {

  }

  StatsCollector::~StatsCollector() {

  }

  void StatsCollector::enablePrometheus(const char* szHostport) {
    assert(nullptr == m_pimpl);
    m_pimpl = new PromImpl(szHostport);
  }

  // counters
  void StatsCollector::counterCreate(const string& name, const char* desc) {
    if (nullptr != m_pimpl) m_pimpl->buildCounter(name, desc);    
  }
  void StatsCollector::counterIncrement(const string& name, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->counterIncrement(name, labels); 
  }
  void StatsCollector::counterIncrement(const string& name, const double val, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->counterIncrement(name, val, labels); 
  }

  // gauges
  void StatsCollector::gaugeCreate(const string& name, const char* desc) {
    if (nullptr != m_pimpl) m_pimpl->buildGauge(name, desc);    
  }
  void StatsCollector::gaugeIncrement(const string& name, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->gaugeIncrement(name, labels); 
  }
  void StatsCollector::gaugeIncrement(const string& name, const double val, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->gaugeIncrement(name, val, labels); 
  }
  void StatsCollector::gaugeDecrement(const string& name, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->gaugeDecrement(name, labels); 
  }
  void StatsCollector::gaugeDecrement(const string& name, const double val, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->gaugeDecrement(name, val, labels); 
  }
  void StatsCollector::gaugeSet(const string& name, const double val, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->gaugeSet(name, val, labels); 
  }
  void StatsCollector::gaugeSetToCurrentTime(const string& name, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->gaugeSetToCurrentTime(name, labels); 
  }

  // histograms
  void StatsCollector::histogramCreate(const string& name, const char* desc, const BucketBoundaries& buckets) {
    if (nullptr != m_pimpl) m_pimpl->buildHistogram(name, desc, buckets);    
  }
  void StatsCollector::histogramObserve(const string& name, double val, mapLabels_t labels) {
    if (nullptr != m_pimpl) m_pimpl->histogramObserve(name, val, labels); 
  }

}
