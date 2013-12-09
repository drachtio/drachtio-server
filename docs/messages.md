# Messages

Drachtio is a SIP server that is meant to be controlled by client applications using JSON messaging.  Draschtio-connect, which is a node.js module, is one such example of a client.  This document describes the message flow between a drachtio client and the drachtio server

##Message types

Messages are either a request, a response, or a sip message.  A request must be acknowledged by one and only one response.  All messages have a type property ('request', 'response', 'sip').  

Requests have the following parameters:

* rid (aka request id).  May be any string value so long as it is unique on the sending side.  This value means nothing on the receiving side but is sent back in the response to enable the sender to match responses to request.
* command: a string value indicating what is being requested
* data: an object containing data specific to the command

Responses have the following parameters:

* rid: identifies the request being acknowledged
* data: an object containing details of the response

Sip messages have the following parameters:
* msg: sip message data
* transactionId: identifies a sip transaction, used to match responses to requests

##Message flows

###Authentication

After establishing a tcp connection to the server, a client must authenticate itself, providing a shared secret (currently unencrypted)

```js
C: {
		"type":"request"
		,"command":"auth"
		,"rid":"8da95bd22b964e6be24624a6ec6313ff"
		,"data":{
			"secret":"cymru"
		}
	}
S: {
		"data": {
			"authenticated":true
		}
		,"rid":"8da95bd22b964e6be24624a6ec6313ff"
		,"type":"response"
	}
```

###Request SIP messages

The client sends a message specifying that it wants to receive specific types of SIP requests.  

```js
C: {
		"type":"request"
		,"command":"route"
		,"rid":"f5885448fc9b2b8e53ff991c966d4ecc"
		,"data":{
			"verb":"invite"
		}
	}
S: {
		"type":"response"
		,"rid":"f5885448fc9b2b8e53ff991c966d4ecc"
		,"data":{
			"success":true
		}
	}
```

###Receiving and responding to a SIP request

The server sends a SIP request to a client when it has matched a request specifier.

```js
S:  {
		"type": "request"
		, "rid": "dd815d64-5857-4730-a009-ef9f6da89d9c"
		,"command": "sip"
		, "data": {
			,"transactionId": "694984ea-16c6-46e7-80fc-a3295b19b50a"
			,"message": {
				"request_uri": {.....}
				"headers": {......}
				"payload": {......}
			}
		}
	}
C: {
		"type":"response"
		,"rid":"dd815d64-5857-4730-a009-ef9f6da89d9c"
		,"data":{
			"success":true
		}
	}
```

The client then has the responsibility to respond to the sip request, specifying the SIP transaction by providing the transactionId in the data payload.  A client may provide only one final response, but may provide multiple responses if provisional responses are sent first.  Note that the drachtio server automatically sends a 100 Trying SIP response to all incoming INVITEs so it is not necessary for the client to request that a 100 Trying be sent.

```js
C: {
		"type":"request"
		,"command":"respondToSipRequest"
		,"rid":"bfd2a676a8472ab4c94350ce4d6efac8"
		,"data":{
			"transactionId":"694984ea-16c6-46e7-80fc-a3295b19b50a"
			,"code":500
			,"status":"Internal Server Error - Database down"
			,"opts":{
				"headers":{
					"User-Agent":"Drachtio rocksz"
				}
			}
		}
	}
S: TODO
```

#JSON SIP Message format

An example INVITE message looks like this:
```js
{
	"request_uri": {
		"method": "INVITE"
 		,"version": "SIP/2.0"
 		,"url": {
 			"scheme": "sip"
 			,"user": "11"
 			,"host": "localhost"
 		}
 	}
 	,"headers": {
 		"via": [
 			{
	 			"protocol": "SIP/2.0/UDP"
	 			,"host": "127.0.0.1"
	 			,"port": "39918"
	 			,"branch": "z9hG4bK-d8754z-ce8b6972d37e5b74-1---d8754z-"
	 			,"rport": "39918"
	 			,"params": ["branch=z9hG4bK-d8754z-ce8b6972d37e5b74-1---d8754z-","rport=39918"]
 			}
 		]
	 	,"max_forwards": {"count": "70"}
	 	,"from": {"display": "","tag": "f66e7e75","url": {"scheme": "sip","user": "1234","host": "localhost"},"params": ["tag=f66e7e75"]}
	 	,"to": {"display": "","url": {"scheme": "sip","user": "11","host": "localhost"},"params": []}
	 	,"call_id": "MDJiMjA5NzQxNzliMmZiNDJlZjIxNmVhY2I4MTBlZmY"
	 	,"cseq": {"seq": "1","method": "INVITE"}
	 	,"contact": {"display": "","url": {"scheme": "sip","user": "1234","host": "127.0.0.1","port": "39918"},"params": []}
 	}
}
```