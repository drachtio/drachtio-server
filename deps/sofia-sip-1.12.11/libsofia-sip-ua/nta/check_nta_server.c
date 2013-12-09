/*
 * This file is part of the Sofia-SIP package
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Pekka Pessi <pekka.pessi@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**@CFILE check_nta_server.c
 *
 * @brief Check-driven tester for NTA server transactions
 *
 * @author Pekka Pessi <Pekka.Pessi@nokia.com>
 *
 * @copyright (C) 2010 Nokia Corporation.
 */

#include "config.h"

#include "check_nta.h"
#include "s2base.h"
#include "s2dns.h"

#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_header.h>
#include <sofia-sip/su_tagarg.h>
#include <sofia-sip/su_tag_io.h>
#include <sofia-sip/sres_sip.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define NONE ((void *)-1)

static struct dialog *dialog = NULL;

static void
server_setup(void)
{
  s2_nta_setup("NTA", NULL, TAG_END());
  s2_nta_agent_setup(URL_STRING_MAKE("sip:0.0.0.0:*"), NULL, NULL,
		     TAG_END());

  dialog = su_home_new(sizeof *dialog); fail_if(!dialog);
  dialog->local = sip_from_make(dialog->home, "Alice <sip:alice@example.net>");
  dialog->remote = sip_to_make(dialog->home, "Bob <sip:bob@example.com>");
}

static void
server_teardown(void)
{
  mark_point();
  s2_nta_teardown();
}

START_TEST(server_3_0_0)
{
  struct event *request;
  struct message *response;

  S2_CASE("server-3.0.0", "Receive MESSAGE",
	  "Basic non-INVITE transaction");

  fail_if(s2_sip_request_to(dialog, SIP_METHOD_MESSAGE, NULL,
			    TAG_END()));
  request = s2_nta_wait_for(wait_for_method, (void *)sip_method_message, 0);
  fail_unless(request != NULL);
  fail_unless(request->irq != NULL);

  nta_incoming_treply(request->irq, SIP_200_OK, TAG_END());
  nta_incoming_destroy(request->irq);

  response = s2_sip_wait_for_response(200, SIP_METHOD_MESSAGE);
  fail_unless(response != NULL);
}
END_TEST

START_TEST(server_3_0_1)
{
  struct event *request;
  struct message *response;
  sip_via_t *vorig = s2_sip_tport_via(s2sip->udp.tport);
  sip_via_t via[2];
  char *v0_params[8] = {}, *v1_params[8] = {};
  char branch0[32], branch1[32];

  S2_CASE("server-3.0.1", "Receive MESSAGE",
	  "Basic non-INVITE transaction with comma-separated Via headers");

  s2_sip_msg_flags = MSG_FLG_COMMA_LISTS|MSG_FLG_COMPACT;

  fail_if(vorig == NULL);

  fail_if(nta_agent_set_params(s2->nta,
			       NTATAG_SIPFLAGS(MSG_FLG_EXTRACT_COPY),
			       TAG_END())
	  != 1);

  via[0] = *vorig;
  via[0].v_host = "example.net";
  via[0].v_params = (void *)v0_params;
  snprintf(v0_params[0] = branch0, sizeof branch0,
	   "branch=z9hG4bK%lx", ++s2sip->tid);

  fail_if(vorig == NULL);

  via[1] = *vorig;
  via[1].v_params = (void *)v1_params;
  snprintf(v1_params[0] = branch1, sizeof branch1,
	   "branch=z9hG4bK%lx", ++s2sip->tid);

  fail_if(s2_sip_request_to(dialog, SIP_METHOD_MESSAGE, NULL,
			    SIPTAG_VIA(via + 1),
			    SIPTAG_VIA(via + 0),
			    TAG_END()));
  request = s2_nta_wait_for(wait_for_method, (void *)sip_method_message, 0);
  fail_unless(request != NULL);
  fail_unless(request->irq != NULL);

  fail_if(nta_incoming_treply(request->irq, SIP_200_OK, TAG_END()) != 0);
  nta_incoming_destroy(request->irq);

  response = s2_sip_wait_for_response(200, SIP_METHOD_MESSAGE);
  fail_unless(response != NULL);
  fail_unless(response->sip->sip_via != NULL &&
	      response->sip->sip_via->v_next != NULL);
}
END_TEST


/* ---------------------------------------------------------------------- */

TCase *check_nta_server_3_0(void)
{
  TCase *tc = tcase_create("NTA 2.0 - Server");

  tcase_add_checked_fixture(tc, server_setup, server_teardown);

  tcase_set_timeout(tc, 2);

  tcase_add_test(tc, server_3_0_0);
  tcase_add_test(tc, server_3_0_1);

  return tc;
}
