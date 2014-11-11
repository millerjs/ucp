/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of udpipe.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.
*****************************************************************************/
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <iostream>
#include <pthread.h>
#include <sys/types.h>

#include <udt.h>

#include "udpipe.h"
#include "udpipe_threads.h"
#include "thread_manager.h"
#include "parcel.h"

#define DEBUG 0
#define EXIT_FAILURE 1

#define prii(x) fprintf(stderr,"debug:%d\n",x)
#define pris(x) fprintf(stderr,"debug: %s\n",x)
#define prisi(x,y) fprintf(stderr,"%s: %d\n",x,y)
#define uc_err(x) {fprintf(stderr,"error:%s\n",x);exit(EXIT_FAILURE);}

const int ECONNLOST = 2001;

using std::cerr;
using std::endl;

int READ_IN = 0;

int g_timeout_sem;
int g_timeout_len;

void kick_monitor(void)
{
	verb(VERB_2, "[%s] Monitor kicked", __func__);
	g_timeout_sem = time(NULL) + g_timeout_len;
}

int check_monitor_timeout(void)
{
	return (g_timeout_sem - time(NULL));
}

void *monitor_timeout(void* arg) {

//	int timeout = *(int*) arg;

	while (1) {
//		sleep(timeout);
		sleep(1);
		if (check_monitor_timeout() <= 0){
			verb(VERB_2, "[%s] Timeout triggered, causing exit", __func__);
//			fprintf(stderr, "Exiting on timeout.\n");
			unregister_thread(get_my_thread_id());
			set_thread_exit();
//			exit(0);
			break;

		} else {
			// continue on as normal
		}

		// If g_timeout_sem == 2, the connection has not been made -> no timeout next round
/*		if (g_timeout_sem != 2) {
			g_timeout_sem = 0;
		} */

		if ( check_for_exit(THREAD_TYPE_2) ) {
			verb(VERB_2, "[%s] Got exit signal, exiting", __func__);
			unregister_thread(get_my_thread_id());
			break;
		}
	}
	return 0;
}

void init_monitor(time_t timeout_len)
{
	g_timeout_len = timeout_len;
	kick_monitor();

}



void send_full(UDTSOCKET sock, char* buffer, int len)
{
	int sent = 0;
	int rs = 0;
	verb(VERB_2, "[%s] Attempting to send %d bytes", __func__, len);
	while (sent < len) {
		rs = UDT::send(sock, buffer+sent, len-sent, 0);
//		verb(VERB_2, "[%s] sent %d bytes", __func__, rs);
		if (UDT::ERROR == rs) {
			if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
				cerr << "recv:" << UDT::getlasterror().getErrorMessage() <<
					"send_full: Unable to send data." << endl;
				exit(1);
			}
		}
		sent += rs;
	}
}

void recv_full(UDTSOCKET sock, char* buffer, int len)
{
	int recvd = 0;
	int rs = 0;
	verb(VERB_2, "[%s] Attempting to receive %d bytes", __func__, len);
	while (recvd < len) {
		rs = UDT::recv(sock, buffer+recvd, len-recvd, 0);
//		rs = UDT::recv(sock, buffer+recvd, 1, 0);
//		verb(VERB_2, "[%s] received %d bytes", __func__, rs);
//		print_bytes(buffer, recvd + rs, 16);
		if (UDT::ERROR == rs) {
			if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
				cerr << "recv:" << UDT::getlasterror().getErrorMessage() <<
					"send_full: Unable to send data." << endl;
				exit(1);
			}
		}
		kick_monitor();
//		g_timeout_sem = 1;
		recvd += rs;
	}
}

const int KEY_LEN = 1026;
//const int KEY_LEN = 64;
//int g_signed_auth = 0;
//int g_authed_peer = 0;


void auth_peer(rs_args* args)
{
	char auth_peer_key[KEY_LEN];
	char signed_key[KEY_LEN];
//	char decoded_signed_key[KEY_LEN];

	RAND_bytes((unsigned char*)auth_peer_key, KEY_LEN);
	verb(VERB_2, "[%s] Generated key:", __func__);
//	print_bytes(auth_peer_key, KEY_LEN, 16);

//	g_authed_peer = 0;

	verb(VERB_2, "[%s] Sending data", __func__);
	send_full(*args->usocket, auth_peer_key, KEY_LEN);

//	while (!g_signed_auth);

	verb(VERB_2, "[%s] Receiving data", __func__);
	recv_full(*args->usocket, signed_key, KEY_LEN);

	verb(VERB_2, "[%s] Received signed key:", __func__);
//	print_bytes(signed_key, KEY_LEN, 16);

	int crypt_len = KEY_LEN/args->n_crypto_threads;
	for (int i = 0; i < args->n_crypto_threads; i ++) {
		verb(VERB_2, "[%s] Passing data to encode/decode thread: %x in, %x out, i = %d, crypt_len = %d", __func__,
			signed_key+(i*crypt_len), signed_key+(i*crypt_len), i, crypt_len);
		pass_to_enc_thread(signed_key+(i*crypt_len), signed_key+(i*crypt_len),
				crypt_len, args->c);
	}

	join_all_encryption_threads(args->c);

	if (memcmp(auth_peer_key, signed_key, KEY_LEN)) {
		verb(VERB_1, "[%s] Authorization failed", __func__);
/*		verb(VERB_2, "key (%x):", auth_peer_key);
		print_bytes(auth_peer_key, KEY_LEN, 16);
		verb(VERB_2, "signed_key (%x):", signed_key);
		print_bytes(signed_key, KEY_LEN, 16); */
//		set_thread_exit();
		exit(1);
	} else {
		verb(VERB_2, "[%s] Key signed OK", __func__);
//		set_encrypt_ready(1);
		set_peer_authed();
//		g_authed_peer = 1;
	}
}


void sign_auth(rs_args* args)
{
	char sign_auth_key[KEY_LEN];
//	char encoded_sign_auth_key[KEY_LEN];

//	g_signed_auth = 0;
	verb(VERB_2, "[%s] Receiving data", __func__);
	// appears to try and receive a key
	recv_full(*args->usocket, sign_auth_key, KEY_LEN);

	verb(VERB_2, "[%s] received key:", __func__);
//	print_bytes(sign_auth_key, KEY_LEN, 16);

	// pass the key to the encode thread
	int crypt_len = KEY_LEN/args->n_crypto_threads;
	for (int i = 0; i < args->n_crypto_threads; i ++) {
		pass_to_enc_thread(sign_auth_key+(i*crypt_len), sign_auth_key+(i*crypt_len),
				crypt_len, args->c);
	}

	join_all_encryption_threads(args->c);

	verb(VERB_2, "[%s] signed key:", __func__);
//	print_bytes(sign_auth_key, KEY_LEN, 16);

	verb(VERB_2, "[%s] Sending data back", __func__);
	// send the key back
	send_full(*args->usocket, sign_auth_key, KEY_LEN);

	// set the g_signed_auth to true
//	g_signed_auth = 1;
	set_auth_signed();

}


void* recvdata(void * _args)
{
	pthread_t   tid;
	tid = pthread_self();

	rs_args * args = (rs_args*)_args;

	verb(VERB_2, "[%s %lu] Initializing receive thread, args->c = %0x", __func__, tid, args->c);

	if (args->use_crypto) {
		verb(VERB_2, "[%s %lu] Receive encryption is on.", __func__, tid);
		if ( args->c == NULL ) {
			fprintf(stderr, "[%s %lu] crypto is NULL on enter, exiting!", __func__, tid);
			exit(0);
		}
	}

	UDTSOCKET recver = *args->usocket;

	int crypto_buff_len = BUFF_SIZE / args->n_crypto_threads;
	int buffer_cursor;

	char* indata = (char*) malloc(BUFF_SIZE*sizeof(char));
	if (!indata) {
		fprintf(stderr, "Unable to allocate decryption buffer");
		exit(EXIT_FAILURE);
	}

	if ( (args->use_crypto) ) {
		if ( args->master ) {
			verb(VERB_2, "[%s %lu] Authorizing peer with key (%x)", __func__, tid, args->master);
			auth_peer(args);
		} else {
			verb(VERB_2, "[%s %lu] Waiting for authed to be signed (%x)...", __func__, tid, args->master);
			while (!get_auth_signed());
			verb(VERB_2, "[%s %lu] Authorizing peer with key (%x)", __func__, tid, args->master);
			auth_peer(args);
		}

	}

	// wait until we're actually done signing
	while ( !get_encrypt_ready() );

//	g_timeout_sem = 2;

	// Create a monitor thread to watch for timeouts
	if (args->timeout > 0) {
		pthread_t monitor_thread;
//		pthread_create(&monitor_thread, NULL, &monitor_timeout, &args->timeout);
		init_monitor(args->timeout);
		create_thread(&monitor_thread, NULL, &monitor_timeout, &args->timeout, "monitor_timeout", THREAD_TYPE_2);
	}

	READ_IN = 1;

	int new_block = 1;
	int block_size = 0;
	int offset = sizeof(int)/sizeof(char);
	int crypto_cursor;

	verb(VERB_2, "[%s %lu] Listening on receive thread, args->c = %0x", __func__, tid, args->c);

	// Set monitor thread to expect a timeout
	kick_monitor();
/*	if (args->timeout) {
//		g_timeout_sem = 1;
	} */

	if(args->use_crypto) {
		verb(VERB_2, "[%s %lu] Entering crypto loop...", __func__, tid);
		if ( args->c == NULL ) {
			fprintf(stderr, "crypto class is NULL, exiting!\n");
			exit(0);
		}
		while(true) {
			int rs;
			if (new_block) {

				block_size = 0;
				rs = UDT::recv(recver, (char*)&block_size, offset, 0);
//				if ( rs )
//					verb(VERB_2, "[%s %lu] new block received %d bytes", __func__, tid, rs);

				if (UDT::ERROR == rs) {
					if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
						cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
						break;
					}
					break;
				}

				new_block = 0;
				buffer_cursor = 0;
				crypto_cursor = 0;

			}

			rs = UDT::recv(recver, indata+buffer_cursor,
				   block_size-buffer_cursor, 0);
//			if ( rs )
//				verb(VERB_2, "[%s %lu] received %d bytes", __func__, tid, rs);

			if (UDT::ERROR == rs) {
				if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
					cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
					break;
				}

				break;
			}

			// Cancel timeout for another args->timeout seconds
			kick_monitor();
/*			if (args->timeout) {
				g_timeout_sem = 1;
			} */

			buffer_cursor += rs;

			// Decrypt any full encryption buffer sectors
			while (crypto_cursor + crypto_buff_len < buffer_cursor) {
				pass_to_enc_thread(indata+crypto_cursor, indata+crypto_cursor,
						   crypto_buff_len, args->c);
				crypto_cursor += crypto_buff_len;
			}

			// If we received the whole block
			if (buffer_cursor == block_size) {
				int size = buffer_cursor - crypto_cursor;
				if ( args->c == NULL ) {
					fprintf(stderr, "[%s %lu] crypto class is NULL before thread, exiting!\n", __func__, tid);
					exit(0);
				}
				pass_to_enc_thread(indata+crypto_cursor, indata+crypto_cursor,
						   size, args->c);
				crypto_cursor += size;

				join_all_encryption_threads(args->c);

				pipe_write(args->recv_pipe[1], indata, block_size);

				buffer_cursor = 0;
				crypto_cursor = 0;
				new_block = 1;
			}
			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				break;
			}
		}

	} else {
		tid = pthread_self();
		verb(VERB_2, "[%s %lu] Entering non-crypto loop...", __func__, tid);
		int rs;
		int temp = 0;
		while (1) {
			verb(VERB_2, "[%s %lu] non-crypto loop", __func__, tid);
/*			if ( temp > 1000 ) {
				temp = 0;
			} else {
				temp++;
			} */

			rs = UDT::recv(recver, indata, BUFF_SIZE, 0);
			if ( rs )
				verb(VERB_2, "[%s %lu] received %d bytes", __func__, tid, rs);
			if (UDT::ERROR == rs) {
				if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
					cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
					verb(VERB_2, "[%s %lu] Exiting on error 1...", __func__, tid);
					break;
				}
				verb(VERB_2, "[%s %lu] Connection lost, exiting", __func__, tid);
				break;
			}

			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				break;
			}

			kick_monitor();
/*			if (args->timeout) {
				g_timeout_sem = 1;
			} */
			pipe_write(args->recv_pipe[1], indata, rs);
		}
	}

	verb(VERB_2, "[%s %lu] Closing up and heading out...", __func__, tid);
//	UDT::close(recver);

	free(indata);
	unregister_thread(get_my_thread_id());
	set_thread_exit();
	return NULL;
}

void senddata_cleanup_handler(void *arg)
{
	verb(VERB_2, "[senddata_cleanup_handler] Cleaning up on way out");

}


void* senddata(void* _args)
{
	rs_args * args = (rs_args*) _args;
	pthread_t   tid;
	int error = 0;

	pthread_cleanup_push(senddata_cleanup_handler, NULL);

	tid = pthread_self();
	verb(VERB_2, "[%s %lu] Initializing send thread...", __func__, tid);

	UDTSOCKET client = *(UDTSOCKET*)args->usocket;

	if (args->use_crypto) {
		verb(VERB_2, "[%s %lu] Send encryption is on.", __func__, tid);
	}

	char* outdata = (char*)malloc(BUFF_SIZE*sizeof(char));

	int crypto_buff_len = BUFF_SIZE / args->n_crypto_threads;

	int offset = sizeof(int)/sizeof(char);
	int bytes_read;

	// verifies that we can encrypt/decrypt
	if ( args->use_crypto ) {
		if ( !args->master ) {
			verb(VERB_2, "[%s %lu] Sending encryption status (%x)...", __func__, tid, args->master);
			sign_auth(args);
		} else {
			verb(VERB_2, "[%s %lu] Waiting for peer to be authed (%x)...", __func__, tid, args->master);
			while (!get_peer_authed());
			verb(VERB_2, "[%s %lu] Sending encryption status (%x)...", __func__, tid, args->master);
			sign_auth(args);
		}
	}

	// wait until we're actually done signing
	while ( !get_encrypt_ready() );

	// long local_openssl_version;
	// if (args->use_crypto)
	// 	local_openssl_version = OPENSSL_VERSION_NUMBER;
	// else
	// 	local_openssl_version = 0;


	// if (UDT::send(client, (char*)&local_openssl_version, sizeof(long), 0) < 0){
	// 	    // cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
	// 	    // UDT::close(client);
	// 	    // exit(1);
	// }

	while (!READ_IN);

	verb(VERB_2, "[%s %lu] Send thread listening on stdin.", __func__, tid);

	if (args->use_crypto) {
		verb(VERB_2, "[%s %lu] Entering crypto loop", __func__, tid);
		while(true) {
			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				break;
			}

			int ss;
//			verb(VERB_2, "[%s %lu] Reading %lu from pipe", __func__, tid, (BUFF_SIZE - offset));
			bytes_read = pipe_read(args->send_pipe[0], outdata+offset, (BUFF_SIZE - offset));

			if(bytes_read < 0) {
//				cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
//				verb(VERB_1, "[%s %lu] Error on read: (%d) %s and (%d) %s", errno, strerror(errno), UDT::getlasterror().getErrorCode(), UDT::getlasterror().getErrorMessage());
				if ( errno != EBADF ) {
					verb(VERB_1, "[%s %lu] Error on read: %d",  __func__, tid, errno);
				}
				break;
			}

			if(bytes_read == 0) {
				sleep(1);
				break;
			}

			// fly - why this check again? if we're in here, isn't it
			// already assumed to be crypto?
			if(args->use_crypto) {

				*((int*)outdata) = bytes_read;
				int crypto_cursor = 0;

				while (crypto_cursor < bytes_read) {
					int size = min(crypto_buff_len, bytes_read-crypto_cursor);
//					verb(VERB_2, "[%s %lu] Passing %d data to encode thread", __func__, tid, size);
					pass_to_enc_thread(outdata+crypto_cursor+offset,
							   outdata+crypto_cursor+offset,
							   size, args->c);

					crypto_cursor += size;
				}

//				verb(VERB_2, "[%s %lu] Joining encryption threads", __func__, tid);
				join_all_encryption_threads(args->c);
				bytes_read += offset;

			}

			int ssize = 0;
			while(ssize < bytes_read) {
				if (UDT::ERROR == (ss = UDT::send(client, outdata + ssize,
								  bytes_read - ssize, 0))) {

//					verb(VERB_1, "[%s %lu] Error on send: (%d) %s and (%d) %s", errno, strerror(errno), UDT::getlasterror().getErrorCode(), UDT::getlasterror().getErrorMessage());
					verb(VERB_1, "[%s %lu] Error on send: (%d) %s", errno, strerror(errno));
//					cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
					error = 1;
					break;
				}
				// fly - kind of a crappy way to exit the outer while here, but
				// I'm trying to centralize things on exit
				if ( error > 0 ) {
					break;
				}
				ssize += ss;
			}

			kick_monitor();
/*			if (args->timeout) {
				g_timeout_sem = 1;
			} */

			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				break;
			}
//			verb(VERB_2, "[%s %lu] Loop...", __func__, tid);
		}

	} else {
		verb(VERB_2, "[%s %lu] Entering non-crypto loop", __func__, tid);
		int temp = 0;
		while (1) {
			verb(VERB_2, "[%s %lu] non-crypto loop", __func__, tid);
/*			if ( temp > 10000 ) {
				temp = 0;
			} else {
				temp++;
			} */
			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				break;
			}

			kick_monitor();
/*			if (args->timeout) {
				g_timeout_sem = 1;
			} */

			bytes_read = pipe_read(args->send_pipe[0], outdata, BUFF_SIZE);
			int ssize = 0;
			int ss;

			if(bytes_read == 0) {
				verb(VERB_2, "[%s %lu] No data read, leaving", __func__, tid);
				break;
			}

			while(ssize < bytes_read) {
				verb(VERB_2, "[%s %lu] Sending %d bytes", __func__, tid, bytes_read - ssize);
				if (UDT::ERROR == (ss = UDT::send(client, outdata + ssize,
								  bytes_read - ssize, 0))) {
//					cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
//					verb(VERB_1, "[%s %lu] Error on send: (%d) %s and (%d) %s", errno, strerror(errno), UDT::getlasterror().getErrorCode(), UDT::getlasterror().getErrorMessage());
					verb(VERB_1, "[%s %lu] Error on send: (%d) %s", errno, strerror(errno));
//					verb(VERB_2, "[%s %lu] Leaving on error", __func__, tid);
					error = 1;
					break;
				}
				// fly - kind of a crappy way to exit the outer while here, but
				// I'm trying to centralize things on exit
				if ( error > 0 ) {
					break;
				}
				ssize += ss;
			}
		}
	}

	sleep(1);
	verb(VERB_2, "[%s %lu] Freeing data & exiting", __func__, tid);
	free(outdata);
	unregister_thread(get_my_thread_id());
	pthread_cleanup_pop(0);
	return NULL;
}


void* monitor(void* s)
{
	UDTSOCKET u = *(UDTSOCKET*)s;

	UDT::TRACEINFO perf;

	cerr << "Snd(Mb/s)\tRcv(Mb/s)\tRTT(ms)\tLoss\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;

	while (true) {
		sleep(1);

		if (UDT::ERROR == UDT::perfmon(u, &perf)) {
			cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
			break;
		}

		cerr << perf.mbpsSendRate << "\t\t"
			 << perf.mbpsRecvRate << "\t\t"
			 << perf.msRTT << "\t"
			 << perf.pktRcvLoss << "\t"
			 << perf.pktRecv << "\t\t\t"
			 << perf.pktRecvACK << "\t"
			 << perf.pktRecvNAK << endl;
	}
	unregister_thread(get_my_thread_id());
	return NULL;

}
