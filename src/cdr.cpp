#include "drachtio.h"
#include "controller.hpp"
#include "client-controller.hpp"
#include "cdr.hpp"

namespace drachtio {
  
  boost::shared_ptr<Cdr> Cdr::postCdr( boost::shared_ptr<Cdr> pCdr, const string& encodedMessage ) {
    if( theOneAndOnlyController->getConfig()->generateCdrs() ) {
      pCdr->stamp() ;
      pCdr->setEncodedMessage( encodedMessage ) ;
      boost::shared_ptr<ClientController> pClientController = theOneAndOnlyController->getClientController() ;
      client_ptr client = pClientController->selectClientForRequestOutsideDialog(pCdr->getRecordType()) ;
      if( client ) {
        string encodedMessage ;
        string meta ;

        pCdr->encodeMessage( encodedMessage ) ;
        pCdr->encodeMetaData( meta ) ;

        pClientController->getIOService().post( boost::bind(&Client::sendCdrToClient, client, encodedMessage, meta ) ) ;
      }
    }
    return pCdr ;
  }


  Cdr::Cdr( msg_t* msg, const char* source, RecordType_t recordType, AgentRole_t agentRole ) : 
    m_msg(msg_ref(msg)), m_recordType(recordType), m_agentRole(agentRole), m_source(source),
    m_terminationReason(no_termination) {

  }
  Cdr::~Cdr() {
    msg_unref( m_msg ) ;
  }

  void Cdr::encodeMessage( string& encodedMessage ) {
    if( m_encodedMessage.length() > 0 ) encodedMessage = m_encodedMessage ;
    else EncodeStackMessage( sip_object(m_msg), encodedMessage ) ;
  }
  void Cdr::encodeMetaData( string& metaData ) {
    unsigned short second, minute, hour;
    char time[64] ;

    second = (unsigned short)(m_eventTime.tv_sec % 60);
    minute = (unsigned short)((m_eventTime.tv_sec / 60) % 60);
    hour = (unsigned short)((m_eventTime.tv_sec / 3600) % 24);
    sprintf(time, "%02u:%02u:%02u.%06lu", hour, minute, second, m_eventTime.tv_usec) ;

    metaData = "" ;
    metaData += getRecordType() ;
    metaData += "|" ;
    metaData += m_source ;
    metaData += "|" ;
    metaData += time ;

    if( start_record == Cdr::m_recordType ) {
      metaData += "|" ;
      metaData += getAgentRole() ;      
    }
    else if( stop_record == Cdr::m_recordType ) {
      metaData += "|" ;
      metaData += getTerminationReason() ;
    }
  }


  CdrAttempt::CdrAttempt( msg_t* msg, const char* source ) : 
    Cdr(msg, source, attempt_record, Cdr::role_undefined ) {
    
  }

  CdrStart::CdrStart( msg_t* msg, const char* source, AgentRole_t agentRole  ) : 
    Cdr(msg, source, start_record, agentRole ) {
    
  }

  CdrStop::CdrStop( msg_t* msg, const char* source, TerminationReason_t  terminationReason ) : 
    Cdr(msg, source, stop_record, Cdr::role_undefined ) {
      m_terminationReason = terminationReason ;
  }
}
