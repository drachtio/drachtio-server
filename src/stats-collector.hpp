/*
Copyright (c) 2013-2019, David C Horton

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#ifndef __STATSCOLLECTOR_HPP__
#define __STATSCOLLECTOR_HPP__

#include <string>
#include <map>
#include <vector>

using std::string ;

namespace drachtio {
  
  class StatsCollector {
  public:
    typedef const std::map<string, string> mapLabels_t;
    typedef std::vector<double> BucketBoundaries ;

    enum Metric_t{
      COUNTER,
      GAUGE,
      HISTOGRAM,
      SUMMARY
    };

    //static std::shared_ptr<Cdr> postCdr( std::shared_ptr<Cdr> cdr, const string& encodedMsg = "" ) ;

    StatsCollector( const StatsCollector& ) = delete;

    StatsCollector() ;
    ~StatsCollector() ;

    bool enabled(void) const { return nullptr != m_pimpl; }
    void enablePrometheus(const char* szHostport);

    // counters
    void counterCreate(const string& name, const char* desc);
    void counterIncrement(const string& name, mapLabels_t labels = {});
    void counterIncrement(const string& name, double val, mapLabels_t labels = {});

    // gauges
    void gaugeCreate(const string& name, const char* desc);
    void gaugeIncrement(const string& name, mapLabels_t labels = {});
    void gaugeIncrement(const string& name, double val, mapLabels_t labels = {});
    void gaugeDecrement(const string& name, mapLabels_t labels = {});
    void gaugeDecrement(const string& name, double val, mapLabels_t labels = {});
    void gaugeSet(const string& name, double val, mapLabels_t labels = {});
    void gaugeSetToCurrentTime(const string& name, mapLabels_t labels = {});

    // histogram
    void histogramCreate(const string& name, const char* desc, const BucketBoundaries& buckets);
    void histogramObserve(const string& name, double val, mapLabels_t labels = {}) ;

    // summary
    //void summaryObserve(const string& name, const double val) ;

  protected:

  private: 
    class PromImpl ;
    PromImpl* m_pimpl ;
  };
} 



#endif
