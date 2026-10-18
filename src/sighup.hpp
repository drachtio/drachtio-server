#ifndef __SIGHUP_HPP__
#define __SIGHUP_HPP__

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>

namespace drachtio {

  /* SIGHUP re-reads the configuration and reloads the TLS/WSS certificates,
     and the admin port's, from their files, so a renewed certificate is used
     without a restart.
     The signal handler only writes to a pipe; the work runs on the sofia
     thread, which owns the transports. Calls in progress keep the
     connections, and the certificate, they already have. */
  class Sighup {
  public:
    /* call on the sofia thread once the agent and its transports exist */
    static bool start(su_root_t* root, nta_agent_t* agent);

    /* async-signal-safe */
    static void notify();
  };

}

#endif
