# Prometheus.io integration

drachtio exposes the following metrics to Prometheus:
```
./drachtio --prometheus-scrape-port 9099 -f ../test/drachtio.conf.xml &

$ curl 127.0.0.1:9099/metrics
# HELP drachtio_sip_requests_in_total count of sip requests received
# TYPE drachtio_sip_requests_in_total counter
# HELP drachtio_sip_requests_out_total count of sip requests sent
# TYPE drachtio_sip_requests_out_total counter
# HELP drachtio_sip_responses_in_total count of sip responses received
# TYPE drachtio_sip_responses_in_total counter
# HELP drachtio_sip_responses_out_total count of sip responses sent
# TYPE drachtio_sip_responses_out_total counter
# HELP drachtio_build_info drachtio version running
# TYPE drachtio_build_info counter
drachtio_build_info{version="v0.8.0-rc7-20-gaf3ddfac7"} 1.000000
# HELP drachtio_time_started drachtio start time
# TYPE drachtio_time_started gauge
drachtio_time_started 1555075955.000000
# HELP drachtio_stable_dialogs count of SIP dialogs in progress
# TYPE drachtio_stable_dialogs gauge
# HELP drachtio_proxy_cores count of proxied call setups in progress
# TYPE drachtio_proxy_cores gauge
# HELP drachtio_registered_endpoints count of registered endpoints
# TYPE drachtio_registered_endpoints gauge
# HELP drachtio_app_connections count of connections to drachtio applications
# TYPE drachtio_app_connections gauge
# HELP sofia_client_txn_hash_size current size of sofia hash table for client transactions
# TYPE sofia_client_txn_hash_size gauge
# HELP sofia_server_txn_hash_size current size of sofia hash table for server transactions
# TYPE sofia_server_txn_hash_size gauge
# HELP sofia_dialog_hash_size current size of sofia hash table for dialogs
# TYPE sofia_dialog_hash_size gauge
# HELP sofia_server_txns_total count of sofia server-side transactions
# TYPE sofia_server_txns_total gauge
# HELP sofia_client_txns_total count of sofia client-side transactions
# TYPE sofia_client_txns_total gauge
# HELP sofia_dialogs_total count of sofia dialogs
# TYPE sofia_dialogs_total gauge
# HELP sofia_msgs_recv_total count of sip messages received by sofia sip stack
# TYPE sofia_msgs_recv_total gauge
# HELP sofia_msgs_sent_total count of sip messages sent by sofia sip stack
# TYPE sofia_msgs_sent_total gauge
# HELP sofia_requests_recv_total count of sip requests received by sofia sip stack
# TYPE sofia_requests_recv_total gauge
# HELP sofia_requests_sent_total count of sip requests sent by sofia sip stack
# TYPE sofia_requests_sent_total gauge
# HELP sofia_bad_msgs_recv_total count of invalid sip messages received by sofia sip stack
# TYPE sofia_bad_msgs_recv_total gauge
# HELP sofia_bad_reqs_recv_total count of invalid sip requests received by sofia sip stack
# TYPE sofia_bad_reqs_recv_total gauge
# HELP sofia_retransmitted_requests_total count of sip requests retransmitted by sofia sip stack
# TYPE sofia_retransmitted_requests_total gauge
# HELP sofia_retransmitted_responses_total count of sip responses retransmitted by sofia sip stack
# TYPE sofia_retransmitted_responses_total gauge
# HELP drachtio_call_answer_seconds_in time to answer incoming call
# TYPE drachtio_call_answer_seconds_in histogram
# HELP drachtio_call_answer_seconds_out time to answer outgoing call
# TYPE drachtio_call_answer_seconds_out histogram
# HELP drachtio_call_pdd_seconds_in call post-dial delay in seconds for calls received
# TYPE drachtio_call_pdd_seconds_in histogram
# HELP drachtio_call_pdd_seconds_out call post-dial delay in seconds for calls sent
# TYPE drachtio_call_pdd_seconds_out histogram
```