/*
Copyright (c) 2024, FirstFive8, Inc

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
#ifndef __CDR_H__
#define __CDR_H__

#include <string>

#include <sofia-sip/msg.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

using std::string ;

namespace drachtio {
    
    class Cdr {
    public:

        static std::shared_ptr<Cdr> postCdr( std::shared_ptr<Cdr> cdr, const string& encodedMsg = "" ) ;

        Cdr( const Cdr& ) = delete;

        enum AgentRole_t {
            role_undefined = 0,
            proxy_uac,
            proxy_uas,
            uac,
            uas
        } ;

        enum RecordType_t {
            attempt_record = 0,
            start_record,
            stop_record
        } ;

        enum TerminationReason_t {
            no_termination = 0,
            call_rejected,
            call_canceled,
            normal_release,
            session_expired,
            ackbye,
            system_initiated_termination,
            system_error_initiated_termination
        } ;

        Cdr( msg_t* msg, const char* source, RecordType_t recordType, AgentRole_t agentRole ) ;
        ~Cdr() ;

        const char* getRecordType() {
            static const char * szRecordTypes[] = {
                "cdr:attempt",
                "cdr:start",
                "cdr:stop"
            } ;
            return szRecordTypes[ static_cast<int>(m_recordType) ] ;
        }
        const char* getAgentRole() {
            static const char * szRoles[] = {
                "undefined",
                "proxy-uac",
                "proxy-uas",
                "uac",
                "uas",
            } ;
            return szRoles[ static_cast<int>(m_agentRole) ] ;
        }
        const char* getTerminationReason() {
            static const char * szReasons[] = {
                "undefined",
                "call-rejected",
                "call-canceled",
                "normal-release",
                "session-expired",
                "system-initiated-termination",
                "system-error-initiated-termination"
            } ;
            return szReasons[ static_cast<int>(m_terminationReason) ] ;
        }

        void encodeMessage( string& encodedMessage ) ;
        void encodeMetaData( string& metaData ) ;
        void stamp(void) { m_eventTime = su_now() ; }
        void setEncodedMessage(const string& s) { m_encodedMessage = s ;}
        
    protected:
        msg_t*      m_msg ;

        RecordType_t   m_recordType ;
        AgentRole_t m_agentRole ;
        su_time_t   m_eventTime ;

        TerminationReason_t m_terminationReason ;

        string      m_encodedMessage ;
        string      m_source ;
    } ;


    class CdrAttempt: public Cdr {
    public:
        CdrAttempt( msg_t* msg, const char* source ) ;
        ~CdrAttempt() {}
    } ;

    class CdrStart: public Cdr {
    public:
        CdrStart( msg_t* msg, const char* source, AgentRole_t agentRole ) ;
        ~CdrStart() {}
    } ;

    class CdrStop: public Cdr {
    public:
        CdrStop( msg_t* msg, const char* source, TerminationReason_t  terminationReason ) ;
        ~CdrStop() {}
    } ;
}  



#endif
