#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "sighup.hpp"
#include "controller.hpp"

namespace {
  /* self-pipe: a signal handler may only write(); the sofia thread reads */
  int pipeFds[2] = { -1, -1 };
  su_wait_t pipeWait[1];
  nta_agent_t* theAgent = nullptr;

  int onSignal(su_root_magic_t*, su_wait_t*, su_wakeup_arg_t*) {
    char buf[64];
    while (read(pipeFds[0], buf, sizeof(buf)) > 0) ;   /* several SIGHUPs, one pass */

    theOneAndOnlyController->handleSigHup(SIGHUP);

    int n = nta_agent_reload_tls_files(theAgent);
    if (n < 0) {
      DR_LOG(drachtio::log_error) << "Sighup::onSignal - could not reload the tls certificates, "
        "keeping the old certificate on every transport (see the sofia log for why)";
    }
    else if (n > 0) {
      DR_LOG(drachtio::log_notice) << "Sighup::onSignal - reloaded the tls certificate on " << n << " transport(s)";
    }

    if (auto clients = theOneAndOnlyController->getClientController()) clients->reloadTlsFiles();
    return 0;
  }

  bool setFlags(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
  }
}

namespace drachtio {

  bool Sighup::start(su_root_t* root, nta_agent_t* agent) {
    if (pipe(pipeFds) != 0 || !setFlags(pipeFds[0]) || !setFlags(pipeFds[1])) {
      DR_LOG(log_error) << "Sighup::start - cannot create pipe: " << strerror(errno);
      return false;
    }
    theAgent = agent;
    if (su_wait_create(pipeWait, pipeFds[0], SU_WAIT_IN) != 0 ||
      su_root_register(root, pipeWait, onSignal, nullptr, 0) < 0) {
      DR_LOG(log_error) << "Sighup::start - cannot watch the pipe; SIGHUP will be ignored";
      return false;
    }
    return true;
  }

  void Sighup::notify() {
    if (pipeFds[1] >= 0) {
      int saved = errno;
      char c = 1;
      ssize_t rc = write(pipeFds[1], &c, 1);   /* a full pipe already has a pass pending */
      (void) rc;
      errno = saved;
    }
  }

}
