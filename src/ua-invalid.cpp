/*
Copyright (c) 2022, David C Horton

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
#include "ua-invalid.hpp"
#include "controller.hpp"

namespace drachtio {

    void UaInvalidData::setTport(tport_t* tp) {
      if (tp == m_tp) return ;
      string uri;
      getUri(uri);

      DR_LOG(log_info) << "UaInvalidData::setTport " << uri << " unref old tport " << (void *)m_tp << " ref new tport " << (void *)tp  ;
      tport_unref(m_tp) ;
      m_tp = tp;
      tport_ref(m_tp) ;
    }

    void UaInvalidData::extendExpires(int expires) {
      string uri;
      getUri(uri);

      DR_LOG(log_info) << "UaInvalidData::extendExpires " << uri << " by " << expires << "secs" ;
      m_expires = time(0) + expires ;
    }
 }
